#include "views.h"

#include "app.h"

namespace kd {
using namespace rt;

namespace {
// Segoe Fluent Icons
constexpr const wchar_t* kPlay = L"";
constexpr const wchar_t* kPause = L"";
constexpr const wchar_t* kStop = L"";
constexpr const wchar_t* kCancel = L"";
constexpr const wchar_t* kCheck = L"";
constexpr const wchar_t* kRefresh = L"";
constexpr const wchar_t* kOpen = L"";
constexpr const wchar_t* kSignOut = L"";
constexpr const wchar_t* kServer = L"";
constexpr const wchar_t* kPerson = L"";
constexpr const wchar_t* kKey = L"";

std::wstring what(const std::wstring& card, const std::wstring& client) {
  if (card.empty() && client.empty()) return L"без карта";
  if (card.empty()) return client;
  if (client.empty()) return card;
  return client + L" — " + card;
}
std::wstring num(int n) { return std::to_wstring(n); }
}  // namespace

// ================================================================ panel
bool Panel::create(HINSTANCE inst, RECT at) {
  return createPopup(inst, L"KanbanDesktopPanel", at, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 3);
}

void Panel::showAt(RECT at) {
  SetWindowPos(hwnd_, HWND_TOPMOST, at.left, at.top, at.right - at.left, at.bottom - at.top, SWP_SHOWWINDOW);
  SetForegroundWindow(hwnd_);
  invalidate();
}

void Panel::onDeactivate() { app().hidePanel(); }

bool Panel::onClose() {
  app().hidePanel();
  return false;
}

void Panel::paint() {
  App& a = app();
  const UiPalette& P = ui.pal;
  const float W = ui.view.w(), H = ui.view.h();
  const float x = 16, cw = W - 32;
  float y = 14;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const Color amber = P.dark ? Color::hex(0xFBBF24) : Color::hex(0xB45309);

  // ---- header
  ui.text(RectF(x, y, x + cw - 116, y + 22), L"Контрол на срокове", 15, P.text, 600);
  std::wstring who = a.user.name.empty() ? a.user.username : a.user.name;
  ui.text(RectF(x, y + 22, x + cw - 116, y + 40), who.empty() ? a.api.base : who, 12, P.dim);
  float bx = x + cw - 32;
  if (ui.iconButton(ui.id("signout"), RectF(bx, y + 4, bx + 32, y + 36), kSignOut, L"Изход от профила")) after([] { app().signOut(); });
  bx -= 36;
  if (ui.iconButton(ui.id("web"), RectF(bx, y + 4, bx + 32, y + 36), kOpen, L"Отвори приложението в браузъра"))
    after([] { app().openBrowser(); });
  bx -= 36;
  if (ui.iconButton(ui.id("refresh"), RectF(bx, y + 4, bx + 32, y + 36), kRefresh, L"Обнови", !a.busy)) after([] { app().refresh(); });
  y += 54;

  // ---- the stopwatch
  const TimeState& st = a.st;
  bool running = st.hasRunning && !st.running.paused, paused = st.hasRunning && st.running.paused;
  RectF card(x, y, x + cw, y + 158);
  ui.card(card, 10);
  int secs = a.runningSeconds();
  ui.text(RectF(card.l + 18, card.t + 12, card.r - 18, card.t + 58), st.hasRunning ? clockText(secs, true) : L"0:00:00", 36,
          running ? green : paused ? amber : P.faint, 600);
  std::wstring line = !a.haveState ? (a.offline ? L"Няма връзка със сървъра" : L"Зарежда…")
                      : !st.hasRunning ? L"Хронометърът е спрян"
                      : (paused ? L"На пауза · " : L"Тече · ") + what(st.running.cardTitle, st.running.clientName);
  ui.text(RectF(card.l + 18, card.t + 60, card.r - 18, card.t + 80), line, 12.5f, P.dim);
  if (running) ui.animating = true;  // the big clock ticks

  float by = card.t + 96, bh = 36;
  if (!st.hasRunning) {
    bool blockedByPending = !st.pending.empty();
    bool blockedByCap = st.maxUnassigned > 0 && st.unattributed >= st.maxUnassigned;
    bool can = a.haveState && st.canTrack && !a.busy && !blockedByPending && !blockedByCap;
    if (ui.button(ui.id("start"), RectF(card.l + 18, by, card.r - 18, by + bh), L"Пусни хронометъра", Btn::Accent, kPlay, can))
      after([] { app().startTimer(); });
    std::wstring hint = blockedByPending ? L"Първо решете отчетеното време отдолу (✓ или ✕)."
                        : blockedByCap   ? L"Имате " + num(st.unattributed) + L" неразпределени записа (максимум " + num(st.maxUnassigned) +
                                             L"). Разпределете ги в браузъра."
                                         : L"Без карта — картата се избира в браузъра при спиране или по-късно.";
    ui.text(RectF(card.l + 18, by + bh + 4, card.r - 18, card.b - 4), hint, 11.5f, P.dim, 400, Align::Left, VAlign::Top, true);
  } else {
    float gap = 8, bw = (card.w() - 36 - 2 * gap) / 3;
    RectF b1(card.l + 18, by, card.l + 18 + bw, by + bh), b2(b1.r + gap, by, b1.r + gap + bw, by + bh),
        b3(b2.r + gap, by, b2.r + gap + bw, by + bh);
    if (paused) {
      if (ui.button(ui.id("resume"), b1, L"Продължи", Btn::Accent, kPlay, !a.busy)) after([] { app().resumeTimer(); });
    } else {
      if (ui.button(ui.id("pause"), b1, L"Пауза", Btn::Standard, kPause, !a.busy)) after([] { app().pauseTimer(); });
    }
    if (ui.button(ui.id("stop"), b2, L"Спри", Btn::Standard, kStop, !a.busy)) after([] { app().stopTimer(); });
    bool armed = armedCancel_ > nowSec();
    if (ui.button(ui.id("cancel"), b3, armed ? L"Сигурни?" : L"Откажи", armed ? Btn::Danger : Btn::Subtle, kCancel, !a.busy)) {
      if (armed) {
        armedCancel_ = 0;
        after([] { app().cancelTimer(); });
      } else {
        armedCancel_ = nowSec() + 3;
      }
    }
    if (armed) ui.animating = true;
    ui.text(RectF(card.l + 18, by + bh + 4, card.r - 18, card.b - 4), L"„Спри“ записва времето за ✓ / ✕; „Откажи“ го изтрива.", 11.5f,
            P.dim, 400, Align::Left, VAlign::Top, true);
  }
  y = card.b + 14;

  // ---- stopped sessions waiting for ✓ / ✕
  if (!st.pending.empty()) {
    ui.text(RectF(x, y, x + cw, y + 20), L"Чака решение", 13, P.text, 600);
    y += 24;
    int shown = 0;
    for (const Pending& p : st.pending) {
      if (shown++ == 3) break;
      RectF row(x, y, x + cw, y + 40);
      ui.card(row, 8);
      std::wstring t = clockText(p.seconds, false) + L"  ·  " + what(p.cardTitle, p.clientName);
      ui.text(RectF(row.l + 12, row.t, row.r - 92, row.b), t, 12.5f, P.text);
      int id = p.id;
      if (ui.iconButton(ui.id("ok", id), RectF(row.r - 84, row.t + 4, row.r - 48, row.b - 4), kCheck, L"Запиши (✓)", !a.busy))
        after([id] { app().confirmPending(id); });
      if (ui.iconButton(ui.id("no", id), RectF(row.r - 44, row.t + 4, row.r - 8, row.b - 4), kCancel, L"Отхвърли (✕)", !a.busy))
        after([id] { app().discardPending(id); });
      y += 46;
    }
    if (st.pending.size() > 3) {
      ui.text(RectF(x, y, x + cw, y + 18), L"Още " + num((int)st.pending.size() - 3) + L" — в браузъра", 12, P.dim);
      y += 22;
    }
  }

  // ---- today
  if (a.haveState) {
    std::wstring info = L"Днес отчетено: " + clockText(st.todaySeconds, false);
    if (st.unattributed > 0) {
      info += L"   ·   За разпределяне: " + num(st.unattributed);
      if (st.maxUnassigned > 0) info += L" от " + num(st.maxUnassigned);
    }
    ui.text(RectF(x, y, x + cw, y + 20), info, 12, P.dim);
    y += 26;
  }
  if (!a.error.empty() || a.offline) {
    std::wstring e = a.error.empty() ? L"Няма връзка със сървъра — опитваме отново на всеки 30 секунди." : a.error;
    ui.text(RectF(x, y, x + cw, H - 44), e, 12, P.danger, 400, Align::Left, VAlign::Top, true);
  }

  // ---- footer
  if (!a.updateNote.empty())
    ui.text(RectF(x, H - 58, x + cw, H - 38), a.updateNote, 11.5f, a.updateBusy ? P.accent : P.dim);
  if (ui.link(ui.id("browser"), RectF(x, H - 36, x + cw - 90, H - 14), L"Отвори приложението в браузъра", 12.5f)) after([] { app().openBrowser(); });
  ui.text(RectF(x + cw - 90, H - 36, x + cw, H - 14), L"версия " + widen(KD_VERSION_STR), 11.5f, P.faint, 400, Align::Right);
}

// ================================================================ sign-in
bool LoginWindow::create(HINSTANCE inst) {
  return createWindow(inst, L"KanbanDesktopLogin", L"Контрол на срокове — вход", 440, 470, 440, 470, false);
}

void LoginWindow::open(const std::wstring& server, const std::wstring& username, bool activate) {
  if (server_.empty()) server_ = server;
  if (user_.empty()) user_ = username;
  focusPending_ = true;
  if (activate) {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
  } else {
    // Found at start-up: typing elsewhere must not land in these fields. The taskbar button flashes instead.
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    FLASHWINFO fi{sizeof fi, hwnd_, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
    FlashWindowEx(&fi);
  }
  invalidate();
}

void LoginWindow::submit() {
  after([this] { app().signIn(server_, user_, pass_); });
}

void LoginWindow::paint() {
  App& a = app();
  const UiPalette& P = ui.pal;
  const float W = ui.view.w();
  const float x = 28, cw = W - 56;
  float y = 22;
  ui.text(RectF(x, y, x + cw, y + 32), L"Вход", 24, P.text, 600);
  y += 38;
  ui.text(RectF(x, y, x + cw, y + 40),
          L"Същите адрес, потребител и парола като в браузъра. Паролата не се пази на този компютър — пази се само сесията.", 12.5f,
          P.dim, 400, Align::Left, VAlign::Top, true);
  y += 52;

  uint32_t idServer = ui.id("server"), idUser = ui.id("user"), idPass = ui.id("pass");
  if (focusPending_) {
    ui.focusOn(server_.empty() ? idServer : user_.empty() ? idUser : idPass);
    focusPending_ = false;
  }
  bool entered = false;
  ui.text(RectF(x, y, x + cw, y + 18), L"Адрес на сървъра", 12.5f, P.text, 600);
  y += 22;
  ui.textBox(idServer, RectF(x, y, x + cw, y + 34), server_, L"напр. http://server:8106", kServer, &entered);
  y += 46;
  ui.text(RectF(x, y, x + cw, y + 18), L"Потребител", 12.5f, P.text, 600);
  y += 22;
  ui.textBox(idUser, RectF(x, y, x + cw, y + 34), user_, L"потребителско име или имейл", kPerson, &entered);
  y += 46;
  ui.text(RectF(x, y, x + cw, y + 18), L"Парола", 12.5f, P.text, 600);
  y += 22;
  ui.passwordBox(idPass, RectF(x, y, x + cw, y + 34), pass_, {}, kKey, &entered);
  y += 50;

  if (!a.loginError.empty())
    ui.text(RectF(x, y, x + cw, y + 40), a.loginError, 12, P.danger, 400, Align::Left, VAlign::Top, true);
  y += 46;
  bool ready = !a.loginBusy && !trim(server_).empty() && !trim(user_).empty() && !pass_.empty();
  if (ui.button(ui.id("go"), RectF(x, y, x + cw, y + 38), a.loginBusy ? L"Влизане…" : L"Вход", Btn::Accent, nullptr, ready) ||
      (entered && ready))
    submit();
}

}  // namespace kd
