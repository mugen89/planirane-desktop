#include "update.h"

#include <bcrypt.h>

#include "install.h"
#include "net.h"

namespace kd {
using namespace rt;

namespace {
std::string sha256Hex(const std::string& data) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  unsigned char digest[32]{};
  std::string out;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
      if (BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 && BCryptFinishHash(h, digest, sizeof digest, 0) == 0) {
        static const char* hex = "0123456789abcdef";
        for (unsigned char b : digest) {
          out += hex[b >> 4];
          out += hex[b & 15];
        }
      }
      BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
  }
  return out;
}

std::string firstHash(const std::string& text) {  // "e3b0…  KanbanDesktop-Setup.exe" → "e3b0…"
  std::string h;
  for (char c : text) {
    char l = (char)tolower((unsigned char)c);
    if ((l >= '0' && l <= '9') || (l >= 'a' && l <= 'f')) {
      h += l;
      if (h.size() == 64) return h;
    } else if (!h.empty()) {
      h.clear();
    }
  }
  return {};
}
}  // namespace

std::wstring updateBase() {
  wchar_t env[512];
  DWORD n = GetEnvironmentVariableW(L"KANBANDESKTOP_UPDATE_BASE", env, 512);
  std::wstring b = n > 0 && n < 512 ? std::wstring(env) : std::wstring(kRepoUrl);
  while (!b.empty() && b.back() == L'/') b.pop_back();
  return b;
}

int compareVersions(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    long x = 0, y = 0;
    while (i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
    while (j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
    if (x != y) return x < y ? -1 : 1;
    while (i < a.size() && !isdigit((unsigned char)a[i])) i++;
    while (j < b.size() && !isdigit((unsigned char)b[j])) j++;
  }
  return 0;
}

void checkLatestRelease(std::function<void(const std::string&, const std::wstring&)> done) {
  HttpRequest r;
  r.url = updateBase() + L"/releases/latest";
  r.followRedirects = false;
  r.accept = "text/html";
  r.timeoutMs = 20000;
  netPost(std::move(r), [done = std::move(done)](const HttpResult& h) {
    if (h.status == 0) return done({}, netErrorText(h.error));
    size_t at = h.location.find("/releases/tag/");
    if (h.status >= 300 && h.status < 400 && at != std::string::npos) {
      std::string tag = h.location.substr(at + 14);
      tag = tag.substr(0, tag.find_first_of("/?#"));
      if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
      return done(tag, {});
    }
    if (h.status == 404 || (h.status >= 300 && h.status < 400)) return done({}, {});  // nothing published yet
    wchar_t buf[96];
    swprintf(buf, 96, L"GitHub отговори с %d при проверката за обновление.", h.status);
    done({}, buf);
  });
}

void downloadSetup(const std::string& version, std::function<void(const std::wstring&, const std::wstring&)> done) {
  std::wstring base = updateBase() + L"/releases/download/v" + widen(version) + L"/" + kSetupAsset;
  HttpRequest sum;
  sum.url = base + L".sha256";
  sum.accept = "*/*";
  sum.timeoutMs = 30000;
  netPost(std::move(sum), [version, base, done = std::move(done)](const HttpResult& hs) {
    std::string want = hs.status == 200 ? firstHash(hs.body) : std::string();
    if (want.empty()) return done({}, L"Липсва контролната сума на версия " + widen(version) + L" — обновлението е спряно.");
    HttpRequest exe;
    exe.url = base;
    exe.accept = "application/octet-stream";
    exe.timeoutMs = 180000;
    netPost(std::move(exe), [version, want, done](const HttpResult& he) {
      if (he.status != 200 || he.body.size() < 100 * 1024)
        return done({}, he.status ? L"Изтеглянето на обновлението не успя." : netErrorText(he.error));
      if (sha256Hex(he.body) != want) return done({}, L"Изтегленото обновление не съвпада с контролната си сума — не е инсталирано.");
      std::wstring dir = joinPath(knownFolder(FOLDERID_LocalAppData), L"Temp\\KanbanDesktop-Update");
      SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
      std::wstring path = joinPath(dir, L"KanbanDesktop-Setup-" + widen(version) + L".exe");
      if (!writeFileAtomic(path, he.body, false)) return done({}, L"Обновлението не можа да бъде записано на диска.");
      done(path, {});
    });
  });
}

bool startSetup(const std::wstring& path) {
  std::wstring cmd = L"\"" + path + L"\" --update";
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dirName(path).c_str(), &si, &pi)) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}  // namespace kd
