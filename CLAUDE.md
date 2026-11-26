# CLAUDE.md

Guidance for Claude Code in this repository: **Kanban Desktop**, the Windows companion of the kanban web app
(„Контрол на срокове“), whose active tree is `D:\Open Projects\07 Контрол на Сроковете\kanban-app` (dev server
:8106, its own CLAUDE.md). Read `docs/PLAN.md` first: the decisions there are settled with the user.

## Rules that are not obvious from the code

- **API only — never the database.** Every read and write goes through the server's REST API with the user's
  own session. Do not add a Postgres client, and do not re-implement a server rule here: show the server's
  error message instead. If the desktop needs data in a new shape, add an endpoint to the server (in the
  kanban-app tree, with its CHANGELOG) rather than computing it twice.
- **The endpoints the desktop uses are listed in kanban-app `docs/API.md` („Настолно приложение“).** Keep
  that list current when you use a new one.
- **Card moves are the web's own move** (`POST /collector/cards/:id/move`). The server's refusal (`422`, `error` +
  `message`) decides what the card shows: `reference_required` → a „№ вх. док.“ field under it; anything else → the
  message and „Отвори в браузъра“. Never pre-check a rule here.
- **The rail never activates.** The panel hides when it loses activation (a click elsewhere); the rail beside it is
  `WS_EX_NOACTIVATE` and answers `WM_MOUSEACTIVATE` with `MA_NOACTIVATE`. Anything clickable next to the panel must do
  the same, or a click on it closes the panel.
- **„За разпределяне“ lives here.** The browser has had no screen for attaching time without a card since its time pane
  went (2026-09-08); a stopwatch started here without a card can only get its card here. Keep it working.
- **Test writes on a scratch copy, not on :8106.** Moves write card history and close cards; a view change writes the
  person's own preference (on :8106 that is the user's admin account). Copy `kanban_merge` to a scratch database, run
  the `kanban-app:merge` image on :8199 against it, point a test copy (`KANBANDESKTOP_DATA`) there, drop it all after.
- **Light on the PC.** The user's explicit requirement. No timers faster than 1 s while idle, no GPU device
  for the taskbar button (it is software-rendered on purpose), windows give their device back 20 s after
  closing. Measure after changes (Task Manager / `Get-Process`): idle was 0 ms CPU per 10 s, ~4–8 MB private.
- **UI text is Bulgarian; identifiers and comments English.** „Бизнес линия“, not „отдел“, in anything a
  user reads.
- **The summary table is computed by the server** (`GET /desktop/summary`) with `server/src/shared/summary.ts`, the same
  module the browser's table uses (web CHANGELOG 331). Never recompute it here; the response's `layout` says which columns.
  Compare with the browser after a change (Chrome: the unified board's table vs. the endpoint, row by row).
- **The taskbar button covers Windows' Widgets (weather/news) slot** — on purpose (see PLAN.md, decision 3).
  Do not try to switch Widgets off programmatically: UCPD refuses it to every ordinary process.
- **Testing on the user's PC: never synthetic keyboard or mouse input.** The first test session typed into
  the user's live desktop (the sign-in window took their keystrokes). Drive the app with its command line
  (`--start`, `--panel`, `--quit` …), isolate it with `KANBANDESKTOP_DATA`, take screenshots (read-only), and
  run `tests/test_api.cpp` for the API. A sign-in for tests = a session cookie from the API, DPAPI-protected
  (entropy `KanbanDesktop.session.v1`, CurrentUser) into `session.dat`. Clean up: the test's time entries,
  their `time_entry_log` rows (a trigger writes them) and the test session.
- **The dev server is :8106 with real-looking data.** Record ids/counts before a test, remove only what the
  test created.
- **Shared modules come from Radial Taskbar** (`common`, `json`, `gfx`, `ui`). Two deliberate changes here:
  `Ui::passwordBox` and `UiWindow::createPopup` (+ `onDeactivate`). Keep other edits small so fixes can be
  carried between the two projects.

## Build

```powershell
.\build.ps1                     # Release into dist\ (MinGW-w64 GCC, CMake, Ninja on PATH)
cmake -S . -B build -DKD_TESTS=ON; cmake --build build    # also build\test_api.exe
```

A running copy locks `dist\KanbanDesktop.exe` (`build.ps1` then only warns); `dist\KanbanDesktop.exe --quit` closes it.

## Releasing and the repository

Public repository `https://github.com/mugen89/planirane-desktop` (desktop only — the web app is elsewhere).
Releases: `.\release.ps1 X.Y.Z` after adding `## X.Y.Z` to CHANGELOG.md; a `vX.Y.Z` tag makes GitHub Actions
build and publish the setup + `.sha256` (`.github/workflows/release.yml`). Installed copies update themselves
(`src/update.cpp`); a broken release reaches every PC within six hours — build and try it locally first.

- **The user may be running a copy from `dist\` or an installed one.** Check processes before testing; never
  kill it. A test copy started with `KANBANDESKTOP_DATA` is a separate instance (own lock + window class), and a
  setup / Uninstall.exe started with the same variable only closes that instance.
- **Update test without publishing anything:** build a higher version in a temporary copy of the tree, serve its
  `dist` with a stand-in for `/releases/latest` (302 to `/releases/tag/vX`) and `/releases/download/vX/…`, point
  the installed test copy at it with `KANBANDESKTOP_UPDATE_BASE`, then `KanbanDesktop.exe --update`.
- The installer writes per-user state shared with a real installation (Settings › Apps key, Start menu and
  desktop shortcuts, the Run value): uninstall test installs afterwards and check those four are gone.

Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
