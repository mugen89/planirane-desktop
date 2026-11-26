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
constexpr const wchar_t* kAttach = L"";  // link

std::wstring what(const std::wstring& card, const std::wstring& client) {
  if (card.empty() && client.empty()) return L"без карта";
  if (card.empty()) return client;
  if (client.empty()) return card;
  return client + L" — " + card;
}
std::wstring num(int n) { return std::to_wstring(n); }

std::wstring initials(const std::wstring& name) {
  std::wstring out;
  bool start = true;
  for (wchar_t c : name) {
    if (c == L' ') {
      start = true;
    } else if (start) {
      out += (wchar_t)towupper(c);
      start = false;
      if (out.size() == 2) break;
    }
  }
  return out;
}

std::wstring shortDate(const std::wstring& iso) {  // 2026-11-30… → 30.11
  if (iso.size() < 10) return iso;
  return iso.substr(8, 2) + L"." + iso.substr(5, 2);
}

// Which cards a click on (row, column) means — the same reading as the browser's table (PresetSummary).
bool cardInCell(const Summary& s, const SumCard& k, const std::string& row, const std::string& col) {
  bool byStatus = s.table == "status";
  bool inRow = row == "__total" ? !(byStatus && k.closed) || col == "done" : k.row == row;
  if (!inRow) return false;
  if (col.rfind("status:", 0) == 0) return !k.closed && k.status == col.substr(7);
  if (col == "approval") return !k.closed && k.awaiting;
  if (col == "done") return k.closed;
  if (col == "tasks") return !k.hasClient && (!k.closed || byStatus);
  if (col == "clients") return k.hasClient && (!k.closed || byStatus);
  // the row itself, its count, its plan
  if (byStatus && row == "done") return k.closed;
  return !k.closed;
}
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

void Panel::select(const std::string& row, const std::string& col) {
  selRow_ = row;
  selCol_ = row.empty() ? "" : col;
  invalidate();
}

void Panel::onDeactivate() { app().hidePanel(); }

bool Panel::onClose() {
  app().hidePanel();
  return false;
}

std::wstring Panel::toUpperBg(const std::wstring& s) {
  std::wstring out = s;
  if (!out.empty()) CharUpperBuffW(out.data(), (DWORD)out.size());
  return out;
}

void Panel::paint() {
  App& a = app();
  const UiPalette& P = ui.pal;
  const float W = ui.view.w(), H = ui.view.h();
  const float x = 16, cw = W - 32;
  float y = 12;

  // ---- header
  ui.text(RectF(x, y, x + cw - 120, y + 22), L"Контрол на срокове", 15, P.text, 600);
  std::wstring who = a.user.name.empty() ? a.user.username : a.user.name;
  if (a.summary.loaded) who += (who.empty() ? L"" : L"  ·  ") + a.summary.subviewLabel;
  ui.text(RectF(x, y + 22, x + cw - 120, y + 40), who.empty() ? a.api.base : who, 12, P.dim);
  float bx = x + cw - 32;
  if (ui.iconButton(ui.id("signout"), RectF(bx, y + 4, bx + 32, y + 36), kSignOut, L"Изход от профила")) after([] { app().signOut(); });
  bx -= 36;
  if (ui.iconButton(ui.id("web"), RectF(bx, y + 4, bx + 32, y + 36), kOpen, L"Отвори приложението в браузъра"))
    after([] { app().openBrowser(); });
  bx -= 36;
  if (ui.iconButton(ui.id("refresh"), RectF(bx, y + 4, bx + 32, y + 36), kRefresh, L"Обнови", !a.busy))
    after([] {
      app().refresh();
      app().refreshSummary();
    });
  y += 50;

  y = paintTimer(RectF(x, y, x + cw, y + 64));
  paintSummary(RectF(x, y + 10, x + cw, H - 42));

  // ---- footer
  if (!a.updateNote.empty())
    ui.text(RectF(x, H - 34, x + cw - 300, H - 12), a.updateNote, 11.5f, a.updateBusy ? P.accent : P.dim);
  else if (!a.error.empty() || a.offline)
    ui.text(RectF(x, H - 34, x + cw - 300, H - 12), a.error.empty() ? L"Няма връзка със сървъра — опитваме отново." : a.error, 11.5f, P.danger);
  if (ui.link(ui.id("browser"), RectF(x + cw - 300, H - 34, x + cw - 80, H - 12), L"Отвори приложението в браузъра", 12.f))
    after([] { app().openBrowser(); });
  ui.text(RectF(x + cw - 80, H - 34, x + cw, H - 12), L"версия " + widen(KD_VERSION_STR), 11.f, P.faint, 400, Align::Right);
}

// The stopwatch as one strip: clock, what it is for, the buttons; stopped time waiting for ✓ / ✕ under it.
float Panel::paintTimer(RectF r) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const TimeState& st = a.st;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const Color amber = P.dark ? Color::hex(0xFBBF24) : Color::hex(0xB45309);
  bool running = st.hasRunning && !st.running.paused, paused = st.hasRunning && st.running.paused;
  ui.card(r, 10);
  int secs = a.runningSeconds();
  ui.text(RectF(r.l + 16, r.t + 6, r.l + 190, r.t + 40), st.hasRunning ? clockText(secs, true) : L"0:00:00", 26,
          running ? green : paused ? amber : P.faint, 600);
  std::wstring line = !a.haveState    ? (a.offline ? L"Няма връзка със сървъра" : L"Зарежда…")
                      : !st.hasRunning ? L"Не тече  ·  днес " + clockText(st.todaySeconds, false) +
                                           (st.unattributed ? L"  ·  за разпределяне " + num(st.unattributed) : L"")
                                       : (paused ? L"На пауза  ·  " : L"Тече  ·  ") + what(st.running.cardTitle, st.running.clientName);
  ui.text(RectF(r.l + 16, r.t + 40, r.r - 390, r.t + 58), line, 11.5f, P.dim);
  if (running) ui.animating = true;

  float bh = 34, by = r.cy() - bh / 2, bw = 116, gap = 8;
  if (!st.hasRunning) {
    bool blocked = !st.pending.empty() || (st.maxUnassigned > 0 && st.unattributed >= st.maxUnassigned);
    RectF b(r.r - 14 - 190, by, r.r - 14, by + bh);
    if (ui.button(ui.id("start"), b, L"Пусни без карта", Btn::Accent, kPlay, a.haveState && st.canTrack && !a.busy && !blocked))
      after([] { app().startTimer(); });
    if (blocked)
      ui.tooltip(b, !st.pending.empty() ? L"Първо решете спряното време (✓ или ✕)."
                                        : L"Стигнат е таванът на неразпределените записи — разпределете ги в браузъра.");
  } else {
    RectF b3(r.r - 14 - bw, by, r.r - 14, by + bh), b2(b3.l - gap - bw, by, b3.l - gap, by + bh), b1(b2.l - gap - bw, by, b2.l - gap, by + bh);
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
  }
  float y = r.b;
  if (!st.pending.empty()) {
    const Pending& p = st.pending.front();
    RectF row(r.l, y + 6, r.r, y + 40);
    ui.card(row, 8);
    std::wstring t = L"Чака решение: " + clockText(p.seconds, false) + L"  ·  " + what(p.cardTitle, p.clientName);
    if (st.pending.size() > 1) t += L"   (още " + num((int)st.pending.size() - 1) + L")";
    ui.text(RectF(row.l + 12, row.t, row.r - 92, row.b), t, 12.f, P.text);
    int id = p.id;
    if (ui.iconButton(ui.id("ok", id), RectF(row.r - 84, row.t + 3, row.r - 48, row.b - 3), kCheck, L"Запиши (✓)", !a.busy))
      after([id] { app().confirmPending(id); });
    if (ui.iconButton(ui.id("no", id), RectF(row.r - 44, row.t + 3, row.r - 8, row.b - 3), kCancel, L"Отхвърли (✕)", !a.busy))
      after([id] { app().discardPending(id); });
    y = row.b;
  }
  return y;
}

// The summary table of the unified board — the same numbers as in the browser — and, under it, the cards of the
// row or cell that was clicked.
void Panel::paintSummary(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Summary& s = a.summary;
  if (!s.loaded) {
    std::wstring t = !a.summaryError.empty() ? a.summaryError : a.summaryBusy ? L"Зарежда таблицата…" : L"Таблицата се зарежда при отваряне.";
    ui.text(RectF(area.l, area.t + 10, area.r, area.t + 60), t, 12.5f, a.summaryError.empty() ? P.dim : P.danger, 400, Align::Left,
            VAlign::Top, true);
    return;
  }
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const Color amber = P.dark ? Color::hex(0xFBBF24) : Color::hex(0xB45309);

  // Column widths: the numbers fixed, the name takes the rest.
  std::vector<float> cw;
  for (auto& c : s.layout) cw.push_back(c.key == "exec" ? 84.f : c.key == "plan" ? 62.f : 66.f);
  float fixed = 0;
  for (float w : cw) fixed += w;
  float nameW = area.w() - fixed - 4;
  if (nameW < 150 && fixed > 0) {
    float k = std::max(0.6f, (area.w() - 150 - 4) / fixed);
    for (float& w : cw) w *= k;
    fixed *= k;
    nameW = area.w() - fixed - 4;
  }
  auto colX = [&](size_t i) {
    float xx = area.l + nameW;
    for (size_t k = 0; k < i; k++) xx += cw[k];
    return xx;
  };
  auto statusColor = [&](const std::string& key) {
    for (auto& c : s.layout)
      if (c.key == "status:" + key && c.hasColor) return c.color;
    return P.faint;
  };

  // ---- header
  // Headers wrap onto two lines rather than being cut; a status column's colour is the bar under it.
  float y = area.t, hh = 44;
  ui.text(RectF(area.l + 8, y, area.l + nameW, y + hh - 6), toUpperBg(s.nameHead), 10.5f, P.dim, 600, Align::Left, VAlign::Bottom);
  for (size_t i = 0; i < s.layout.size(); i++) {
    const SumColumn& c = s.layout[i];
    RectF h(colX(i) + 2, y, colX(i) + cw[i] - 2, y + hh);
    float labelBottom = c.sub.empty() ? h.b - 6 : h.b - 17;
    ui.text(RectF(h.l, h.t, h.r, labelBottom), toUpperBg(c.head), 9.f, P.dim, 600, Align::Center, VAlign::Bottom, true);
    if (!c.sub.empty()) ui.text(RectF(h.l, h.b - 17, h.r, h.b - 6), c.sub, 9.f, P.faint, 400, Align::Center);
    if (c.hasColor) ui.p->rrect(RectF(h.cx() - h.w() * 0.3f, h.b - 4, h.cx() + h.w() * 0.3f, h.b - 2), 1, c.color);
  }
  y += hh;
  ui.divider(RectF(area.l, y, area.r, y + 1));

  bool selected = !selRow_.empty();
  float listH = selected ? std::max(150.f, (area.b - y) * 0.46f) : 0.f;
  float tableBottom = area.b - listH - (selected ? 8 : 0);
  float rh = 27;

  auto cellText = [&](const SumRow& r, const SumColumn& c, Color& color, bool& pill) -> std::wstring {
    pill = false;
    color = P.text;
    auto count = [&](int n, Color col, bool asPill) -> std::wstring {
      if (!n) {
        color = P.faint;
        return L"—";
      }
      color = col;
      pill = asPill;
      return num(n);
    };
    if (c.key == "plan") {
      color = r.planSeconds ? P.dim : P.faint;
      return r.planSeconds ? clockText(r.planSeconds, false) : L"—";
    }
    if (c.key.rfind("status:", 0) == 0) {
      auto it = r.byStatus.find(c.key.substr(7));
      return count(it == r.byStatus.end() ? 0 : it->second, c.color, true);
    }
    if (c.key == "approval") return count(r.awaiting, amber, true);
    if (c.key == "done") return count(r.done, green, true);
    if (c.key == "tasks") return count(r.noClient, P.text, false);
    if (c.key == "clients") return count(r.withClient, P.text, false);
    if (c.key == "count") return count(r.key == "done" ? r.done : r.open, P.text, false);
    if (c.key == "exec") {
      std::wstring t;
      for (auto& n : r.assignees) t += (t.empty() ? L"" : L" · ") + initials(n);
      color = t.empty() ? P.faint : P.dim;
      return t.empty() ? L"—" : t;
    }
    return L"";
  };

  auto drawRow = [&](const SumRow& r, RectF rr, bool total, int salt) {
    std::string key = total ? std::string("__total") : r.key;
    bool isSel = selRow_ == key;
    bool hot = ui.hot(rr);
    if (isSel) ui.p->rrect(rr, 6, P.accent.withA(0.14f));
    else if (hot) ui.p->rrect(rr, 6, P.text.withA(0.05f));
    ui.text(RectF(rr.l + 8, rr.t, rr.l + nameW - 6, rr.b), total ? L"ОБЩО" : r.label, 12.5f, P.text, total ? 700 : 400);
    // the name: the whole row; a number: that cell
    RectF nameR(rr.l, rr.t, rr.l + nameW, rr.b);
    if (ui.hot(nameR)) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
    if (ui.press(ui.id("row", salt), nameR)) {
      bool same = selRow_ == key && selCol_.empty();
      selRow_ = same ? "" : key;
      selCol_.clear();
      ui.scrollTo(ui.id("cards"), 0);
    }
    for (size_t i = 0; i < s.layout.size(); i++) {
      const SumColumn& c = s.layout[i];
      RectF cell(colX(i), rr.t, colX(i) + cw[i], rr.b);
      Color col;
      bool pill;
      std::wstring t = cellText(r, c, col, pill);
      if (isSel && selCol_ == c.key) ui.p->rrectStroke(cell.inset(3, 3), 5, P.accent, 1.4f);
      if (pill) {
        float tw = ui.textWidth(t, 12.f, 600) + 14;
        RectF pr(cell.cx() - tw / 2, cell.cy() - 9, cell.cx() + tw / 2, cell.cy() + 9);
        ui.p->rrect(pr, 9, col.withA(0.18f));
        ui.text(pr, t, 12.f, P.dark ? col : mix(col, P.text, 0.25f), 600, Align::Center);
      } else {
        ui.text(cell, t, 12.f, col, total ? 700 : 400, Align::Center);
      }
      bool clickable = c.key != "exec" && c.key != "plan" && t != L"—";
      if (clickable && ui.hot(cell)) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
      if (clickable && ui.press(ui.id("cell", salt * 64 + (int)i), cell)) {
        bool same = selRow_ == key && selCol_ == c.key;
        selRow_ = same ? "" : key;
        selCol_ = same ? "" : c.key;
        ui.scrollTo(ui.id("cards"), 0);
      }
    }
  };

  drawRow(s.total, RectF(area.l, y + 2, area.r, y + 2 + rh), true, 0);
  y += rh + 4;
  ui.divider(RectF(area.l, y - 1, area.r, y));
  RectF scrollR(area.l, y, area.r, tableBottom);
  float off = ui.beginScroll(ui.id("rows"), scrollR, s.rows.size() * rh);
  for (size_t i = 0; i < s.rows.size(); i++) {
    RectF rr(area.l, y + off + i * rh, area.r, y + off + (i + 1) * rh);
    if (rr.b < scrollR.t || rr.t > scrollR.b) continue;
    drawRow(s.rows[i], rr, false, (int)i + 1);
  }
  ui.endScroll();
  if (!selected) return;

  // ---- the cards behind the selection
  RectF list(area.l, tableBottom + 8, area.r, area.b);
  std::vector<const SumCard*> cards;
  for (auto& k : s.cards)
    if (cardInCell(s, k, selRow_, selCol_)) cards.push_back(&k);
  std::wstring rowLabel = L"ОБЩО", colLabel;
  for (auto& r : s.rows)
    if (r.key == selRow_) rowLabel = r.label;
  for (auto& c : s.layout)
    if (c.key == selCol_) colLabel = c.head + (c.sub.empty() ? L"" : L" " + c.sub);
  ui.text(RectF(list.l + 4, list.t, list.r - 40, list.t + 24),
          rowLabel + (colLabel.empty() ? L"" : L"  ·  " + colLabel) + L"  —  " + num((int)cards.size()) + L" карти", 12.5f, P.text, 600);
  if (ui.iconButton(ui.id("clearsel"), RectF(list.r - 32, list.t - 2, list.r, list.t + 26), kCancel, L"Затвори списъка")) {
    selRow_.clear();
    selCol_.clear();
  }
  RectF cardsR(list.l, list.t + 28, list.r, list.b);
  float ch = 46;
  float coff = ui.beginScroll(ui.id("cards"), cardsR, cards.size() * ch);
  const TimeState& st = a.st;
  for (size_t i = 0; i < cards.size(); i++) {
    const SumCard& k = *cards[i];
    RectF cr(cardsR.l, cardsR.t + coff + i * ch, cardsR.r, cardsR.t + coff + (i + 1) * ch - 4);
    if (cr.b < cardsR.t || cr.t > cardsR.b) continue;
    ui.card(cr, 8);
    ui.p->rrect(RectF(cr.l + 4, cr.t + 6, cr.l + 7, cr.b - 6), 1.5f, k.closed ? green : statusColor(k.status));
    const std::wstring& title = k.title;  // already begins with the activity's code
    ui.text(RectF(cr.l + 16, cr.t + 3, cr.r - 124, cr.t + 22), title, 12.5f, P.text, 600);
    std::wstring sub = k.clientName.empty() ? L"задача" : k.clientName;
    if (!k.dueDate.empty()) sub += L"  ·  срок " + shortDate(k.dueDate);
    if (!k.lane.empty()) sub += L"  ·  " + k.lane;
    if (!k.worker.empty()) sub += L"  ·  " + k.worker;
    if (k.awaiting) sub += L"  ·  за одобрение";
    ui.text(RectF(cr.l + 16, cr.t + 21, cr.r - 124, cr.b - 3), sub, 11.f, P.dim);
    if (k.closed) continue;
    int id = k.id;
    RectF b(cr.r - 112, cr.cy() - 15, cr.r - 10, cr.cy() + 15);
    if (st.hasRunning && st.running.cardId == id) {
      ui.text(RectF(cr.r - 112, cr.t, cr.r - 12, cr.b), L"⏱ тече", 12.f, green, 600, Align::Right);
    } else if (st.hasRunning) {
      if (ui.button(ui.id("attach", id), b, L"Закачи", Btn::Subtle, kAttach, !a.busy && st.canTrack)) after([id] { app().startTimerOnCard(id); });
      ui.tooltip(b, L"Пуснатият хронометър отива на тази карта");
    } else {
      if (ui.button(ui.id("go", id), b, L"Пусни", Btn::Standard, kPlay, !a.busy && st.canTrack && st.pending.empty()))
        after([id] { app().startTimerOnCard(id); });
    }
  }
  ui.endScroll();
  if (cards.empty()) ui.text(cardsR, L"Няма карти тук.", 12.f, P.faint, 400, Align::Center, VAlign::Top);
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

  if (!a.loginError.empty()) ui.text(RectF(x, y, x + cw, y + 40), a.loginError, 12, P.danger, 400, Align::Left, VAlign::Top, true);
  y += 46;
  bool ready = !a.loginBusy && !trim(server_).empty() && !trim(user_).empty() && !pass_.empty();
  if (ui.button(ui.id("go"), RectF(x, y, x + cw, y + 38), a.loginBusy ? L"Влизане…" : L"Вход", Btn::Accent, nullptr, ready) || (entered && ready))
    submit();
}

}  // namespace kd
