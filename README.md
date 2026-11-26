# Kanban Desktop

The Windows companion of the kanban web app („Контрол на срокове“). It puts the stopwatch on the taskbar, in
the place of Windows' weather/news button, with a panel above it — so starting, pausing and stopping work time,
and deciding ✓ / ✕ on it, is one click away instead of a browser tab.

- Talks to the same server as the browser, through its API, with the same username and password. Nothing is
  stored on the PC except the address, the username and an encrypted session (never the password).
- One small exe, no runtime to install. Idle: 0% CPU, a few MB of memory.
- UI in Bulgarian.

See [docs/PLAN.md](docs/PLAN.md) for the decisions behind it and what comes next, and
[CHANGELOG.md](CHANGELOG.md) for what each version brought.

## Install

Download `KanbanDesktop-Setup.exe` from the [latest release](https://github.com/mugen89/planirane-desktop/releases/latest)
and run it. It installs for the current Windows user only (no administrator rights) and asks for:

- **the folder** (default `%LOCALAPPDATA%\Programs\Kanban Desktop`);
- **Start with Windows** (on by default);
- **a desktop icon**.

It also adds a Start menu entry. To remove it: `Uninstall.exe` in the install folder, or Settings › Apps ›
„Контрол на срокове“. Your work is on the server; the uninstaller can also delete this PC's settings (server
address, username, session).

The installer runs unattended too: `KanbanDesktop-Setup.exe --silent [--dir <folder>] [--no-autostart]
[--desktop | --no-desktop] [--no-launch]`; `Uninstall.exe --silent [--purge]`.

## Updates

The installed copy checks the latest GitHub Release a minute after it starts and then every six hours (tray
menu › „Провери за обновление“ asks at once). A newer version is downloaded, checked against the `.sha256`
published with it, and installed quietly; the program restarts within seconds and nothing is lost — the
stopwatch lives on the server. It waits while the panel or the sign-in window is open, and tries one version at
most once a day.

## Releasing a new version

1. Add a `## X.Y.Z` section to `CHANGELOG.md` (it becomes the release notes).
2. `.\release.ps1 X.Y.Z` — sets the version in `CMakeLists.txt`, builds once locally, commits, tags `vX.Y.Z` and
   pushes.
3. GitHub Actions (`.github/workflows/release.yml`) builds the setup with the same compiler and publishes the
   release with the setup and its `.sha256`.

## Use

| | |
|---|---|
| Click the stopwatch on the taskbar | The panel: start, pause, resume, stop, cancel; stopped time waits for ✓ / ✕ |
| Right-click it, or the tray icon | The menu: the same actions, open in the browser, sign out, close |
| First start | The sign-in window: server address (e.g. `http://server:8106`), username, password |

A stopwatch started here has no card; choose the card in the browser (at stop or later). The server allows
only a few such entries at a time (Админ setting „максимум неразпределени“).

Command line (talks to the copy already running): `KanbanDesktop.exe --panel | --hide | --start | --pause |
--resume | --stop | --cancel | --refresh | --update | --signin | --signout | --quit`.

Settings live in `%APPDATA%\KanbanDesktop` (`config.json`, `session.dat`). `KANBANDESKTOP_DATA` points a copy at
another folder and makes it a separate instance (for tests); `KANBANDESKTOP_LOG` writes a log;
`KANBANDESKTOP_UPDATE_BASE` points the updater at a stand-in for GitHub.

## Build

Needs MinGW-w64 GCC (WinLibs UCRT), CMake and Ninja on PATH, and Python with Pillow for the icon:

```powershell
.\build.ps1
```

The program, the setup and the setup's `.sha256` land in `dist\`. The API test (against a **development** server only):

```powershell
cmake -S . -B build -DKD_TESTS=ON; cmake --build build
$env:KD_TEST_SERVER = "http://localhost:8106"; $env:KD_TEST_USER = "…"; $env:KD_TEST_PASSWORD = "…"; .\build\test_api.exe
```

## Source map

| File | What it holds |
|---|---|
| `src/app.*` | The resident process: hidden window, tray, sign-in flow, polling, every action, the command line |
| `src/api.*` | The server's API: sign-in, session, time state and actions; the clock arithmetic (as the web's `elapsedOfSession`) |
| `src/net.*` | WinHTTP on a worker thread; answers come back on the UI thread |
| `src/store.*` | `config.json` and the DPAPI-encrypted session |
| `src/taskbar.*` | Where the taskbar, the tray, the weather button and the app buttons are (window rectangles + UI Automation) |
| `src/pill.*` | The taskbar button (software Direct2D into a layered window) |
| `src/views.*` | The panel and the sign-in window |
| `src/install.*` | The installed copy: folder, shortcuts, Start with Windows, the Settings › Apps entry, closing a running copy |
| `src/update.*` | Updates from GitHub Releases |
| `setup/setup_main.cpp` | The installer (window and `--silent`) and, as `Uninstall.exe`, the uninstaller |
| `src/common.* json.* gfx.* ui.*` | From Radial Taskbar: Windows helpers, JSON, Direct2D, the immediate-mode controls (plus a password box and popup windows) |
