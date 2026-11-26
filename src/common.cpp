#include "common.h"

#include <shellscalingapi.h>
#include <stdarg.h>

namespace rt {

// ---------------------------------------------------------------- colour
bool Color::parse(std::string_view s, Color& out) {
  if (!s.empty() && s[0] == '#') s.remove_prefix(1);
  if (s.size() != 6 && s.size() != 8) return false;
  uint32_t v = 0;
  for (char ch : s) {
    int d;
    if (ch >= '0' && ch <= '9') d = ch - '0';
    else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
    else return false;
    v = (v << 4) | (uint32_t)d;
  }
  float a = 1;
  if (s.size() == 8) {
    a = (v & 255) / 255.f;
    v >>= 8;
  }
  out = Color::hex(v, a);
  return true;
}

std::string Color::str() const {
  auto q = [](float f) { return (int)lroundf(saturate(f) * 255.f); };
  char buf[16];
  if (a >= 0.999f) snprintf(buf, sizeof buf, "#%02X%02X%02X", q(r), q(g), q(b));
  else snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", q(r), q(g), q(b), q(a));
  return buf;
}

Hsv toHsv(Color c) {
  float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
  Hsv o;
  o.v = mx;
  o.s = mx <= 0 ? 0 : d / mx;
  if (d <= 1e-6f) o.h = 0;
  else if (mx == c.r) o.h = fmodf((c.g - c.b) / d + 6.f, 6.f) / 6.f;
  else if (mx == c.g) o.h = ((c.b - c.r) / d + 2.f) / 6.f;
  else o.h = ((c.r - c.g) / d + 4.f) / 6.f;
  return o;
}

Color fromHsv(Hsv h, float a) {
  float hh = fmodf(h.h, 1.f);
  if (hh < 0) hh += 1.f;
  hh *= 6.f;
  int i = (int)hh;
  float f = hh - i, p = h.v * (1 - h.s), q = h.v * (1 - h.s * f), t = h.v * (1 - h.s * (1 - f));
  switch (i % 6) {
    case 0: return {h.v, t, p, a};
    case 1: return {q, h.v, p, a};
    case 2: return {p, h.v, t, a};
    case 3: return {p, q, h.v, a};
    case 4: return {t, p, h.v, a};
    default: return {h.v, p, q, a};
  }
}

// ---------------------------------------------------------------- spring
void Spring::step(float dt, float freqHz, float damping) {
  if (dt <= 0) return;
  float w0 = kTau * freqHz;
  float y = x - target;
  if (damping < 0.999f) {
    float wd = w0 * sqrtf(1.f - damping * damping);
    float e = expf(-damping * w0 * dt);
    float c = cosf(wd * dt), s = sinf(wd * dt);
    float A = y, B = (v + damping * w0 * y) / wd;
    x = target + e * (A * c + B * s);
    v = e * ((-damping * w0) * (A * c + B * s) + wd * (-A * s + B * c));
  } else {
    float e = expf(-w0 * dt);
    float B = v + w0 * y;
    x = target + (y + B * dt) * e;
    v = (B - w0 * (y + B * dt)) * e;
  }
  if (!std::isfinite(x) || !std::isfinite(v)) snap(target);
}

// ---------------------------------------------------------------- strings
std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring o((size_t)n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n);
  return o;
}
std::string narrow(std::wstring_view s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string o((size_t)n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n, nullptr, nullptr);
  return o;
}
std::wstring toLower(std::wstring s) {
  if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
  return s;
}
std::wstring trim(std::wstring s) {
  size_t a = s.find_first_not_of(L" \t\r\n");
  if (a == std::wstring::npos) return {};
  size_t b = s.find_last_not_of(L" \t\r\n");
  return s.substr(a, b - a + 1);
}
std::wstring fileName(const std::wstring& path) {
  size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? path : path.substr(i + 1);
}
std::wstring fileStem(const std::wstring& path) {
  std::wstring n = fileName(path);
  size_t d = n.find_last_of(L'.');
  return (d == std::wstring::npos || d == 0) ? n : n.substr(0, d);
}
std::wstring fileExt(const std::wstring& path) {
  std::wstring n = fileName(path);
  size_t d = n.find_last_of(L'.');
  return d == std::wstring::npos ? L"" : toLower(n.substr(d));
}
std::wstring dirName(const std::wstring& path) {
  size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? L"" : path.substr(0, i);
}
std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}
std::wstring expandEnv(const std::wstring& s) {
  if (s.find(L'%') == std::wstring::npos) return s;
  DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
  if (!n) return s;
  std::wstring o(n, 0);
  n = ExpandEnvironmentStringsW(s.c_str(), o.data(), n);
  if (n) o.resize(n - 1);
  return o;
}
bool fileExists(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool dirExists(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring modulePath() {
  std::wstring s(1024, 0);
  DWORD n = GetModuleFileNameW(nullptr, s.data(), (DWORD)s.size());
  s.resize(n);
  return s;
}
std::wstring dataDir() {
  static const std::wstring dir = [] {
    std::wstring d;
    // An override keeps automated checks away from the person's real settings.
    wchar_t env[1024];
    DWORD n = GetEnvironmentVariableW(L"KANBANDESKTOP_DATA", env, 1024);
    if (n > 0 && n < 1024) {
      d = env;
    } else {
      PWSTR p = nullptr;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) d = joinPath(p, L"KanbanDesktop");
      CoTaskMemFree(p);
    }
    SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
    return d;
  }();
  return dir;
}
std::wstring cacheDir() {
  static const std::wstring dir = [] {
    std::wstring d;
    if (GetEnvironmentVariableW(L"KANBANDESKTOP_DATA", nullptr, 0) > 0) {
      d = joinPath(dataDir(), L"cache");
    } else {
      PWSTR p = nullptr;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) d = joinPath(p, L"KanbanDesktop");
      CoTaskMemFree(p);
    }
    SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
    return d;
  }();
  return dir;
}
void sweepCache(bool all) {
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  auto num = [](FILETIME f) { return (int64_t)(((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime); };
  for (const wchar_t* pattern : {L"*.req", L"*.json", L"*.tmp", L"icons\\*.tmp"}) {
    std::wstring dir = cacheDir();
    if (wcschr(pattern, L'\\')) dir = joinPath(dir, L"icons");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(joinPath(cacheDir(), pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (all || num(now) - num(fd.ftLastWriteTime) > 36000000000ll) DeleteFileW(joinPath(dir, fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
}
std::string newId() {
  GUID g{};
  if (FAILED(CoCreateGuid(&g))) QueryPerformanceCounter((LARGE_INTEGER*)&g);
  char b[40];
  snprintf(b, sizeof b, "%08lx%04x%02x%02x", g.Data1, g.Data2, g.Data4[6], g.Data4[7]);
  return b;
}
double nowSec() {
  static LARGE_INTEGER f = [] {
    LARGE_INTEGER q;
    QueryPerformanceFrequency(&q);
    return q;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)f.QuadPart;
}

bool readFile(const std::wstring& path, std::string& out, DWORD* error) {
  if (error) *error = 0;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (error) *error = GetLastError();
    return false;
  }
  LARGE_INTEGER sz{};
  bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < 64 * 1024 * 1024;
  if (ok) {
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size());
  }
  if (!ok && error) *error = GetLastError() ? GetLastError() : ERROR_READ_FAULT;
  CloseHandle(h);
  return ok;
}

bool writeFileAtomic(const std::wstring& path, const std::string& data, bool keepBackup) {
  std::wstring tmp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD wr = 0;
  bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
  if (ok) FlushFileBuffers(h);
  CloseHandle(h);
  if (ok) {
    for (int i = 0; i < 24; i++) {
      if (keepBackup && fileExists(path)) ok = ReplaceFileW(path.c_str(), tmp.c_str(), (path + L".bak").c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != FALSE;
      else ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
      if (ok) break;
      Sleep(45);
    }
  }
  if (!ok) DeleteFileW(tmp.c_str());
  return ok;
}

HANDLE startHelper(const std::wstring& args, bool quick) {
  // Helpers live in a job that dies with us, so none is left holding the program file during an update.
  static HANDLE job = [] {
    HANDLE j = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (j) SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof li);
    return j;
  }();
  constexpr DWORD_PTR kJobList = 0x0002000D;  // PROC_THREAD_ATTRIBUTE_JOB_LIST
  SIZE_T cb = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &cb);
  std::vector<uint8_t> buf(cb ? cb : 64);
  auto* list = (LPPROC_THREAD_ATTRIBUTE_LIST)buf.data();
  bool attrs = job && InitializeProcThreadAttributeList(list, 1, 0, &cb);
  if (attrs && !UpdateProcThreadAttribute(list, 0, kJobList, &job, sizeof job, nullptr, nullptr)) {
    DeleteProcThreadAttributeList(list);
    attrs = false;
  }
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = attrs ? sizeof si : sizeof si.StartupInfo;
  si.StartupInfo.dwFlags = STARTF_FORCEOFFFEEDBACK;
  si.lpAttributeList = attrs ? list : nullptr;
  std::wstring exe = modulePath(), cmd = L"\"" + exe + L"\" " + args;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW | (quick ? NORMAL_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS) | (attrs ? EXTENDED_STARTUPINFO_PRESENT : 0), nullptr, nullptr,
                           &si.StartupInfo, &pi);
  DWORD err = GetLastError();
  if (attrs) DeleteProcThreadAttributeList(list);
  if (!ok) {
    logf("helper did not start (%lu)", err);
    return nullptr;
  }
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64(const std::string& in) {
  std::string o;
  for (size_t i = 0; i < in.size(); i += 3) {
    uint32_t v = (uint8_t)in[i] << 16;
    if (i + 1 < in.size()) v |= (uint8_t)in[i + 1] << 8;
    if (i + 2 < in.size()) v |= (uint8_t)in[i + 2];
    o += kB64[v >> 18];
    o += kB64[(v >> 12) & 63];
    o += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
    o += i + 2 < in.size() ? kB64[v & 63] : '=';
  }
  return o;
}
bool unbase64(const std::string& t, std::string& out) {
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  for (char c : t) {
    if (c == '=') break;
    const char* q = c ? strchr(kB64, c) : nullptr;
    if (!q) return false;
    acc = (acc << 6) | (uint32_t)(q - kB64);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((acc >> bits) & 255);
    }
  }
  return true;
}

void logf(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  OutputDebugStringA(buf);
  OutputDebugStringA("\n");
  static wchar_t path[1024];
  static int toFile = -1;
  if (toFile < 0) {
    DWORD n = GetEnvironmentVariableW(L"KANBANDESKTOP_LOG", path, 1024);
    toFile = (n > 0 && n < 1024) ? 1 : 0;
  }
  if (!toFile) return;
  HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  char line[1100];
  int n = snprintf(line, sizeof line, "[%10.3f] %s\r\n", nowSec(), buf);
  DWORD wr;
  WriteFile(h, line, (DWORD)n, &wr, nullptr);
  CloseHandle(h);
}

// ---------------------------------------------------------------- system look
static DWORD regDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD def) {
  DWORD v = def, cb = sizeof v;
  RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &v, &cb);
  return v;
}
bool systemUsesLightTheme() {
  return regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", 1) != 0;
}
Color systemAccent() {
  DWORD v = regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0xFFD47800);
  return {(v & 255) / 255.f, ((v >> 8) & 255) / 255.f, ((v >> 16) & 255) / 255.f, 1.f};
}
bool systemAnimationsEnabled() {
  BOOL on = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
  return on != FALSE;
}
float dpiScaleFor(HWND hwnd) {
  UINT d = hwnd ? GetDpiForWindow(hwnd) : 96;
  return d ? d / 96.f : 1.f;
}
float dpiScaleFor(HMONITOR mon) {
  UINT x = 96, y = 96;
  if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &x, &y))) x = 96;
  return x / 96.f;
}

}  // namespace rt
