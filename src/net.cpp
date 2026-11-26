#include "net.h"

#include <winhttp.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace kd {
using namespace rt;

namespace {

struct Job {
  HttpRequest req;
  std::function<void(const HttpResult&)> done;
  HttpResult res;
};

struct Worker {
  std::mutex m;
  std::condition_variable cv;
  std::deque<Job*> q;
  std::thread thread;
  HWND notify = nullptr;
  UINT msg = 0;
  bool stop = false;
};
Worker& worker() {
  static Worker w;
  return w;
}

// One WinHTTP session for the whole process; used only from the worker thread.
HINTERNET session() {
  static HINTERNET s = [] {
    std::wstring agent = widen("KanbanDesktop/" KD_VERSION_STR);
    return WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }();
  return s;
}

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

void loop() {
  auto& w = worker();
  for (;;) {
    Job* j = nullptr;
    {
      std::unique_lock<std::mutex> lk(w.m);
      w.cv.wait(lk, [&] { return w.stop || !w.q.empty(); });
      if (w.stop) return;
      j = w.q.front();
      w.q.pop_front();
    }
    double t0 = nowSec();
    j->res = httpSend(j->req);
    {
      std::string url = narrow(j->req.url);
      size_t path = url.find('/', url.find("://") + 3);
      logf("net: %s %s -> %d in %.0f ms%s", narrow(j->req.method).c_str(), path == std::string::npos ? "/" : url.c_str() + path,
           j->res.status, (nowSec() - t0) * 1000.0, j->res.status ? "" : " (no answer)");
    }
    HWND to = nullptr;
    UINT msg = 0;
    {
      std::lock_guard<std::mutex> lk(w.m);
      to = w.stop ? nullptr : w.notify;
      msg = w.msg;
    }
    if (!to || !PostMessageW(to, msg, 0, (LPARAM)j)) delete j;
  }
}

}  // namespace

HttpResult httpSend(const HttpRequest& r) {
  HttpResult out;
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof uc;
  wchar_t host[256]{}, path[4096]{}, extra[2048]{};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 4096;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(r.url.c_str(), 0, 0, &uc)) {
    out.error = GetLastError();
    return out;
  }
  HINTERNET s = session();
  if (!s) {
    out.error = GetLastError();
    return out;
  }
  Handle c{WinHttpConnect(s, host, uc.nPort, 0)};
  if (!c.h) {
    out.error = GetLastError();
    return out;
  }
  std::wstring object = std::wstring(path) + extra;
  DWORD flags = WINHTTP_FLAG_REFRESH | (uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  Handle q{WinHttpOpenRequest(c.h, r.method.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
  if (!q.h) {
    out.error = GetLastError();
    return out;
  }
  // The session cookie is ours to keep (it survives restarts); WinHTTP must not keep a second copy.
  DWORD features = WINHTTP_DISABLE_COOKIES | (r.followRedirects ? 0 : WINHTTP_DISABLE_REDIRECTS);
  WinHttpSetOption(q.h, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof features);
  WinHttpSetTimeouts(q.h, r.timeoutMs, r.timeoutMs, r.timeoutMs, r.timeoutMs);

  std::wstring headers = L"Accept: " + widen(r.accept) + L"\r\nX-Client: KanbanDesktop/" + widen(KD_VERSION_STR) + L"\r\n";
  if (r.method != L"GET") headers += L"Content-Type: application/json; charset=utf-8\r\n";
  if (!r.cookie.empty()) headers += L"Cookie: " + widen(r.cookie) + L"\r\n";
  // A POST with no body still says how long it is (Express waits for it otherwise behind some proxies).
  const std::string body = r.body.empty() && r.method != L"GET" ? std::string("{}") : r.body;
  BOOL ok = WinHttpSendRequest(q.h, headers.c_str(), (DWORD)-1L, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                               (DWORD)body.size(), (DWORD)body.size(), 0);
  if (ok) ok = WinHttpReceiveResponse(q.h, nullptr);
  if (!ok) {
    out.error = GetLastError();
    return out;
  }
  DWORD code = 0, len = sizeof code;
  WinHttpQueryHeaders(q.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                      WINHTTP_NO_HEADER_INDEX);
  out.status = (int)code;
  {
    wchar_t loc[2048]{};
    DWORD size = sizeof loc;
    if (WinHttpQueryHeaders(q.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &size, WINHTTP_NO_HEADER_INDEX))
      out.location = narrow(loc);
  }

  for (DWORD index = 0; index < 32;) {
    DWORD size = 0, at = index;
    WinHttpQueryHeaders(q.h, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, &at);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) break;
    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    at = index;
    if (!WinHttpQueryHeaders(q.h, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &size, &at)) break;
    buf.resize(size / sizeof(wchar_t));
    out.setCookies.push_back(narrow(buf));
    if (at == index) break;  // the index did not move: nothing more to read
    index = at;
  }

  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(q.h, &avail)) {
      out.error = GetLastError();
      break;
    }
    if (!avail) break;
    size_t at = out.body.size();
    out.body.resize(at + avail);
    DWORD got = 0;
    if (!WinHttpReadData(q.h, out.body.data() + at, avail, &got)) {
      out.error = GetLastError();
      out.body.resize(at);
      break;
    }
    out.body.resize(at + got);
    if (out.body.size() > (64u << 20)) break;  // nothing we ask for is this big
  }
  return out;
}

void netStart(HWND notify, UINT msg) {
  auto& w = worker();
  std::lock_guard<std::mutex> lk(w.m);
  w.notify = notify;
  w.msg = msg;
  w.stop = false;
  if (!w.thread.joinable()) w.thread = std::thread(loop);
}

void netStop() {
  auto& w = worker();
  {
    std::lock_guard<std::mutex> lk(w.m);
    w.stop = true;
    w.notify = nullptr;
    for (Job* j : w.q) delete j;
    w.q.clear();
  }
  w.cv.notify_all();
  // A request already on the wire may take up to its timeout; the process does not wait for it.
  if (w.thread.joinable()) w.thread.detach();
}

void netPost(HttpRequest r, std::function<void(const HttpResult&)> done) {
  auto& w = worker();
  auto* j = new Job{std::move(r), std::move(done), {}};
  {
    std::lock_guard<std::mutex> lk(w.m);
    if (w.stop) {
      delete j;
      return;
    }
    w.q.push_back(j);
  }
  w.cv.notify_one();
}

void netDeliver(LPARAM l) {
  auto* j = (Job*)l;
  if (j->done) j->done(j->res);
  delete j;
}

std::wstring netErrorText(DWORD e) {
  switch (e) {
    case ERROR_WINHTTP_CANNOT_CONNECT: return L"Сървърът не отговаря на този адрес.";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return L"Няма такъв сървър (проверете адреса).";
    case ERROR_WINHTTP_TIMEOUT: return L"Сървърът не отговори навреме.";
    case ERROR_WINHTTP_CONNECTION_ERROR: return L"Връзката със сървъра прекъсна.";
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return L"Адресът на сървъра не е правилен.";
    case ERROR_WINHTTP_SECURE_FAILURE: return L"Проблем със сертификата на сървъра (HTTPS).";
    default: {
      wchar_t buf[64];
      swprintf(buf, 64, L"Няма връзка със сървъра (код %lu).", e);
      return buf;
    }
  }
}

}  // namespace kd
