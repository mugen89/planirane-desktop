// Setup and uninstall of Kanban Desktop („Контрол на срокове“). Per user, no administrator rights; the program
// itself is embedded as a resource.
//
// The same exe is also copied into the install folder as Uninstall.exe: started under that name (or with
// --uninstall) it removes the program. Settings › Apps runs that copy.
//
//   KanbanDesktop-Setup.exe                     the window: folder, Start with Windows, desktop icon
//   KanbanDesktop-Setup.exe --silent | --update install / update with the current choices (the app's updater uses --update)
//       --dir <folder>  --no-autostart  --no-desktop  --desktop  --no-launch
//   Uninstall.exe  [--silent] [--purge]         remove (purge: also this PC's settings and session)
#include "install.h"
#include "ui.h"

#include <commctrl.h>
#include <process.h>
#include <shobjidl.h>

#include <atomic>

using namespace rt;
using namespace kd;

namespace {

struct Options {
  std::wstring dir;
  bool autostart = true, desktop = true, launch = true;
};

std::atomic<int> g_progress{0};  // 0..100
std::wstring g_error;
bool g_upgrade = false;

bool writePayload(const std::wstring& path) {
  HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(100), RT_RCDATA);
  HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
  const void* data = g ? LockResource(g) : nullptr;
  DWORD size = r ? SizeofResource(nullptr, r) : 0;
  if (!data || size < 1024) {
    g_error = L"Инсталаторът е повреден. Изтеглете го отново.";
    return false;
  }
  std::wstring tmp = path + L".new";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    g_error = L"В тази папка не може да се пише. Изберете друга (например в профила си).";
    return false;
  }
  DWORD done = 0;
  bool ok = true;
  while (ok && done < size) {
    DWORD chunk = std::min<DWORD>(128 * 1024, size - done), wr = 0;
    ok = WriteFile(h, (const BYTE*)data + done, chunk, &wr, nullptr) && wr == chunk;
    done += wr;
    g_progress = 20 + (int)(50.0 * done / size);
  }
  if (ok) ok = FlushFileBuffers(h) != FALSE;
  CloseHandle(h);
  if (ok) {
    for (int i = 0; i < 25; i++) {  // a copy that has just closed can keep the old file busy for a moment
      ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
      if (ok) break;
      Sleep(160);
    }
  }
  if (!ok) {
    DeleteFileW(tmp.c_str());
    g_error = L"Програмата не можа да бъде заменена. Още ли работи?";
  }
  return ok;
}

void setReg(HKEY k, const wchar_t* n, const std::wstring& v) {
  RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
void setReg(HKEY k, const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, 4); }

void launch(const std::wstring& dir) {
  std::wstring exe = joinPath(dir, kExeName);
  AllowSetForegroundWindow(ASFW_ANY);
  ShellExecuteW(nullptr, nullptr, exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

bool install(const Options& o) {
  g_progress = 5;
  std::wstring exe = joinPath(o.dir, kExeName);
  if (!quitRunningApp(8000)) {
    g_error = L"„Контрол на срокове“ работи и не се затвори. Затворете го от иконата до часовника и опитайте пак.";
    return false;
  }
  bool hadRunning = fileExists(exe);  // an update: whatever happens, the person must not be left without the program
  g_progress = 15;
  int e = SHCreateDirectoryExW(nullptr, o.dir.c_str(), nullptr);
  if (e != ERROR_SUCCESS && e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS) {
    g_error = L"Папката не може да бъде създадена. Изберете друга (например в профила си).";
    return false;
  }
  if (!writePayload(exe)) {
    if (hadRunning && fileExists(exe)) launch(o.dir);
    return false;
  }
  g_progress = 75;

  // The uninstaller lives beside the program: this very setup, under another name.
  std::wstring self = modulePath(), un = joinPath(o.dir, kUninstallExe);
  if (toLower(self) != toLower(un)) CopyFileW(self.c_str(), un.c_str(), FALSE);

  createShortcut(startMenuLink(), exe, L"Хронометърът и сроковете — на лентата на задачите");
  if (o.desktop) createShortcut(desktopLink(), exe, L"Хронометърът и сроковете — на лентата на задачите");
  else if (shortcutPointsInto(desktopLink(), o.dir)) DeleteFileW(desktopLink().c_str());
  setAutostart(o.autostart, exe);
  g_progress = 85;

  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t date[16];
    swprintf(date, 16, L"%04d%02d%02d", t.wYear, t.wMonth, t.wDay);
    setReg(k, L"DisplayName", kAppName);
    setReg(k, L"DisplayVersion", widen(KD_VERSION_STR));
    setReg(k, L"DisplayIcon", exe);
    setReg(k, L"InstallLocation", o.dir);
    setReg(k, L"InstallDate", date);
    setReg(k, L"UninstallString", L"\"" + un + L"\"");
    setReg(k, L"QuietUninstallString", L"\"" + un + L"\" --silent");
    setReg(k, L"URLInfoAbout", kRepoUrl);
    setReg(k, L"EstimatedSize", (fa.nFileSizeLow + 1023) / 1024 * 3);
    setReg(k, L"NoModify", 1);
    setReg(k, L"NoRepair", 1);
    RegCloseKey(k);
  }
  g_progress = 100;
  return true;
}

// ---------------------------------------------------------------- uninstall
// A running exe cannot delete itself: stage 1 copies this program to the temp folder and starts it there
// for stage 2, which removes the files, the shortcuts, Start with Windows and the Settings › Apps entry.
int uninstallStage1(bool silent, bool purge) {
  std::wstring dir = installedDir();
  if (dir.empty()) dir = dirName(modulePath());
  if (!fileExists(joinPath(dir, kExeName)) && !fileExists(joinPath(dir, kUninstallExe))) {
    if (!silent) MessageBoxW(nullptr, L"„Контрол на срокове“ не е инсталиран на този компютър.", kAppName, MB_ICONINFORMATION);
    return 2;
  }
  if (!silent) {
    TASKDIALOG_BUTTON btn[] = {{IDOK, L"Премахни"}};
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof c;
    c.hInstance = GetModuleHandleW(nullptr);
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    c.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = MAKEINTRESOURCEW(1);
    c.pszMainInstruction = L"Да се премахне ли „Контрол на срокове“?";
    c.pszContent = L"Премахват се програмата, преките пътища и стартирането с Windows. Данните ви са на сървъра и не се засягат.";
    c.pszVerificationText = L"Изтрий и настройките на този компютър (адрес на сървъра, потребител, сесия)";
    c.pButtons = btn;
    c.cButtons = 1;
    int pressed = 0;
    BOOL checked = FALSE;
    if (FAILED(TaskDialogIndirect(&c, &pressed, nullptr, &checked)) || pressed != IDOK) return 0;
    purge = checked != FALSE;
  }
  std::wstring tmp = joinPath(knownFolder(FOLDERID_LocalAppData), L"Temp\\KanbanDesktop-Uninstall");
  SHCreateDirectoryExW(nullptr, tmp.c_str(), nullptr);
  std::wstring copy = joinPath(tmp, kUninstallExe);
  if (!CopyFileW(modulePath().c_str(), copy.c_str(), FALSE)) return 1;
  // Stage 2 waits for this process to end: this file may be the Uninstall.exe it is about to delete.
  std::wstring cmd = L"\"" + copy + L"\" --uninstall-run \"" + dir + L"\" --after " + std::to_wstring(GetCurrentProcessId()) +
                     (silent ? L" --silent" : L"") + (purge ? L" --purge" : L"");
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, tmp.c_str(), &si, &pi)) return 1;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}

bool deleteWithRetry(const std::wstring& f) {
  for (int i = 0; i < 40; i++) {
    if (DeleteFileW(f.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return true;
    Sleep(150);  // stage 1 or the program may still be exiting
  }
  return false;
}

int uninstallStage2(const std::wstring& dir, DWORD after, bool silent, bool purge) {
  // Only ever a folder that really holds this program — and inside it only the two files we put there.
  if (dir.size() < 4 || (!fileExists(joinPath(dir, kExeName)) && !fileExists(joinPath(dir, kUninstallExe)))) return 2;
  if (after) {  // stage 1, which may be running from the Uninstall.exe in that folder
    if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, after)) {
      WaitForSingleObject(p, 10000);
      CloseHandle(p);
    }
  }
  if (!quitRunningApp(8000)) {
    if (!silent) MessageBoxW(nullptr, L"„Контрол на срокове“ още работи. Затворете го от иконата до часовника и опитайте пак.", kAppName, MB_ICONWARNING);
    return 3;
  }
  std::wstring run = [] {
    wchar_t v[1200];
    DWORD cb = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, v, &cb) == ERROR_SUCCESS ? std::wstring(v) : std::wstring();
  }();
  if (!run.empty() && toLower(run).find(toLower(dir)) != std::wstring::npos) setAutostart(false, L"");
  for (auto& l : {startMenuLink(), desktopLink()})
    if (shortcutPointsInto(l, dir)) DeleteFileW(l.c_str());

  bool gone = deleteWithRetry(joinPath(dir, kExeName));
  deleteWithRetry(joinPath(dir, kUninstallExe));
  DeleteFileW(joinPath(dir, std::wstring(kExeName) + L".new").c_str());
  RemoveDirectoryW(dir.c_str());  // only when nothing else is in it: the folder may be one the person chose
  if (gone) RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);

  if (purge) {
    std::wstring d = dataDir();
    if (toLower(fileName(d)) == L"kanbandesktop") {
      for (const wchar_t* f : {L"config.json", L"config.json.bak", L"session.dat"}) DeleteFileW(joinPath(d, f).c_str());
      RemoveDirectoryW(d.c_str());
    }
  }
  if (!silent)
    MessageBoxW(nullptr, gone ? L"„Контрол на срокове“ е премахнат." : L"Някои файлове са заети и не бяха премахнати.", kAppName,
                gone ? MB_ICONINFORMATION : MB_ICONWARNING);
  // This copy in the temp folder goes too, a moment after it exits.
  std::wstring self = modulePath(), cmd = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + self + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  return gone ? 0 : 4;
}

// ---------------------------------------------------------------- the window
struct Job {
  Options o;
  std::atomic<int> state{0};  // 1 running, 2 done, 3 failed
};
unsigned __stdcall worker(void* p) {
  auto* j = (Job*)p;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  bool ok = install(j->o);
  CoUninitialize();
  j->state = ok ? 2 : 3;
  return 0;
}

std::wstring pickFolder(HWND owner, const std::wstring& start) {
  ComPtr<IFileOpenDialog> d;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileOpenDialog), (void**)d.GetAddressOf()))) return {};
  DWORD f = 0;
  d->GetOptions(&f);
  d->SetOptions(f | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  d->SetTitle(L"Папка за инсталиране");
  std::wstring from = start;
  while (!from.empty() && !dirExists(from)) from = dirName(from);
  ComPtr<IShellItem> si;
  if (!from.empty() && SUCCEEDED(SHCreateItemFromParsingName(from.c_str(), nullptr, __uuidof(IShellItem), (void**)si.GetAddressOf())))
    d->SetFolder(si.Get());
  if (FAILED(d->Show(owner))) return {};
  ComPtr<IShellItem> res;
  PWSTR p = nullptr;
  std::wstring out;
  if (SUCCEEDED(d->GetResult(&res)) && SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &p))) out = p;
  CoTaskMemFree(p);
  return out;
}

class SetupWindow final : public UiWindow {
 public:
  Job job;
  std::wstring oldVersion;

  bool create(HINSTANCE inst) {
    return createWindow(inst, L"KanbanDesktopSetup", L"Контрол на срокове — инсталиране", 680, 460, 680, 460, false);
  }
  void show() {
    frame();
    ShowWindow(hwnd(), SW_SHOW);
    SetForegroundWindow(hwnd());
  }
  void preview(int step) {
    step_ = step;
    job.state = step == 2 ? 2 : 0;
    shown_ = 0.62f;
  }

 protected:
  bool onClose() override {
    if (job.state == 1) return false;
    PostQuitMessage(0);
    return true;
  }

  bool check(uint32_t id, RectF r, const wchar_t* label, const wchar_t* sub, bool& v) {
    bool c = ui.press(id, r);
    if (c) v = !v;
    bool h = ui.hot(r);
    if (h) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
    float a = ui.anim(id, v ? 1.f : 0.f, 20.f), q = ui.anim(id ^ 3u, h ? 1.f : 0.f);
    RectF b = RectF::xywh(r.l, r.t + 2, 20, 20);
    ui.p->rrect(b, 5, mix(mix(ui.pal.control, ui.pal.controlHover, q), ui.pal.accent, a));
    if (a < 0.98f) ui.p->rrectStroke(b.inset(0.5f), 5, ui.pal.faint.alpha(1 - a), 1.2f);
    if (a > 0.02f) ui.glyph(b, L"\xE73E", 12.f, ui.pal.onAccent.alpha(a));
    ui.text(RectF(r.l + 30, r.t, r.r, r.t + 24), label, 13.5f, ui.pal.text);
    if (sub) ui.text(RectF(r.l + 30, r.t + 22, r.r, r.b), sub, 11.5f, ui.pal.dim);
    return c;
  }

  // The left band: the stopwatch from the taskbar, large.
  void band(RectF r) {
    auto bg = ui.p->linear({r.l, r.t}, {r.r, r.b}, {{0, Color::hex(0x1D4ED8)}, {1, Color::hex(0x172554)}});
    ui.p->dc->FillRectangle(r.f(), bg.Get());
    V2 c{r.cx(), r.cy() - 30};
    float R = 46;
    Color w(1, 1, 1, 0.95f);
    ui.p->circleStroke(c, R, w, 7);
    ui.p->rrect(RectF(c.x - 6, c.y - R - 16, c.x + 6, c.y - R - 3), 2, w);
    ui.p->rrect(RectF(c.x - 15, c.y - R - 22, c.x + 15, c.y - R - 13), 3, w);
    float a = kTau / 6;
    ui.p->line(c, {c.x + sinf(a) * R * 0.62f, c.y - cosf(a) * R * 0.62f}, w, 7);
    ui.p->circle(c, 6, w);
    ui.text(RectF(r.l + 16, c.y + R + 26, r.r - 16, c.y + R + 54), kAppName, 16.f, Color(1, 1, 1, 0.95f), 600, Align::Center);
    ui.text(RectF(r.l + 16, c.y + R + 54, r.r - 16, c.y + R + 74), L"версия " + widen(KD_VERSION_STR), 12.f, Color(1, 1, 1, 0.7f), 400, Align::Center);
  }

  void paint() override {
    RectF v = ui.view;
    band(RectF(v.l, v.t, v.l + 220, v.b));
    RectF r(v.l + 252, v.t + 30, v.r - 32, v.b - 26);
    if (job.state == 1) step_ = 1;
    if (job.state == 2 && step_ == 1) {
      step_ = 2;
      if (job.o.launch) launch(job.o.dir);
    }
    if (job.state == 3) step_ = 3;

    if (step_ == 0) {
      ui.text(RectF(r.l, r.t, r.r, r.t + 34), g_upgrade ? L"Обновяване" : L"Инсталиране", 24.f, ui.pal.text, 600);
      std::wstring sub = g_upgrade ? L"От версия " + oldVersion + L" на " + widen(KD_VERSION_STR) + L". Настройките и сесията остават."
                                   : L"Хронометърът и сроковете — на лентата на задачите.";
      ui.text(RectF(r.l, r.t + 36, r.r, r.t + 58), sub, 12.5f, ui.pal.dim);
      float y = r.t + 78;
      ui.text(RectF(r.l, y, r.r, y + 18), L"Папка за инсталиране", 12.f, ui.pal.dim, 600);
      RectF box(r.l, y + 22, r.r - 104, y + 56);
      ui.textBox(ui.id("dir"), box, job.o.dir);
      if (ui.button(ui.id("browse"), RectF(r.r - 96, box.t, r.r, box.b), L"Промени…")) {
        after([this] {
          std::wstring d = pickFolder(hwnd(), job.o.dir);
          if (!d.empty()) job.o.dir = toLower(fileName(d)) == toLower(kFolderName) ? d : joinPath(d, kFolderName);
        });
      }
      ui.text(RectF(r.l, box.b + 4, r.r, box.b + 22), L"В тази папка е и Uninstall.exe — за премахване.", 11.5f,
              ui.pal.faint);
      y = box.b + 34;
      check(ui.id("auto"), RectF(r.l, y, r.r, y + 44), L"Стартирай с Windows", L"Хронометърът е на лентата веднага след влизане в Windows.",
            job.o.autostart);
      y += 50;
      check(ui.id("desk"), RectF(r.l, y, r.r, y + 26), L"Икона на работния плот", nullptr, job.o.desktop);
      y += 32;
      check(ui.id("run"), RectF(r.l, y, r.r, y + 26), L"Отвори след инсталиране", nullptr, job.o.launch);
      ui.text(RectF(r.l, r.b - 34, r.r - 160, r.b), L"Само за вас — не са нужни администраторски права.", 11.5f, ui.pal.faint, 400, Align::Left,
              VAlign::Middle, true);
      bool ok = trim(job.o.dir).size() > 3;
      if (ui.button(ui.id("go"), RectF(r.r - 150, r.b - 38, r.r, r.b), g_upgrade ? L"Обнови" : L"Инсталирай", Btn::Accent, nullptr, ok)) {
        job.o.dir = trim(job.o.dir);
        while (job.o.dir.size() > 3 && (job.o.dir.back() == L'\\' || job.o.dir.back() == L'/')) job.o.dir.pop_back();
        job.state = 1;
        uintptr_t t = _beginthreadex(nullptr, 0, worker, &job, 0, nullptr);
        if (t) CloseHandle((HANDLE)t);
        else job.state = 3;
      }
    } else if (step_ == 1) {
      ui.text(RectF(r.l, r.t + 120, r.r, r.t + 156), g_upgrade ? L"Обновява се" : L"Инсталира се", 22.f, ui.pal.text, 600);
      shown_ += (g_progress / 100.f - shown_) * (1.f - expf(-10.f * ui.dt));
      RectF bar(r.l, r.t + 172, r.r, r.t + 178);
      ui.p->rrect(bar, 3, ui.pal.text.withA(0.12f));
      ui.p->rrect(RectF(bar.l, bar.t, lerp(bar.l, bar.r, shown_), bar.b), 3, ui.pal.accent);
      ui.animating = true;
    } else if (step_ == 2) {
      float a = ui.anim(ui.id("donein"), 1.f, 7.f);
      V2 c{r.l + 28, r.t + 96};
      ui.p->circle(c, 26 * lerp(0.6f, 1.f, a), Color::hex(0x1FA463).withA(0.16f));
      ui.glyph(RectF(c.x - 26, c.y - 26, c.x + 26, c.y + 26), L"\xE73E", 24, Color::hex(0x1FA463));
      ui.text(RectF(r.l, r.t + 136, r.r, r.t + 172), g_upgrade ? L"Обновено" : L"Готово", 26.f, ui.pal.text, 600);
      ui.text(RectF(r.l, r.t + 176, r.r, r.t + 260),
              L"Хронометърът е на лентата на задачите — на мястото на времето. Влезте със същите адрес, потребител и парола като в браузъра.",
              13.5f, ui.pal.dim, 400, Align::Left, VAlign::Top, true);
      if (ui.button(ui.id("close"), RectF(r.r - 150, r.b - 38, r.r, r.b), L"Затвори", Btn::Accent)) PostMessageW(hwnd(), WM_CLOSE, 0, 0);
    } else {
      ui.glyph(RectF(r.l, r.t + 76, r.l + 44, r.t + 120), L"\xE7BA", 30, ui.pal.danger);
      ui.text(RectF(r.l, r.t + 130, r.r, r.t + 166), L"Не завърши", 22.f, ui.pal.text, 600);
      ui.text(RectF(r.l, r.t + 170, r.r, r.t + 250), g_error, 13.5f, ui.pal.dim, 400, Align::Left, VAlign::Top, true);
      if (ui.button(ui.id("again"), RectF(r.r - 150, r.b - 38, r.r, r.b), L"Опитай пак", Btn::Accent)) {
        job.state = 0;
        step_ = 0;
        g_progress = 0;
      }
    }
  }

 private:
  int step_ = 0;
  float shown_ = 0;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
  OleInitialize(nullptr);
  int n = 0;
  LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &n);
  std::vector<std::wstring> args;
  for (int i = 1; av && i < n; i++) args.push_back(av[i]);
  LocalFree(av);
  auto has = [&](const wchar_t* s) { return std::find(args.begin(), args.end(), s) != args.end(); };
  auto val = [&](const wchar_t* s) {
    for (size_t i = 0; i + 1 < args.size(); i++)
      if (args[i] == s) return args[i + 1];
    return std::wstring();
  };

  int rc = 0;
  bool asUninstaller = toLower(fileName(modulePath())) == toLower(kUninstallExe);
  if (!val(L"--uninstall-run").empty()) {
    rc = uninstallStage2(val(L"--uninstall-run"), (DWORD)wcstoul(val(L"--after").c_str(), nullptr, 10), has(L"--silent"), has(L"--purge"));
  } else if (has(L"--uninstall") || asUninstaller) {
    rc = uninstallStage1(has(L"--silent"), has(L"--purge"));
  } else {
    auto winPtr = std::make_unique<SetupWindow>();
    SetupWindow& win = *winPtr;
    Options& o = win.job.o;
    o.dir = installedDir();
    g_upgrade = !o.dir.empty();
    if (g_upgrade) {
      // An update keeps what the person chose last time.
      win.oldVersion = installedVersion();
      o.autostart = autostartOn();
      o.desktop = shortcutPointsInto(desktopLink(), o.dir);
    } else {
      o.dir = defaultInstallDir();
    }
    if (!val(L"--dir").empty()) o.dir = val(L"--dir");
    if (has(L"--no-autostart")) o.autostart = false;
    if (has(L"--no-desktop")) o.desktop = false;
    if (has(L"--desktop")) o.desktop = true;
    if (has(L"--no-launch")) o.launch = false;

    if (has(L"--silent") || has(L"--update") || has(L"/S")) {
      rc = install(o) ? 0 : 1;
      if (rc == 0 && o.launch) launch(o.dir);
    } else if (!val(L"--shot").empty()) {  // a headless picture of one page, for checks
      if (gfx().ensure()) {
        auto t = gfx().target(680, 460);
        win.preview((int)wcstol(val(L"--step").c_str(), nullptr, 10));
        rc = win.renderTo(t.Get(), 680, 460, 60, has(L"--dark")) && savePng(t.Get(), val(L"--shot")) ? 0 : 1;
      }
    } else if (win.create(inst)) {
      win.show();
      MSG m;
      bool run = true;
      while (run) {
        HANDLE h = win.frameHandle();
        bool want = win.wantsFrame();
        DWORD r = MsgWaitForMultipleObjectsEx(want && h ? 1 : 0, &h, want && !h ? 8 : INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
          if (m.message == WM_QUIT) {
            run = false;
            break;
          }
          TranslateMessage(&m);
          DispatchMessageW(&m);
        }
        if (run && win.wantsFrame() && (r == WAIT_TIMEOUT || (want && h && r == WAIT_OBJECT_0))) win.frame();
      }
    }
    winPtr.reset();
  }
  gfx().shutdown();
  OleUninitialize();
  return rc;
}
