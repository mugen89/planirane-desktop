// The installed copy: where it lives, its shortcuts, Start with Windows, its Settings › Apps entry, and the
// way a setup asks a running copy to close. Shared by the app and by the setup / uninstall program.
// Per user only: the user's own folders and HKEY_CURRENT_USER, never administrator rights.
#pragma once
#include "common.h"

namespace kd {

constexpr const wchar_t* kAppName = L"Контрол на срокове";
constexpr const wchar_t* kExeName = L"KanbanDesktop.exe";
constexpr const wchar_t* kUninstallExe = L"Uninstall.exe";  // the setup program again, inside the install folder
constexpr const wchar_t* kFolderName = L"Kanban Desktop";
constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KanbanDesktop";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"KanbanDesktop";
constexpr const wchar_t* kAumid = L"KanbanDesktop";
constexpr const wchar_t* kHiddenClass = L"KanbanDesktopHidden";  // the resident window: commands arrive here
constexpr ULONG_PTR kCopyCommand = 0x4B44;                         // "KD": the WM_COPYDATA tag of a command
constexpr const wchar_t* kRepoUrl = L"https://github.com/mugen89/planirane-desktop";

std::wstring knownFolder(REFKNOWNFOLDERID id);
std::wstring defaultInstallDir();  // %LOCALAPPDATA%\Programs\Kanban Desktop
std::wstring installedDir();       // from the Settings › Apps entry; empty when not installed
std::wstring installedVersion();
bool isInstalledCopy();            // this very exe is the installed one (only that one updates itself)

std::wstring startMenuLink();
std::wstring desktopLink();
bool createShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& desc);
bool shortcutPointsInto(const std::wstring& lnk, const std::wstring& dir);  // ours, i.e. safe to delete

bool autostartOn();
void setAutostart(bool on, const std::wstring& exe);

// A copy with its own settings folder (KANBANDESKTOP_DATA, used by tests) is a separate instance: its own
// single-instance lock and resident window, so neither it nor a setup started beside it touches the real one.
std::wstring instanceSuffix();
std::wstring hiddenWindowClass();
bool sendCommand(const std::wstring& cmd);  // to the running copy; false when none answers
bool quitRunningApp(DWORD waitMs);          // true when no copy is running any more

}  // namespace kd
