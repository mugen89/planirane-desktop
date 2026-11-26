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
| The rail left of the panel | Tabs: Таблица · Админ. дейности · Клиенти · Календар · Одитен график · Профил (Админ. дейности only for people who track time; Одитен график only when the person has audit cards — admins and audit staff). It never takes the keyboard, so a click on it leaves the panel open |
| **Win+Q** | The board over a dimmed screen: the panel large in the middle of the monitor, the rail beside it. Win+Q again, Esc or a click on the dimmed part closes it. Switched in Профил → „Win+Q — таблицата на цял екран“ (on); while on, Win+Q no longer opens Windows Search (Win+S does) |
| Click the stopwatch on the taskbar | The panel: the stopwatch (start, pause, resume, stop, cancel; stopped time waits for ✓ / ✕) and the unified board's summary table |
| The views above the table | The same views as in the browser („Изп.“ / „УС“, as your role allows). The choice is remembered on the server for you, so the browser opens on it too |
| Click a row or a number in the table | The table collapses to that row and its cards take the room (the name again, or ✕, brings the table back); „Пусни“ starts the stopwatch on a card, „Закачи“ moves a running stopwatch onto it |
| „Провери за обновление“ (bottom of the panel, by the version) | Checks GitHub and installs a newer version at once; the app closes and comes back by itself. Blue — „Обнови до X“ — when the regular check has found one |
| „За разпределяне“ (under the stopwatch) | Time recorded without a card: „Избери карта“ by an entry, then a row or number in the table and „Сложи тук“ on a card. A choice the card needs (its activity's options, or „Дейност“ + „Коментар“ on a project task) appears under it. The browser has no such screen |
| ✓ on stopped time whose card needs a choice | The choice (or „Дейност“ + „Коментар“) appears under the stopwatch; „Запиши“ sends it |
| A card's lane menu | Moves it — Предстоящи, В процес, За одобрение, Приключени — with the browser's rules; when closing needs a „№ вх. док.“, it is typed under the card |
| Right-click it, or the tray icon | The menu: the same actions, open in the browser, sign out, close |
| First start | The sign-in window: server address (e.g. `http://server:8106`), username, password |

The stopwatch starts on a card only („Пусни“ in the table, the calendar or a client's cards) — starting without one was
removed in 0.5.0 at the user's request. Entries recorded earlier without a card are still attached from „За разпределяне“.

Command line (talks to the copy already running): `KanbanDesktop.exe --panel | --overlay | --hide | --pause |
--resume | --stop | --cancel | --refresh | --update | --signin | --signout | --quit`; for tests, what a click does:
`--select:<row>[:<column>]` (the table), `--view:<id>` (a view above it), `--arm:<entry>` / `--attach:<entry>:<card>[:<option ids>]` („Избери карта“ / „Сложи тук“), `--confirm:<entry>[:<option ids>]` (✓), `--tab:table|day|clients|calendar|audit|profile`, `--calmonth:<YYYY-MM>`, `--client:<id>` (a client's nomenclature, loaded), `--daydate:<YYYY-MM-DD>`, `--dayset:<activity>:<minutes>[:<comment>]` (one row of „Админ. дейности“, saved), `--dayline:<activity>:<colleague|0>:<client|0|na>:<minutes>[:<comment>]` (a line activity's lines = this line), `--dayabs:<activity>:<minutes>:<from>:<to>[:<substitute>]` (an absence over a period), `--railshot:<file.png>` (the rail off screen), `--move:<card>:<lane>[:<№ вх. док.>]` (a card's
lane menu), and `--shot:<file.png>[|<row>[|<column>]]` (the panel drawn off screen at its real size).

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
