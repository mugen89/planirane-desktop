#include "install.h"

#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>

namespace kd {
using namespace rt;

namespace {
const PROPERTYKEY kPKEY_AppUserModel_ID = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

std::wstring regString(const wchar_t* key, const wchar_t* name) {
  wchar_t v[1200];
  DWORD cb = sizeof v;
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, v, &cb) != ERROR_SUCCESS) return {};
  return v;
}

std::wstring normalized(std::wstring p) {
  p = toLower(p);
  for (auto& c : p)
    if (c == L'/') c = L'\\';
  while (p.size() > 3 && p.back() == L'\\') p.pop_back();
  return p;
}
}  // namespace

std::wstring knownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  std::wstring s;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) s = p;
  CoTaskMemFree(p);
  return s;
}

std::wstring defaultInstallDir() { return joinPath(joinPath(knownFolder(FOLDERID_LocalAppData), L"Programs"), kFolderName); }

std::wstring installedDir() {
  std::wstring d = regString(kUninstallKey, L"InstallLocation");
  return !d.empty() && fileExists(joinPath(d, kExeName)) ? d : std::wstring();
}

std::wstring installedVersion() { return regString(kUninstallKey, L"DisplayVersion"); }

bool isInstalledCopy() {
  std::wstring d = installedDir();
  return !d.empty() && normalized(d) == normalized(dirName(modulePath()));
}

std::wstring startMenuLink() { return joinPath(knownFolder(FOLDERID_Programs), std::wstring(kAppName) + L".lnk"); }
std::wstring desktopLink() { return joinPath(knownFolder(FOLDERID_Desktop), std::wstring(kAppName) + L".lnk"); }

bool createShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& desc) {
  ComPtr<IShellLinkW> sl;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IShellLinkW), (void**)sl.GetAddressOf()))) return false;
  sl->SetPath(target.c_str());
  sl->SetWorkingDirectory(dirName(target).c_str());
  sl->SetDescription(desc.c_str());
  sl->SetIconLocation(target.c_str(), 0);
  // The same AppUserModelID as the running program: Windows then knows the shortcut, the tray icon's
  // notifications and the program are one thing.
  ComPtr<IPropertyStore> ps;
  if (SUCCEEDED(sl.As(&ps))) {
    PROPVARIANT v;
    if (SUCCEEDED(InitPropVariantFromString(kAumid, &v))) {
      ps->SetValue(kPKEY_AppUserModel_ID, v);
      ps->Commit();
      PropVariantClear(&v);
    }
  }
  ComPtr<IPersistFile> pf;
  return SUCCEEDED(sl.As(&pf)) && SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
}

bool shortcutPointsInto(const std::wstring& lnk, const std::wstring& dir) {
  if (!fileExists(lnk)) return false;
  ComPtr<IShellLinkW> sl;
  ComPtr<IPersistFile> pf;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IShellLinkW), (void**)sl.GetAddressOf())) ||
      FAILED(sl.As(&pf)) || FAILED(pf->Load(lnk.c_str(), STGM_READ)))
    return false;
  wchar_t path[MAX_PATH * 2]{};
  if (FAILED(sl->GetPath(path, MAX_PATH * 2, nullptr, SLGP_RAWPATH))) return false;
  return normalized(dirName(path)) == normalized(dir) && toLower(fileName(path)) == toLower(kExeName);
}

bool autostartOn() { return !regString(kRunKey, kRunValue).empty(); }

void setAutostart(bool on, const std::wstring& exe) {
  if (on) {
    std::wstring cmd = L"\"" + exe + L"\" --autostart";
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
  }
}

std::wstring instanceSuffix() { return GetEnvironmentVariableW(L"KANBANDESKTOP_DATA", nullptr, 0) > 0 ? L".alt" : L""; }
std::wstring hiddenWindowClass() { return std::wstring(kHiddenClass) + instanceSuffix(); }

bool sendCommand(const std::wstring& cmd) {
  HWND h = FindWindowW(hiddenWindowClass().c_str(), nullptr);
  if (!h) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  AllowSetForegroundWindow(pid);
  COPYDATASTRUCT cd{kCopyCommand, (DWORD)((cmd.size() + 1) * sizeof(wchar_t)), (void*)cmd.c_str()};
  DWORD_PTR res = 0;
  return SendMessageTimeoutW(h, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 3000, &res) && res;
}

bool quitRunningApp(DWORD waitMs) {
  double end = nowSec() + waitMs / 1000.0;
  for (;;) {
    HWND h = FindWindowW(hiddenWindowClass().c_str(), nullptr);
    if (!h) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
    sendCommand(L"quit");
    bool gone = false;
    if (p) {
      gone = WaitForSingleObject(p, (DWORD)(std::max(0.0, end - nowSec()) * 1000.0)) == WAIT_OBJECT_0;
      CloseHandle(p);
    } else {
      Sleep(300);
    }
    if (!gone && nowSec() >= end) return FindWindowW(hiddenWindowClass().c_str(), nullptr) == nullptr;
  }
}

}  // namespace kd
