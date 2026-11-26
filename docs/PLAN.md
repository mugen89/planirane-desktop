# Kanban Desktop — plan

Started 2026-11-26. A Windows companion to the kanban web app („Контрол на срокове“), because people find
the browser inconvenient for the many small things they do all day: is my stopwatch running, start it, stop it,
what is due. The web app stays the full application; the desktop puts the frequent part one click (or one
glance) away. Local only, like the web app.

## Decisions (and why)

| # | Decision | Why |
|---|---|---|
| 1 | **The desktop talks to the server's REST API, never to PostgreSQL.** Same server address, username and password as the browser. | Goal one is that desktop and web stay consistent: every rule (permissions, the вх.№ gate, ✓/✕ time confirmation, the cap on time without a card, the audit logs) lives in the server, so going through it gives consistency by construction. A direct DB client would have to re-implement and track every rule, put the DB password on every PC (full access to HR files and password hashes), break on every schema change, and production's Postgres is bound to 127.0.0.1 anyway. It is also the lighter option for the PC: small JSON over WinHTTP, no DB driver. |
| 2 | **Native C++20, built on Radial Taskbar's foundation** (`D:\01_Organized\C Plus - Radial Task bar`): Direct2D, the immediate-mode UI, the DirectComposition overlay, the Win-key takeover hook, tray/single-instance patterns, the installer. | The person's own proven stack and toolchain (MinGW-w64/WinLibs, CMake, Ninja); one small exe, no runtime. |
| 3 | **The taskbar button covers Windows' weather/news (Widgets) button exactly.** | Windows 11 has no API for third-party taskbar content (desk bands are gone), and a window laid over the taskbar is not known to the taskbar's layout. The Widgets slot is the one space Windows keeps free; covering it replaces the weather without taking extra room. Hiding Widgets is not possible for a program anyway: the UCPD driver refuses `TaskbarDa` writes to every ordinary process (verified: PowerShell and Python both get *access denied*), and once hidden the app buttons flow into the space. Fallback when Widgets is off: the gap left of the tray, smaller or hidden when the app buttons fill it (the tray icon keeps the time in its tooltip). |
| 4 | **Updates straight from GitHub Releases.** | The person's choice. Consequence: the desktop repository must be public (it holds no secrets or data: the server address is typed by the user) or every PC needs a token. |
| 5 | **Time without a card is started from the desktop only.** The browser keeps requiring a card at start. | Asked for („restore the unassigned times but only for the desktop“). No server change was needed: `POST /time/start` without `cardId` was never removed, only the web stopped offering it. The server still enforces `maxUnassignedTime` (3), refuses a start while time waits for ✓/✕, and the web shows such sessions as „⌖ без карта“ to attach a card. |
| 6 | **Priority: the taskbar part.** The Win+Q overlay is an extra. | The person's priority. |

## What exists (v0.3.0)

- Sign-in (server, username, password) → a 90-day sliding session, stored encrypted for the Windows user (DPAPI); the password is never stored.
- The taskbar button: a stopwatch whose hand moves with the seconds and two lines (running time · what it is for, or „Днес h:mm · Не тече“). Software-rendered layered window: no GPU device while idle.
- The panel above it (acrylic flyout, almost as tall as the screen): the stopwatch strip (Пусни / Пауза / Продължи / Спри / Откажи, the stopped sessions waiting for ✓ / ✕, today's total and „за разпределяне“), the views of the unified board („Изп.“ / „УС“, remembered on the server like the web's), its summary table (a chosen row collapses the table; its cards take the room), and per card: start / attach the stopwatch, the lane menu (the web's move). Footer: „Провери за обновление“ by the version.
- Tray icon + menu, single instance, command line (`--panel --hide --start --pause --resume --stop --cancel --refresh --signin --signout --quit`).
- Polls `GET /time/state` every 30 s (the browser's rhythm), refreshes at once after an action and on unlocking the screen; hides during full-screen apps and on the lock screen.
- Verified on screen (2026-11-26): the button covers the weather slot exactly; idle → running → idle redraws at once.
- Measured on the development PC: 0 ms CPU over 10 s idle (docking and UI Automation active); 4–8 MB private memory idle, ~18 MB while the panel is open (given back 20 s after it closes).

## Phases

1. ✅ **Foundation + the stopwatch on the taskbar** (this slice).
2. ✅ **Installable + updates** (0.1.0) — per-user installer: folder of choice, Start with Windows, desktop icon, Start menu, `Uninstall.exe` in the folder and the Settings › Apps entry; self-update from GitHub Releases with a SHA-256 check; `release.ps1` + a GitHub Actions workflow that builds and publishes. Tested: install → update 0.1.0 → 0.1.1 through a stand-in for GitHub → uninstall leaves nothing behind.
3. ✅ **The collector summary in the panel** (0.2.0) — the summary table of the unified board for the person's subview, the same numbers as the browser: the computation moved into `server/src/shared/summary.ts` of the web app, which both the browser and `GET /desktop/summary` use (verified: the admin's table identical in the browser before/after the move and in the endpoint). A row or a number lists its cards; a card starts the stopwatch, or takes the running one. **0.3.0** (the user's review of 0.2.0): the views above the table, remembered on the server like the web's (`PATCH /me/view`); a chosen row collapses the table to itself; a card's lane menu (the web's own `POST /collector/cards/:id/move`, so all its rules) with „№ вх. док.“ typed under the card when closing needs it; the panel almost as tall as the screen. Tested on a scratch copy of the database (moves, closing with a number, the time rule, a view change). Still to come here: the client complexity/independence columns.
4. **Choose the card** — partly done in 0.2.0 (a running stopwatch is attached to a card from the table); still to come: attaching a stopped / „за разпределяне“ entry.
5. **Win+Q overlay** — dim the screen and show the summary table full size (Radial's overlay + hook; Win+Q is Windows Search's shortcut, so the hook takes it over, which Radial already supports).

## Server endpoints the desktop relies on

`POST /api/auth/login` · `GET /api/auth/session` · `POST /api/auth/logout` · `GET /api/time/state` ·
`POST /api/time/start | pause | resume | stop` (`start` with or without `cardId`) · `PATCH /api/time/running` (`cardId`) ·
`DELETE /api/time/running` · `POST /api/time/entries/:id/confirm | discard` · `GET /api/desktop/summary?year=&month=&subview=`
(each card with its `laneKey`) · `POST /api/collector/cards/:id/move` (`lane`, `referenceId`) · `PATCH /api/me/view`
(`boardSubview`). Listed in the web app's `docs/API.md` too: changing their
shape breaks installed desktops.

## Risks and open points

- **Floating over the taskbar is not an official integration.** It is re-docked every second (cheap) and on Explorer restarts; a taskbar redesign by Microsoft can move things. The slot is found by UI Automation (`WidgetsButton`), every 4 s.
- **HTTP without TLS on the LAN**: the password crosses the network in clear at sign-in — the same as the browser today (HTTPS is an open decision for the server).
- **Unsigned exe**: SmartScreen warns on first run of a downloaded setup; Microsoft Defender holds a new unsigned build for a cloud check (seen: ~10 s before an updated copy started); antivirus heuristics may dislike the keyboard hook of phase 5. A code-signing certificate fixes all three.
- **The session cookie name follows the server's port** (`kb_session_8106`); the desktop takes whichever `kb_session_*` it receives.
- **GitHub Releases** need a public repository or a token on every PC (decision 4).

## Testing

- `tests/test_api.cpp` (build with `-DKD_TESTS=ON`) signs in to a development server, runs the stopwatch through start / pause / resume / cancel / stop → ✓✕ pending → discard, and signs out; it leaves no time behind (only the database trigger's audit rows in `time_entry_log`, which the test session removed by id).
- The app is driven with its command line and an isolated data folder (`KANBANDESKTOP_DATA`), **never with synthetic keyboard/mouse input** on a PC someone is using: the first try typed into the person's live session.
