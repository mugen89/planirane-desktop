#include "app.h"

namespace kd {
namespace {
void putAnswer(rt::Json& b, const AttachNeeds* answer);
}
}  // namespace kd

#include <algorithm>

#include "gfx.h"
#include "install.h"
#include "taskbar.h"
#include "update.h"
#include "views.h"

#include <wtsapi32.h>

namespace kd {
using namespace rt;

namespace {
App* g_app = nullptr;
constexpr const wchar_t* kTitle = kAppName;
constexpr UINT_PTR kTick = 1;
constexpr double kPollEvery = 30.0;   // the browser's rhythm (web/src/timeState.ts, POLL_MS)
constexpr double kReleaseAfter = 20.0;  // windows closed this long: give their graphics memory back

enum MenuId : UINT {
  kMenuPanel = 1, kMenuStart, kMenuPause, kMenuResume, kMenuStop, kMenuRefresh, kMenuBrowser,
  kMenuPill, kMenuSignIn, kMenuSignOut, kMenuQuit, kMenuUpdate,
};

// Runs a message loop that also paces the frame-latency waitable swap chains of open windows.
template <class Frames>
void pump(Frames&& frames) {
  MSG msg;
  for (;;) {
    HANDLE waits[4];
    DWORD n = 0, timeout = INFINITE;
    frames.collect(waits, n, timeout);
    DWORD r = MsgWaitForMultipleObjectsEx(n, waits, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) return;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (r == WAIT_TIMEOUT || r < WAIT_OBJECT_0 + n) frames.draw();
  }
}

std::wstring target(const std::wstring& card, const std::wstring& client) {
  if (!card.empty()) return card;
  if (!client.empty()) return client;
  return L"без карта";
}
}  // namespace

App& app() { return *g_app; }
App::App() { g_app = this; }
App::~App() { g_app = nullptr; }

// ---------------------------------------------------------------- lifetime
bool App::makeWindow() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = proc;
  wc.hInstance = inst_;
  std::wstring cls = hiddenWindowClass();
  wc.lpszClassName = cls.c_str();
  wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(1));
  RegisterClassExW(&wc);
  // A real (never shown) top-level window, so it hears TaskbarCreated and settings broadcasts.
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, cls.c_str(), kTitle, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst_, this);
  return hwnd_ != nullptr;
}

int App::run(HINSTANCE inst) {
  inst_ = inst;
  // "KanbanDesktop.exe --stop" and friends: run by the copy that is already there.
  std::wstring startCmd;
  {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++)
      if (!wcsncmp(argv[i], L"--", 2)) startCmd = argv[i] + 2;
    if (argv) LocalFree(argv);
  }
  HANDLE mutex = CreateMutexW(nullptr, TRUE, (L"Local\\KanbanDesktop.Instance" + instanceSuffix()).c_str());
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(hiddenWindowClass().c_str(), nullptr)) {
      DWORD pid = 0;
      GetWindowThreadProcessId(other, &pid);
      AllowSetForegroundWindow(pid);
      std::wstring cmd = startCmd.empty() ? L"panel" : startCmd;
      COPYDATASTRUCT cd{kCopyCommand, (DWORD)((cmd.size() + 1) * sizeof(wchar_t)), (void*)cmd.c_str()};
      DWORD_PTR res = 0;
      SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 3000, &res);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
  }
  if (startCmd == L"quit") {  // nothing running: nothing to close
    if (mutex) CloseHandle(mutex);
    return 0;
  }
  SetCurrentProcessExplicitAppUserModelID(kAumid);
  cfg.load();
  api.base = normalizeServer(cfg.server);
  if (!makeWindow()) return 1;
  netStart(hwnd_, WM_APP_NET);
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);  // back from a locked screen: refresh at once
  updateTray();

  pill_.onClick = [this] {
    if (signedIn) togglePanel();
    else showLogin();
  };
  pill_.onMenu = [this](POINT p) { trayMenu(p); };
  pill_.create(inst_);
  updatePill();
  pill_.setEnabled(cfg.pill);
  fgHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, onForeground, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  SetTimer(hwnd_, kTick, 1000, nullptr);
  nextUpdateCheck_ = nowSec() + 60;  // a minute after start, then every 6 hours

  std::string cookie;
  if (!api.base.empty() && loadSession(cookie)) {
    api.cookie = cookie;
    signedIn = true;  // until the server says otherwise; the clock keeps working while it is unreachable
    api.session([this](const Reply& r, const User& u) {
      if (r.ok() && u.id) {
        user = u;
        refresh();
      } else if (r.status == 0) {
        offline = true;
        updatePill();
      } else {
        sessionLost();
      }
    });
  } else {
    showLogin(false);
  }
  if (!startCmd.empty()) PostMessageW(hwnd_, WM_APP_COMMAND, 0, (LPARAM) new std::wstring(startCmd));

  struct Frames {
    App& a;
    void collect(HANDLE* w, DWORD& n, DWORD& t) {
      for (UiWindow* win : {(UiWindow*)a.panel_.get(), (UiWindow*)a.login_.get()}) {
        if (!win || !win->wantsFrame()) continue;
        if (win->frameHandle()) w[n++] = win->frameHandle();
        else t = 8;
      }
    }
    void draw() {
      for (UiWindow* win : {(UiWindow*)a.panel_.get(), (UiWindow*)a.login_.get()})
        if (win && win->wantsFrame()) win->frame();
    }
  };
  pump(Frames{*this});

  KillTimer(hwnd_, kTick);
  WTSUnRegisterSessionNotification(hwnd_);
  if (fgHook_) UnhookWinEvent(fgHook_);
  netStop();
  panel_.reset();
  login_.reset();
  pill_.destroy();
  if (trayOn_) {
    NOTIFYICONDATAW ni{};
    ni.cbSize = sizeof ni;
    ni.hWnd = hwnd_;
    ni.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &ni);
  }
  gfx().release();
  DestroyWindow(hwnd_);
  if (mutex) CloseHandle(mutex);
  return 0;
}

void App::quit() { PostQuitMessage(0); }

// ---------------------------------------------------------------- updates
void App::checkForUpdate(bool manual) {
  if (updateBusy) return;
  if (manual) {
    updateNote = L"Проверява се за обновление…";
    if (panel_) panel_->invalidate();
  }
  checkLatestRelease([this, manual](const std::string& latest, const std::wstring& err) {
    if (!err.empty()) {
      if (manual) {
        updateNote = err;
        updateToast();
      }
      if (panel_) panel_->invalidate();
      return;
    }
    if (latest.empty() || compareVersions(latest, KD_VERSION_STR) <= 0) {
      updateVersion.clear();
      if (manual) {
        updateNote = L"Това е последната версия (" + widen(KD_VERSION_STR) + L").";
        updateToast();
      }
      if (panel_) panel_->invalidate();
      return;
    }
    updateVersion = latest;
    if (!isInstalledCopy()) {
      // A development build or a copy run from elsewhere: say so, never replace the installed one from here.
      updateNote = L"Има версия " + widen(latest) + L" — инсталирайте я от GitHub (това копие не е инсталираното).";
      if (manual) updateToast();
      if (panel_) panel_->invalidate();
      return;
    }
    bool triedToday = cfg.updateTried == latest && unixNow() - cfg.updateTriedAt < 24 * 3600;
    bool someoneHere = (panel_ && panel_->visible()) || (login_ && login_->visible());
    if (!manual && (triedToday || someoneHere)) {
      if (someoneHere) nextUpdateCheck_ = nowSec() + 600;  // not under the person's hands: again in ten minutes
      updateNote = L"Има версия " + widen(latest) + L".";
      if (panel_) panel_->invalidate();
      return;
    }
    startUpdate(latest, manual);
  });
}

void App::updateToast() {
  if (panel_ && panel_->visible()) panel_->invalidate();
  else notify(kTitle, updateNote);
}

void App::startUpdate(const std::string& version, bool manual) {
  updateBusy = true;
  updateNote = L"Изтегля се версия " + widen(version) + L"…";
  cfg.updateTried = version;
  cfg.updateTriedAt = unixNow();
  cfg.save();
  if (panel_) panel_->invalidate();
  downloadSetup(version, [this, version, manual](const std::wstring& path, const std::wstring& err) {
    if (!err.empty() || !startSetup(path)) {
      updateBusy = false;
      updateNote = err.empty() ? L"Обновлението не можа да бъде пуснато." : err;
      if (manual) updateToast();
      if (panel_) panel_->invalidate();
      return;
    }
    // The setup closes this copy within a few seconds and starts the new one.
    updateNote = L"Инсталира се версия " + widen(version) + L"…";
    if (panel_) panel_->invalidate();
  });
}

void App::command(const std::wstring& c) {
  if (c == L"quit") quit();
  else if (c == L"panel") {
    if (!signedIn) showLogin();
    else if (!panel_ || !panel_->visible()) togglePanel();
  } else if (c == L"hide") hidePanel();
  else if (c == L"signin") showLogin();
  else if (c == L"signout") signOut();
  else if (c == L"refresh") {
    refresh();
    refreshSummary();
  }
  else if (c == L"start") startTimer();
  else if (c == L"pause") pauseTimer();
  else if (c == L"resume") resumeTimer();
  else if (c == L"stop") stopTimer();
  else if (c == L"cancel") cancelTimer();
  else if (c == L"update") checkForUpdate(true);
  else if (c.rfind(L"shot:", 0) == 0) {  // shot:<png>[|row[|column]] — the panel with this data, drawn off screen (checks)
    std::vector<std::wstring> parts;
    std::wstring rest = c.substr(5);
    for (size_t at = 0;;) {
      size_t bar = rest.find(L'|', at);
      parts.push_back(rest.substr(at, bar == std::wstring::npos ? std::wstring::npos : bar - at));
      if (bar == std::wstring::npos) break;
      at = bar + 1;
    }
    if (gfx().ensure()) {
      Panel shot;
      shot.select(parts.size() > 1 ? narrow(parts[1]) : "", parts.size() > 2 ? narrow(parts[2]) : "");
      float s = 1;
      RECT at = panelRect(s);  // the real panel's size on this screen, in DIPs
      int w = (int)((at.right - at.left) / s), h = (int)((at.bottom - at.top) / s);
      auto target = gfx().target(w, h);
      if (target && shot.renderTo(target.Get(), w, h, 3, !systemUsesLightTheme())) savePng(target.Get(), parts[0]);
    }
  } else if (c.rfind(L"move:", 0) == 0) {  // move:<card>:<lane>[:<№ вх. док.>] — what the lane picker does
    std::wstring rest = c.substr(5);
    size_t p1 = rest.find(L':');
    if (p1 != std::wstring::npos) {
      size_t p2 = rest.find(L':', p1 + 1);
      std::wstring lane = rest.substr(p1 + 1, p2 == std::wstring::npos ? std::wstring::npos : p2 - p1 - 1);
      moveCard(_wtoi(rest.substr(0, p1).c_str()), narrow(lane), p2 == std::wstring::npos ? std::wstring() : rest.substr(p2 + 1));
    }
  } else if (c.rfind(L"arm:", 0) == 0) {  // arm:<entry> — „Избери карта“ on a „за разпределяне“ entry (0: cancel)
    armEntry(_wtoi(c.substr(4).c_str()));
  } else if (c.rfind(L"attach:", 0) == 0) {  // attach:<entry>:<card>[:<option ids, comma>] — „Сложи тук“ / the form's „Сложи“
    std::wstring rest = c.substr(7);
    size_t p1 = rest.find(L':');
    if (p1 != std::wstring::npos) {
      size_t p2 = rest.find(L':', p1 + 1);
      int entry = _wtoi(rest.substr(0, p1).c_str());
      int card = _wtoi(rest.substr(p1 + 1, p2 == std::wstring::npos ? std::wstring::npos : p2 - p1 - 1).c_str());
      if (p2 == std::wstring::npos) {
        attachEntry(entry, card);
      } else {
        AttachNeeds answer;
        std::wstring ids = rest.substr(p2 + 1);
        for (size_t at = 0; at < ids.size();) {
          size_t comma = ids.find(L',', at);
          std::wstring one = ids.substr(at, comma == std::wstring::npos ? std::wstring::npos : comma - at);
          if (!one.empty()) answer.chosen.push_back(_wtoi(one.c_str()));
          if (comma == std::wstring::npos) break;
          at = comma + 1;
        }
        attachEntry(entry, card, &answer);
      }
    }
  } else if (c.rfind(L"confirm:", 0) == 0) {  // confirm:<entry>[:<option ids, comma>] — ✓ on stopped time (and its form)
    std::wstring rest = c.substr(8);
    size_t p1 = rest.find(L':');
    int entry = _wtoi(rest.substr(0, p1).c_str());
    if (p1 == std::wstring::npos) {
      confirmPending(entry);
    } else {
      AttachNeeds answer;
      std::wstring ids = rest.substr(p1 + 1);
      for (size_t at = 0; at < ids.size();) {
        size_t comma = ids.find(L',', at);
        std::wstring one = ids.substr(at, comma == std::wstring::npos ? std::wstring::npos : comma - at);
        if (!one.empty()) answer.chosen.push_back(_wtoi(one.c_str()));
        if (comma == std::wstring::npos) break;
        at = comma + 1;
      }
      confirmPending(entry, &answer);
    }
  } else if (c.rfind(L"view:", 0) == 0) {  // view:<id> — what a tab above the table does
    chooseSubview(narrow(c.substr(5)));
  } else if (c.rfind(L"select:", 0) == 0 && panel_) {  // select:<row>[:<column>] — the panel's row / cell, as a click would
    std::string rest = narrow(c.substr(7));
    size_t colon = rest.find(':');  // row keys never contain one; a column key may ("status:…")
    panel_->select(rest.substr(0, colon), colon == std::string::npos ? "" : rest.substr(colon + 1));
  }
}

// ---------------------------------------------------------------- session
void App::showLogin(bool activate) {
  hidePanel();
  if (!login_) {
    login_ = std::make_unique<LoginWindow>();
    if (!login_->create(inst_)) {
      login_.reset();
      return;
    }
  }
  login_->open(cfg.server, cfg.username, activate);
}

void App::signIn(const std::wstring& server, const std::wstring& username, const std::wstring& password) {
  std::wstring base = normalizeServer(server), name = trim(username);
  if (base.empty() || name.empty() || password.empty()) {
    loginError = L"Попълнете адреса, потребителя и паролата.";
    if (login_) login_->invalidate();
    return;
  }
  loginBusy = true;
  loginError.clear();
  api.base = base;
  api.cookie.clear();
  if (login_) login_->invalidate();
  api.login(name, password, [this, base, name](const Reply& r, const User& u) {
    loginBusy = false;
    if (login_) login_->invalidate();
    if (!r.ok()) {
      loginError = r.status == 404 ? L"На този адрес няма приложението (404). Проверете адреса и порта." : r.error;
      return;
    }
    if (u.mustChangePassword) {
      api.cookie.clear();
      loginError = L"Първо сменете паролата си в браузъра (отваряме го), после влезте тук с новата.";
      openBrowser();
      return;
    }
    cfg.server = base;
    cfg.username = name;
    cfg.save();
    saveSession(api.cookie);
    user = u;
    signedIn = true;
    offline = false;
    error.clear();
    stateSeq_++;
    if (login_) ShowWindow(login_->hwnd(), SW_HIDE);
    updatePill();
    updateTray();
    refresh();
  });
}

void App::signOut() {
  hidePanel();
  if (signedIn) api.logout(nullptr);
  api.cookie.clear();
  clearSession();
  signedIn = false;
  haveState = false;
  st = {};
  user = {};
  summary = {};
  cardProblems.clear();
  movingCards.clear();
  subviewChoice.clear();
  armedEntry = 0;
  attachProblems.clear();
  attachingCards.clear();
  pendingNeeds.clear();
  error.clear();
  stateSeq_++;
  updatePill();
  updateTray();
  showLogin();
}

void App::sessionLost() {
  bool was = signedIn;
  api.cookie.clear();
  clearSession();
  signedIn = false;
  haveState = false;
  st = {};
  user = {};
  summary = {};
  cardProblems.clear();
  movingCards.clear();
  subviewChoice.clear();
  armedEntry = 0;
  attachProblems.clear();
  attachingCards.clear();
  pendingNeeds.clear();
  stateSeq_++;
  hidePanel();
  updatePill();
  updateTray();
  if (was) notify(kTitle, L"Сесията изтече. Щракнете бутона на лентата на задачите, за да влезете отново.");
}

// ---------------------------------------------------------------- data
void App::refresh() {
  if (!signedIn || api.base.empty()) return;
  int seq = ++stateSeq_;
  lastPoll_ = nowSec();
  api.timeState([this, seq](const Reply& r, const TimeState& t) {
    if (seq != stateSeq_) return;  // something newer was asked since
    if (r.status == 401) {
      sessionLost();
      return;
    }
    offline = r.status == 0;
    if (r.ok()) {
      st = t;
      haveState = true;
      // The picked entry went (attached in the browser, discarded by an administrator…): nothing to attach any more.
      if (armedEntry && std::none_of(st.unassigned.begin(), st.unassigned.end(), [this](const Unassigned& u) { return u.id == armedEntry; })) {
        armedEntry = 0;
        attachProblems.clear();
      }
      // ✓ done elsewhere (the browser), or ✕: the question under the stopwatch has no subject any more.
      for (auto it = pendingNeeds.begin(); it != pendingNeeds.end();)
        it = std::any_of(st.pending.begin(), st.pending.end(), [&](const Pending& p) { return p.id == it->first; }) ? std::next(it)
                                                                                                                   : pendingNeeds.erase(it);
      if (!user.id) {
        api.session([this](const Reply& rs, const User& u) {
          if (rs.ok() && u.id) {
            user = u;
            if (panel_) panel_->invalidate();
          }
        });
      }
    }
    updatePill();
    updateTray();
    if (panel_) panel_->invalidate();
  });
}

void App::action(const wchar_t* method, const std::string& path, const Json* body) {
  if (!signedIn) return;
  busy = true;
  error.clear();
  if (panel_) panel_->invalidate();
  api.send(method, path, body, [this](const Reply& r) {
    busy = false;
    if (r.status == 401) {
      sessionLost();
      return;
    }
    if (!r.ok()) {
      error = r.error;
      offline = r.status == 0;
    }
    refresh();
  });
}

void App::startTimer() { action(L"POST", "/api/time/start", nullptr); }

void App::startTimerOnCard(int cardId) {
  Json b = Json::object();
  b.set("cardId", cardId);
  // A stopwatch already running (started without a card, say) is attached to the card instead — the server's
  // „избор на карта, докато тече" (PATCH /time/running); otherwise a new one starts on it.
  if (st.hasRunning) action(L"PATCH", "/api/time/running", &b);
  else action(L"POST", "/api/time/start", &b);
}

void App::moveCard(int cardId, const std::string& lane, const std::wstring& reference) {
  if (!signedIn || movingCards.count(cardId)) return;
  Json b = Json::object();
  b.set("lane", lane);
  if (!trim(reference).empty()) b.set("referenceId", narrow(trim(reference)));
  movingCards.insert(cardId);
  cardProblems.erase(cardId);
  if (panel_) panel_->invalidate();
  api.send(L"POST", "/api/collector/cards/" + std::to_string(cardId) + "/move", &b, [this, cardId, lane](const Reply& r) {
    movingCards.erase(cardId);
    if (r.status == 401) {
      sessionLost();
      return;
    }
    if (r.ok()) {
      // Shown at once; the table's numbers follow with the refresh below.
      for (auto& k : summary.cards)
        if (k.id == cardId) {
          k.laneKey = lane;
          if (lane == "done") k.closed = true;
        }
    } else {
      // The refusals (422) carry a code in `error` and the sentence for people in `message`.
      CardProblem p;
      p.code = r.json.str("error");
      p.message = widen(r.json.str("message"));
      if (p.message.empty()) p.message = r.error;
      cardProblems[cardId] = p;
    }
    refreshSummary();
    if (panel_) panel_->invalidate();
  });
}

void App::chooseSubview(const std::string& id) {
  if (!signedIn || id.empty()) return;
  if (id == (subviewChoice.empty() ? summary.subviewId : subviewChoice)) return;
  subviewChoice = id;
  summary.subviewId = id;  // the tab lights up at once; the table says "Зарежда…" until it comes
  summary.loaded = false;
  for (auto& v : summary.subviews)
    if (v.id == id) summary.subviewLabel = v.label;
  Json b = Json::object();
  b.set("boardSubview", id);
  api.send(L"PATCH", "/api/me/view", &b, [this, id](const Reply& r) {
    if (r.status == 401) {
      sessionLost();
      return;
    }
    // Stored: from now on the server's own (a later change in the browser is followed here too). Refused — e.g.
    // locked by the administrator (403) — it stays chosen on this PC until the app closes, as in the web.
    if (r.ok() && subviewChoice == id) subviewChoice.clear();
    if (!r.ok()) logf("view: %s not stored (%d)", id.c_str(), r.status);
  });
  refreshSummary();
  if (panel_) panel_->invalidate();
}

void App::armEntry(int entryId) {
  armedEntry = entryId;
  attachProblems.clear();  // the forms were for the entry picked before
  if (panel_) panel_->invalidate();
}

void App::attachEntry(int entryId, int cardId, const AttachNeeds* answer) {
  if (!signedIn || entryId <= 0 || cardId <= 0 || attachingCards.count(cardId)) return;
  Json b = Json::object();
  b.set("cardId", cardId);
  putAnswer(b, answer);
  int secs = 0;
  for (auto& u : st.unassigned)
    if (u.id == entryId) secs = u.seconds;
  std::wstring title;
  for (auto& k : summary.cards)
    if (k.id == cardId) title = k.title;
  attachingCards.insert(cardId);
  if (panel_) panel_->invalidate();
  api.send(L"PATCH", "/api/time/entries/" + std::to_string(entryId), &b, [this, entryId, cardId, secs, title](const Reply& r) {
    attachingCards.erase(cardId);
    if (r.status == 401) {
      sessionLost();
      return;
    }
    if (r.ok()) {
      if (armedEntry == entryId) armedEntry = 0;
      attachProblems.clear();
      attachNote = clockText(secs, false) + L"  →  " + (title.empty() ? std::wstring(L"картата") : title);
      attachNoteUntil = nowSec() + 8;
      st.unassigned.erase(std::remove_if(st.unassigned.begin(), st.unassigned.end(), [entryId](const Unassigned& u) { return u.id == entryId; }),
                          st.unassigned.end());  // at once; the refresh below confirms it
      st.unattributed = (int)st.unassigned.size();
    } else {
      // 422 time_detail_required carries what is missing (a choice, Дейност, Коментар); 409 — a stopwatch runs; 403 …
      attachProblems[cardId] = parseAttachNeeds(r.json, r.error);
    }
    refresh();
    if (panel_) panel_->invalidate();
  });
}

void App::dismissAttach(int cardId) {
  attachProblems.erase(cardId);
  if (panel_) panel_->invalidate();
}

void App::dismissProblem(int cardId) {
  cardProblems.erase(cardId);
  if (panel_) panel_->invalidate();
}

void App::refreshSummary() {
  if (!signedIn || api.base.empty()) return;
  if (summaryBusy) {
    summaryAgain_ = true;
    return;
  }
  SYSTEMTIME t;
  GetLocalTime(&t);
  char path[96];
  snprintf(path, sizeof path, "/api/desktop/summary?year=%d&month=%d", t.wYear, t.wMonth);
  std::string url = path;
  if (!subviewChoice.empty()) url += "&subview=" + subviewChoice;  // ids are plain ASCII: ex-clients, mp-people
  summaryBusy = true;
  summaryAt_ = nowSec();
  api.send(L"GET", url, nullptr, [this](const Reply& r) {
    summaryBusy = false;
    bool again = summaryAgain_;
    summaryAgain_ = false;
    if (r.status == 401) {
      sessionLost();
      return;
    }
    if (r.ok()) {
      summary = parseSummary(r.json);
      summaryError.clear();
    } else if (r.status == 404) {
      summaryError = L"Сървърът още няма таблицата за настолното приложение — нужна е по-нова версия на сървъра.";
    } else {
      summaryError = r.error;
    }
    if (panel_) panel_->invalidate();
    if (again) refreshSummary();
  });
}
void App::pauseTimer() { action(L"POST", "/api/time/pause", nullptr); }
void App::resumeTimer() { action(L"POST", "/api/time/resume", nullptr); }
void App::stopTimer() { action(L"POST", "/api/time/stop", nullptr); }
void App::cancelTimer() { action(L"DELETE", "/api/time/running", nullptr); }
namespace {
// The answer to a 422 time_detail_required, in the body: the choice and/or Дейност + Коментар.
void putAnswer(Json& b, const AttachNeeds* answer) {
  if (!answer) return;
  if (!trim(answer->activity).empty()) b.set("activity", narrow(trim(answer->activity)));
  if (!trim(answer->comment).empty()) b.set("comment", narrow(trim(answer->comment)));
  if (!answer->options.empty() || !answer->chosen.empty()) {
    Json ids = Json::array();
    for (int id : answer->chosen) ids.push(id);
    b.set("optionIds", ids);
  }
}
}  // namespace

void App::confirmPending(int id, const AttachNeeds* answer) {
  if (!signedIn) return;
  Json b = Json::object();
  putAnswer(b, answer);
  busy = true;
  error.clear();
  if (panel_) panel_->invalidate();
  api.send(L"POST", "/api/time/entries/" + std::to_string(id) + "/confirm", &b, [this, id](const Reply& r) {
    busy = false;
    if (r.status == 401) {
      sessionLost();
      return;
    }
    if (r.ok()) {
      pendingNeeds.erase(id);
    } else if (r.status == 422 && r.json.str("error") == "time_detail_required") {
      pendingNeeds[id] = parseAttachNeeds(r.json, r.error);  // the panel asks for it under the stopwatch
    } else {
      error = r.error;
      offline = r.status == 0;
    }
    refresh();
  });
}

void App::dismissPendingNeeds(int id) {
  pendingNeeds.erase(id);
  if (panel_) panel_->invalidate();
}
void App::discardPending(int id) { action(L"POST", "/api/time/entries/" + std::to_string(id) + "/discard", nullptr); }

void App::openBrowser() {
  std::wstring url = api.base.empty() ? normalizeServer(cfg.server) : api.base;
  if (!url.empty()) ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- panel
void App::togglePanel() {
  if (!signedIn) {
    showLogin();
    return;
  }
  if (panel_ && panel_->visible()) {
    hidePanel();
    return;
  }
  if (nowSec() - panelHiddenAt_ < 0.35) return;  // that same click just closed it
  float s = 1;
  RECT at = panelRect(s);
  if (!panel_) {
    panel_ = std::make_unique<Panel>();
    if (!panel_->create(inst_, at)) {
      panel_.reset();
      return;
    }
  }
  panel_->showAt(at);
  refresh();
  refreshSummary();
}

// Above the taskbar button, right edges aligned, like Windows' own flyouts: wide enough for the summary table and
// almost as tall as the screen (the person's wish — the table and its cards need the room); never more than the
// work area.
RECT App::panelRect(float& s) {
  RECT anchor = pill_.shown() ? pill_.rect() : RECT{};
  TaskbarInfo t = queryTaskbar();
  s = t.ok ? t.scale : 1.f;
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(t.mon ? t.mon : MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
  int m = (int)(12 * s);
  // 1040: the widest table (by client, for a partner: 11 number columns) and still room for a firm's name.
  int w = std::min<int>((int)(1040 * s), mi.rcWork.right - mi.rcWork.left - 2 * m);
  int h = mi.rcWork.bottom - mi.rcWork.top - 2 * m;
  int right = pill_.shown() ? anchor.right : mi.rcWork.right - m;
  int bottom = mi.rcWork.bottom - m;
  RECT at{std::max<LONG>(mi.rcWork.left + m, right - w), std::max<LONG>(mi.rcWork.top + m, bottom - h), 0, 0};
  at.right = at.left + w;
  at.bottom = at.top + h;
  return at;
}

void App::hidePanel() {
  if (panel_ && panel_->visible()) {
    ShowWindow(panel_->hwnd(), SW_HIDE);
    panelHiddenAt_ = nowSec();
  }
}

void CALLBACK App::onForeground(HWINEVENTHOOK, DWORD, HWND h, LONG, LONG, DWORD, DWORD) {
  // Another program came to the front: close the flyout (like Windows' own) and get back above the taskbar.
  if (!g_app) return;
  if (g_app->panel_ && g_app->panel_->visible() && h != g_app->panel_->hwnd()) g_app->hidePanel();
  g_app->pill_.raise();
}

// ---------------------------------------------------------------- taskbar button + tray
void App::updatePill() {
  if (!signedIn) {
    pill_.show(PillIcon::SignedOut, L"Контрол", L"Вход", 0);
    return;
  }
  if (st.hasRunning) {
    int s = runningSeconds();
    std::wstring t = target(st.running.cardTitle, st.running.clientName);
    if (st.running.paused) pill_.show(PillIcon::Paused, clockText(s, true), L"Пауза · " + t, s);
    else pill_.show(offline ? PillIcon::Offline : PillIcon::Running, clockText(s, true), offline ? L"Няма връзка · " + t : t, s);
    return;
  }
  if (!haveState) {
    pill_.show(offline ? PillIcon::Offline : PillIcon::Idle, L"Контрол", offline ? L"Няма връзка" : L"Зарежда…", 0);
    return;
  }
  std::wstring l2 = offline ? L"Няма връзка"
                    : !st.pending.empty() ? L"Чака решение: " + std::to_wstring(st.pending.size())
                                          : L"Не тече";
  pill_.show(offline ? PillIcon::Offline : PillIcon::Idle, L"Днес " + clockText(st.todaySeconds, false), l2, 0);
}

void App::updateTray() {
  if (!hwnd_) return;
  std::wstring tip = kTitle;
  if (!signedIn) tip += L"\nНе сте влезли";
  else if (st.hasRunning)
    tip += (st.running.paused ? L"\n⏸ " : L"\n⏱ ") + clockText(runningSeconds(), false) + L" · " +
           target(st.running.cardTitle, st.running.clientName);
  else if (haveState) tip += L"\nДнес " + clockText(st.todaySeconds, false);
  if (trayOn_ && tip == trayTip_) return;
  trayTip_ = tip;
  NOTIFYICONDATAW ni{};
  ni.cbSize = sizeof ni;
  ni.hWnd = hwnd_;
  ni.uID = 1;
  ni.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  ni.uCallbackMessage = WM_APP_TRAY;
  ni.hIcon = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
  lstrcpynW(ni.szTip, tip.c_str(), 128);
  if (!trayOn_) {
    trayOn_ = Shell_NotifyIconW(NIM_ADD, &ni) != FALSE;
    ni.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &ni);
  } else {
    Shell_NotifyIconW(NIM_MODIFY, &ni);
  }
}

void App::notify(const std::wstring& title, const std::wstring& text) {
  if (!trayOn_) return;
  NOTIFYICONDATAW ni{};
  ni.cbSize = sizeof ni;
  ni.hWnd = hwnd_;
  ni.uID = 1;
  ni.uFlags = NIF_INFO;
  ni.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
  lstrcpynW(ni.szInfoTitle, title.c_str(), 64);
  lstrcpynW(ni.szInfo, text.c_str(), 256);
  Shell_NotifyIconW(NIM_MODIFY, &ni);
}

void App::trayMenu(POINT at) {
  HMENU m = CreatePopupMenu();
  auto add = [&](UINT id, const std::wstring& text, bool enabled = true, bool checked = false) {
    AppendMenuW(m, MF_STRING | (enabled ? 0 : MF_GRAYED) | (checked ? MF_CHECKED : 0), id, text.c_str());
  };
  if (signedIn) {
    add(kMenuPanel, L"Отвори панела");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (!st.hasRunning) add(kMenuStart, L"Пусни хронометъра (без карта)", haveState && st.pending.empty());
    else if (st.running.paused) add(kMenuResume, L"Продължи");
    else add(kMenuPause, L"Пауза");
    if (st.hasRunning) add(kMenuStop, L"Спри хронометъра");
    add(kMenuRefresh, L"Обнови");
  } else {
    add(kMenuSignIn, L"Вход…");
  }
  add(kMenuBrowser, L"Отвори в браузъра", !api.base.empty() || !cfg.server.empty());
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  add(kMenuPill, L"Бутон на лентата на задачите", true, cfg.pill);
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  if (signedIn) add(kMenuSignOut, L"Изход от профила" + (user.username.empty() ? std::wstring() : L" (" + user.username + L")"));
  add(kMenuUpdate, updateBusy ? L"Обновява се…" : L"Провери за обновление", !updateBusy);
  add(0, L"Версия " + widen(KD_VERSION_STR), false);
  add(kMenuQuit, L"Затвори програмата");
  SetForegroundWindow(hwnd_);
  UINT cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, at.x, at.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(m);
  switch (cmd) {
    case kMenuPanel: togglePanel(); break;
    case kMenuStart: startTimer(); break;
    case kMenuPause: pauseTimer(); break;
    case kMenuResume: resumeTimer(); break;
    case kMenuStop: stopTimer(); break;
    case kMenuRefresh:
      refresh();
      refreshSummary();
      break;
    case kMenuBrowser: openBrowser(); break;
    case kMenuPill:
      cfg.pill = !cfg.pill;
      cfg.save();
      pill_.setEnabled(cfg.pill);
      break;
    case kMenuSignIn: showLogin(); break;
    case kMenuSignOut: signOut(); break;
    case kMenuQuit: quit(); break;
    case kMenuUpdate: checkForUpdate(true); break;
  }
}

// ---------------------------------------------------------------- once a second
void App::tick() {
  pill_.sync();
  if (signedIn && st.hasRunning) {
    updatePill();
    static int minute = -1;
    int now = runningSeconds() / 60;
    if (now != minute) {
      minute = now;
      updateTray();
    }
  }
  if (signedIn && nowSec() - lastPoll_ >= kPollEvery) refresh();
  if (signedIn && panel_ && panel_->visible() && nowSec() - summaryAt_ >= 60) refreshSummary();  // while someone looks
  if (attachNoteUntil > 0 && nowSec() >= attachNoteUntil) {  // „✓ Закачено …“ has been read
    attachNoteUntil = 0;
    attachNote.clear();
    if (panel_) panel_->invalidate();
  }
  if (nowSec() >= nextUpdateCheck_) {
    nextUpdateCheck_ = nowSec() + 6 * 3600 + (GetTickCount() % 900);  // a little spread, so an office does not ask at once
    checkForUpdate(false);
  }

  // Closed windows keep a GPU device and swap chains alive; let them go after a while.
  static double hiddenSince = 0;
  bool anyOpen = (panel_ && panel_->visible()) || (login_ && login_->visible());
  bool anyKept = panel_ || login_;
  if (anyOpen || !anyKept) {
    hiddenSince = 0;
  } else if (hiddenSince == 0) {
    hiddenSince = nowSec();
  } else if (nowSec() - hiddenSince > kReleaseAfter) {
    hiddenSince = 0;
    panel_.reset();
    if (signedIn) login_.reset();
    gfx().release();
    SetProcessWorkingSetSizeEx(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1, 0);
  }
}

// ---------------------------------------------------------------- messages
LRESULT CALLBACK App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
  auto* self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (self) {
    if (!self->hwnd_) self->hwnd_ = h;
    return self->handle(m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

LRESULT App::handle(UINT m, WPARAM w, LPARAM l) {
  if (m == taskbarCreated_ && m) {  // Explorer restarted: the tray icon is gone, the taskbar moved
    trayOn_ = false;
    updateTray();
    pill_.relayout();
    return 0;
  }
  switch (m) {
    case WM_APP_NET: netDeliver(l); return 0;
    case WM_APP_SHOWPANEL: command(L"panel"); return 0;
    case WM_APP_COMMAND: {
      auto* cmd = (std::wstring*)l;
      command(*cmd);
      delete cmd;
      return 0;
    }
    case WM_COPYDATA: {
      auto* cd = (const COPYDATASTRUCT*)l;
      if (!cd || cd->dwData != kCopyCommand || cd->cbData < sizeof(wchar_t)) return FALSE;
      std::wstring cmd((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t) - 1);
      // Not inside SendMessage: the sender is waiting, and a command may show a window or call the server.
      PostMessageW(hwnd_, WM_APP_COMMAND, 0, (LPARAM) new std::wstring(cmd));
      return TRUE;
    }
    case WM_APP_TRAY:
      switch (LOWORD(l)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
          if (signedIn) togglePanel();
          else showLogin();
          break;
        case WM_CONTEXTMENU: trayMenu(POINT{GET_X_LPARAM(w), GET_Y_LPARAM(w)}); break;
      }
      return 0;
    case WM_TIMER:
      if (w == kTick) tick();
      return 0;
    case WM_SETTINGCHANGE:
      if (l && !lstrcmpW((const wchar_t*)l, L"ImmersiveColorSet")) {
        if (panel_) panel_->themeChanged();
        if (login_) login_->themeChanged();
      }
      pill_.sync();
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: pill_.relayout(); return 0;
    case WM_ENDSESSION: if (w) quit(); return 0;
    case WM_WTSSESSION_CHANGE:
      // While the screen is locked the button hides (the lock screen owns the display); on unlock the
      // stopwatch may have been changed from another computer in the meantime.
      if (w == WTS_SESSION_UNLOCK) {
        pill_.relayout();
        refresh();
      }
      return 0;
  }
  return DefWindowProcW(hwnd_, m, w, l);
}

}  // namespace kd
