// HTTP over WinHTTP (part of Windows, nothing to ship). Requests run one at a time on a worker thread,
// so a slow or unreachable server never freezes the taskbar; every answer comes back on the UI thread.
#pragma once
#include "common.h"

namespace kd {

struct HttpRequest {
  std::wstring method = L"GET";
  std::wstring url;           // absolute: http://host:port/path
  std::string body;           // JSON; empty = no body
  std::string cookie;         // "name=value", sent as the Cookie header
  int timeoutMs = 15000;
  bool followRedirects = true;
  std::string accept = "application/json";
};

struct HttpResult {
  int status = 0;             // 0: no answer, see error
  std::string body;
  std::vector<std::string> setCookies;
  std::string location;      // a redirect's target (when followRedirects is off)
  DWORD error = 0;            // WinHTTP / Win32 error when there was no answer
};

HttpResult httpSend(const HttpRequest& r);  // blocking: only on the worker thread or in tools

// The worker. `done` runs on the thread that owns `notify`, when that window handles `msg`.
void netStart(HWND notify, UINT msg);
void netStop();
void netPost(HttpRequest r, std::function<void(const HttpResult&)> done);
void netDeliver(LPARAM l);  // call from the window procedure for `msg`

std::wstring netErrorText(DWORD error);  // Bulgarian, for the screen

}  // namespace kd
