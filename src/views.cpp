#include "views.h"

#include "app.h"

#include <algorithm>

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
constexpr const wchar_t* kDownload = L"\xE896";
constexpr const wchar_t* kStopwatch = L"\xE916";
constexpr const wchar_t* kSync = L"\xE895";
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
      wchar_t u = c;
      CharUpperBuffW(&u, 1);
      out += u;
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
  // Opaque, not acrylic: more contrast (the user, 2026-11-26), and the same ground as the rail beside it.
  return createPopup(inst, L"KanbanDesktopPanel", at, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 0);
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
  std::wstring where = a.tab == App::Tab::Calendar   ? L"Календар"
                      : a.tab == App::Tab::Clients  ? L"Клиенти"
                      : a.tab == App::Tab::AdminDay ? L"Административни дейности"
                      : a.tab == App::Tab::Profile  ? L"Профил и настройки"
                      : a.summary.loaded            ? a.summary.subviewLabel
                                                    : L"";
  if (!where.empty()) who += (who.empty() ? L"" : L"  ·  ") + where;
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
  y = paintUnassigned(RectF(x, y, x + cw, H));
  RectF content(x, y + 10, x + cw, H - 46);
  switch (a.tab) {
    case App::Tab::Calendar: paintCalendar(content); break;
    case App::Tab::Clients: paintClients(content); break;
    case App::Tab::AdminDay: paintAdminDay(content); break;
    case App::Tab::Profile: paintProfile(content); break;
    default: paintSummary(content); break;
  }

  // ---- footer: what is going on (update, connection) · the update button · the browser · the version
  const float fy = H - 40, fb = H - 8;
  float right = x + cw;
  ui.text(RectF(right - 80, fy, right, fb), L"версия " + widen(KD_VERSION_STR), 11.f, P.faint, 400, Align::Right);
  right -= 88;
  if (ui.link(ui.id("browser"), RectF(right - 214, fy, right, fb), L"Отвори приложението в браузъра", 12.f))
    after([] { app().openBrowser(); });
  right -= 226;
  // The update by hand: asks GitHub, and installs a newer version at once — the app closes and comes back by itself.
  // Blue when the regular check has already found one.
  bool newer = !a.updateVersion.empty();
  std::wstring ul = a.updateBusy ? L"Обновява се…" : newer ? L"Обнови до " + widen(a.updateVersion) : L"Провери за обновление";
  float uw = ui.textWidth(ul, 13.5f, newer ? 600 : 400) + 18 + 7 + 28;
  RectF ub(right - uw, fy + 2, right, fb - 2);
  if (ui.button(ui.id("update"), ub, ul, newer ? Btn::Accent : Btn::Standard, newer ? kDownload : kSync, !a.updateBusy))
    after([] { app().checkForUpdate(true); });
  RectF note(x, fy, ub.l - 12, fb);
  if (!a.updateNote.empty())
    ui.text(note, a.updateNote, 11.5f, a.updateBusy ? P.accent : P.dim, 400, Align::Left, VAlign::Middle, true);
  else if (!a.error.empty() || a.offline)
    ui.text(note, a.error.empty() ? L"Няма връзка със сървъра — опитваме отново." : a.error, 11.5f, P.danger, 400, Align::Left,
            VAlign::Middle, true);
}

// The stopwatch as one strip: clock, what it is for, the buttons; stopped time waiting for ✓ / ✕ under it.
float Panel::paintTimer(RectF r) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const TimeState& st = a.st;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const Color amber = P.dark ? Color::hex(0xFBBF24) : Color::hex(0xB45309);
  bool running = st.hasRunning && !st.running.paused, paused = st.hasRunning && st.running.paused;
  // Idle, the stopwatch is one slim line: with the table, „Пусни“ on a card is the usual start, and „Без карта“ stays
  // for work that has no card yet (the user, 2026-11-26: "after adding the summary the stopwatch without an item
  // becomes a bit useless"). Running, it is the big clock and its buttons.
  r.b = r.t + (st.hasRunning ? 64.f : 46.f);
  // Its colour says its state at a glance (the user, 2026-11-26): green while it runs, amber on pause, plain when idle.
  if (st.hasRunning) {
    Color c = running ? green : amber;
    ui.p->rrect(r, 10, c.withA(P.dark ? 0.20f : 0.12f));
    ui.p->rrectStroke(r.inset(0.75f), 10, c.withA(0.70f), 1.5f);
    ui.p->rrect(RectF(r.l + 3, r.t + 6, r.l + 9, r.b - 6), 3.f, c);
  } else {
    ui.card(r, 10);
  }
  float bh = 34, by = r.cy() - bh / 2, bw = 116, gap = 8;
  if (!st.hasRunning) {
    // No stopwatch without a card any more (the user, 2026-11-26): it starts on a card — „Пусни“ in the table.
    std::wstring line = !a.haveState ? (a.offline ? L"Няма връзка със сървъра" : L"Зарежда…")
                                     : L"Хронометърът не тече  ·  днес " + clockText(st.todaySeconds, false) +
                                           (st.unattributed ? L"  ·  за разпределяне " + num(st.unattributed) : L"");
    ui.glyph(RectF(r.l + 12, r.t, r.l + 36, r.b), kStopwatch, 15.f, P.dim);
    ui.text(RectF(r.l + 40, r.t, r.r - 330, r.b), line, 12.5f, P.text);
    ui.text(RectF(r.r - 320, r.t, r.r - 16, r.b), L"Пуска се от картата: „Пусни“ в таблицата или календара", 11.5f, P.dim, 400, Align::Right);
  } else {
    int secs = a.runningSeconds();
    ui.text(RectF(r.l + 16, r.t + 6, r.l + 190, r.t + 40), clockText(secs, true), 26, running ? green : amber, 600);
    std::wstring line = (paused ? L"На пауза  ·  " : L"Тече  ·  ") + what(st.running.cardTitle, st.running.clientName);
    ui.text(RectF(r.l + 16, r.t + 40, r.r - 390, r.t + 58), line, 11.5f, P.dim);
    if (running) ui.animating = true;
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
    // ✓ was refused for want of the card's choice or Дейност + Коментар: asked for here, then ✓ again with them.
    auto pn = a.pendingNeeds.find(id);
    if (pn != a.pendingNeeds.end()) {
      RectF box(r.l, y + 4, r.r, y + 4 + needsHeight(pn->second) + 10);
      ui.card(box, 8);
      bool send = false, dismiss = false;
      needsForm(RectF(box.l + 12, box.t + 4, box.r - 8, box.b - 4), 100000 + id, pn->second, pendingForm_[id], L"Запиши", kCheck, !a.busy,
                &send, &dismiss);
      if (send) {
        AttachNeeds answer = pendingForm_[id];
        after([id, answer] { app().confirmPending(id, &answer); });
      }
      if (dismiss) after([id] { app().dismissPendingNeeds(id); });
      y = box.b;
    }
  }
  return y;
}

// „What the server still wants“ before it takes time on a card (422 time_detail_required): the activity's option choice
// (the defaults already ticked) and/or, on a project task, „Дейност“ + „Коментар“. Any other refusal: its message only.
float Panel::needsHeight(const AttachNeeds& need) {
  return 32.f + (need.options.empty() ? 0.f : 38.f) + (need.needsDetail ? 38.f : 0.f);
}

float Panel::needsForm(RectF r, int key, const AttachNeeds& need, AttachNeeds& form, const wchar_t* label, const wchar_t* icon,
                       bool enabled, bool* send, bool* dismiss) {
  const UiPalette& P = ui.pal;
  if (form.code != need.code || form.message != need.message || form.options.size() != need.options.size()) form = need;  // a new answer
  float y = r.t;
  ui.text(RectF(r.l, y, r.r - 36, y + 30), need.message, 11.5f, P.danger, 400, Align::Left, VAlign::Middle, true);
  if (ui.iconButton(ui.id("ndismiss", key), RectF(r.r - 30, y + 1, r.r, y + 29), kCancel, L"Скрий")) *dismiss = true;
  y += 32;
  bool hasForm = !need.options.empty() || need.needsDetail;
  bool chosenOk = need.options.empty() || (form.multiSelect ? !form.chosen.empty() : form.chosen.size() == 1);
  bool detailOk = !need.needsDetail || (!trim(form.activity).empty() && !trim(form.comment).empty());
  RectF b(r.r - 130, 0, r.r, 0);
  if (!need.options.empty()) {
    float x = r.l;
    for (auto& o : need.options) {
      bool on = std::find(form.chosen.begin(), form.chosen.end(), o.id) != form.chosen.end();
      float w = ui.textWidth(o.name, 12.f, 600) + 26;
      RectF chip(x, y + 3, std::min(x + w, r.r - 140), y + 31);
      bool hot = ui.hot(chip);
      ui.p->rrect(chip, 7, on ? P.accent : P.text.withA(hot ? 0.09f : 0.05f));
      ui.text(chip, o.name, 12.f, on ? P.onAccent : P.text, 600, Align::Center);
      if (!o.hint.empty()) ui.tooltip(chip, o.hint);
      if (hot) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
      if (ui.press(ui.id("opt", key * 64 + o.id), chip)) {
        if (form.multiSelect) {
          if (on) form.chosen.erase(std::remove(form.chosen.begin(), form.chosen.end(), o.id), form.chosen.end());
          else form.chosen.push_back(o.id);
        } else {
          form.chosen = {o.id};
        }
      }
      x = chip.r + 6;
    }
    b = RectF(r.r - 130, y + 3, r.r, y + 31);
    y += 38;
  }
  if (need.needsDetail) {
    float mid = r.l + (r.w() - 140) * 0.42f;
    std::wstring hintA = need.maxChars > 0 ? L"Дейност (до " + num(need.maxChars) + L" знака)" : L"Дейност";
    ui.textBox(ui.id("act", key), RectF(r.l, y + 3, mid - 6, y + 31), form.activity, hintA);
    ui.textBox(ui.id("com", key), RectF(mid, y + 3, r.r - 140, y + 31), form.comment, L"Коментар");
    b = RectF(r.r - 130, y + 3, r.r, y + 31);
    y += 38;
  }
  if (hasForm && ui.button(ui.id("nsend", key), b, label, Btn::Accent, icon, enabled && chosenOk && detailOk)) *send = true;
  return y;
}

// „За разпределяне“: time recorded without a card (a stopwatch started here without one, stopped, ✓). The browser has
// no screen for it any more, so it is done here: pick an entry, then a card in the table — „Сложи тук“.
float Panel::paintUnassigned(RectF r) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const TimeState& st = a.st;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  bool note = !a.attachNote.empty() && nowSec() < a.attachNoteUntil;
  if (st.unassigned.empty() && !note) return r.t;
  float y = r.t + 8;
  if (note) {
    ui.text(RectF(r.l + 4, y, r.r, y + 20), L"✓ Закачено: " + a.attachNote, 12.f, green, 600);
    y += 22;
  }
  if (st.unassigned.empty()) return y;
  int n = (int)st.unassigned.size();
  ui.text(RectF(r.l + 4, y, r.l + 320, y + 22), L"За разпределяне  ·  " + num(n) + (n == 1 ? L" запис" : L" записа"), 12.5f, P.text, 600);
  std::wstring hint = st.hasRunning ? L"Спрете хронометъра, за да разпределите: докато тече, друго време не се пипа."
                      : a.armedEntry ? L"Сега изберете карта: ред или число в таблицата, после „Сложи тук“."
                                     : L"Изберете запис, после карта в таблицата.";
  ui.text(RectF(r.l + 320, y, r.r - 4, y + 22), hint, 11.5f, a.armedEntry && !st.hasRunning ? P.accent : P.dim, 400, Align::Right);
  y += 26;
  int shown = 0;
  for (auto& u : st.unassigned) {
    if (shown == 4) break;  // the cap is usually 3; more go one line below
    RectF row(r.l, y, r.r, y + 34);
    bool armed = a.armedEntry == u.id;
    ui.card(row, 8);
    if (armed) ui.p->rrectStroke(row.inset(0.75f), 8, P.accent, 1.5f);
    std::wstring t = shortDate(u.workDate) + L"  ·  " + clockText(u.seconds, false) + (u.note.empty() ? L"" : L"  ·  " + u.note);
    ui.text(RectF(row.l + 12, row.t, row.r - 160, row.b), t, 12.f, P.text);
    RectF b(row.r - 150, row.t + 3, row.r - 6, row.b - 3);
    int id = u.id;
    if (armed) {
      if (ui.button(ui.id("disarm", id), b, L"Отказ", Btn::Subtle, kCancel)) after([] { app().armEntry(0); });
    } else if (ui.button(ui.id("armit", id), b, L"Избери карта", Btn::Standard, kAttach, !st.hasRunning)) {
      after([id] { app().armEntry(id); });
    }
    y = row.b + 4;
    shown++;
  }
  if (n > shown) {
    ui.text(RectF(r.l + 4, y, r.r, y + 18), L"още " + num(n - shown) + L" — след тези", 11.f, P.faint);
    y += 20;
  }
  return y;
}

// The summary table of the unified board — the same numbers as in the browser — and, under it, the cards of the
// row or cell that was clicked.
void Panel::paintSummary(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Summary& s = a.summary;
  if (s.subviews.size() > 1) area.t = paintViews(RectF(area.l, area.t, area.r, area.t + 30)) + 10;
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
  for (auto& c : s.layout) cw.push_back(c.key == "exec" ? 84.f : c.key == "plan" ? 62.f : c.key == "cx" ? 40.f : c.key == "au" ? 56.f : 66.f);
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

  // A row or a number chosen → the table collapses to that row (its numbers stay clickable) and its cards take the
  // room. Its name again, or ✕ over the list, brings the whole table back.
  bool selected = !selRow_.empty();
  int selIndex = -1;
  if (selected && selRow_ != "__total") {
    for (size_t i = 0; i < s.rows.size(); i++)
      if (s.rows[i].key == selRow_) selIndex = (int)i;
    if (selIndex < 0) {  // gone with a refresh (e.g. its last card was closed)
      selRow_.clear();
      selCol_.clear();
      selected = false;
    }
  }
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
    if (c.key == "cx") {
      color = r.complexity.empty() ? P.faint : P.text;
      return r.complexity.empty() ? L"—" : r.complexity;
    }
    if (c.key == "au") {
      color = r.autonomy < 0 ? P.faint : P.text;
      return r.autonomy < 0 ? L"—" : num(r.autonomy) + L"%";
    }
    if (c.key == "exec") {
      std::wstring t;
      for (auto& n : r.assignees) t += (t.empty() ? L"" : L" · ") + initials(n);
      color = t.empty() ? P.faint : P.dim;
      return t.empty() ? L"—" : t;
    }
    return L"";
  };

  // „сл.“ / „самост.“: what the browser's hover says (ClientAssessment.tsx) — the value, then the nomenclature's text.
  auto assessTip = [&](const SumRow& r, const std::string& key) -> std::wstring {
    if (key == "cx") {
      if (r.complexity.empty()) return L"Нивото на сложност не е зададено за този клиент.";
      if (r.complexityHint.empty()) return L"Сложност " + r.complexity + L" — няма описание в номенклатурата.";
      return L"Сложност " + r.complexity + L"\n\n" + r.complexityHint;
    }
    if (r.autonomy < 0) return L"Очакваната самостоятелност не е зададена за този клиент.";
    if (r.autonomyWindow.empty()) return L"Няма прозорец за " + num(r.autonomy) + L" (Админ → Самостоятелност).";
    std::wstring head = r.autonomyWindow + L"  ·  " + num(r.autonomy) + L"%  ·  прозорец " + r.autonomyRange;
    return r.autonomyHint.empty() ? head + L" — няма описание в номенклатурата." : head + L"\n\n" + r.autonomyHint;
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
      if (!total && (c.key == "cx" || c.key == "au") && r.key != "none") ui.tooltip(cell, assessTip(r, c.key));
      bool clickable = c.key != "exec" && c.key != "plan" && c.key != "cx" && c.key != "au" && t != L"—";
      if (clickable && ui.hot(cell)) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
      if (clickable && ui.press(ui.id("cell", salt * 64 + (int)i), cell)) {
        bool same = selRow_ == key && selCol_ == c.key;
        selRow_ = same ? "" : key;
        selCol_ = same ? "" : c.key;
        ui.scrollTo(ui.id("cards"), 0);
      }
    }
  };

  if (!selected) {
    drawRow(s.total, RectF(area.l, y + 2, area.r, y + 2 + rh), true, 0);
    y += rh + 4;
    ui.divider(RectF(area.l, y - 1, area.r, y));
    RectF scrollR(area.l, y, area.r, area.b);
    float off = ui.beginScroll(ui.id("rows"), scrollR, s.rows.size() * rh);
    for (size_t i = 0; i < s.rows.size(); i++) {
      RectF rr(area.l, y + off + i * rh, area.r, y + off + (i + 1) * rh);
      if (rr.b < scrollR.t || rr.t > scrollR.b) continue;
      if (i % 2 == 1) ui.p->rrect(rr, 4, P.text.withA(P.dark ? 0.035f : 0.03f));  // every other row: easier to follow across
      drawRow(s.rows[i], rr, false, (int)i + 1);
    }
    ui.endScroll();
    return;
  }
  RectF only(area.l, y + 2, area.r, y + 2 + rh);
  if (selIndex < 0) drawRow(s.total, only, true, 0);
  else drawRow(s.rows[(size_t)selIndex], only, false, selIndex + 1);
  ui.tooltip(RectF(only.l, only.t, only.l + nameW, only.b), L"Щракнете отново — цялата таблица");
  y += rh + 4;
  ui.divider(RectF(area.l, y - 1, area.r, y));
  if (selRow_.empty()) return;  // that click just closed it: the whole table from the next frame

  // ---- the cards behind the selection
  RectF list(area.l, y + 8, area.r, area.b);
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
  if (ui.iconButton(ui.id("clearsel"), RectF(list.r - 32, list.t - 2, list.r, list.t + 26), kCancel, L"Затвори списъка — цялата таблица")) {
    selRow_.clear();
    selCol_.clear();
  }
  paintCards(RectF(list.l, list.t + 28, list.r, list.b), cards, ui.id("cards"));
}

// A list of cards: the lane menu, „Пусни“ / „Закачи“ / „Сложи тук“, the period, the urgency bar (days left), and under
// a card the server's questions (attach form, a refused move, „№ вх. док.“). The table's selection and a client's.
void Panel::paintCards(RectF cardsR, const std::vector<const SumCard*>& cards, uint32_t scrollId) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  // The collector's lanes, named as on the board. Choosing one moves the card there — the web's move, with its rules.
  static const char* kLaneKeys[] = {"upcoming", "in_progress", "review", "done"};
  static const std::vector<std::wstring> kLaneNames = {L"Предстоящи", L"В процес", L"За одобрение", L"Приключени"};
  auto laneIndex = [](const std::string& key) {
    for (int i = 0; i < 4; i++)
      if (key == kLaneKeys[i]) return i;
    return 0;
  };
  // Under a card: the attach form (what the server wants for the entry), else why a move was refused.
  auto extraOf = [&](const SumCard& k) -> float {
    if (k.closed) return 0.f;
    auto at = a.attachProblems.find(k.id);
    if (at != a.attachProblems.end()) return needsHeight(at->second) + 6.f;
    return a.cardProblems.count(k.id) ? 40.f : 0.f;
  };
  auto heightOf = [&](const SumCard& k) { return 50.f + extraOf(k); };
  float contentH = 0;
  for (auto* k : cards) contentH += heightOf(*k);
  float coff = ui.beginScroll(scrollId, cardsR, contentH);
  const TimeState& st = a.st;
  float cy = cardsR.t + coff;
  for (const SumCard* kp : cards) {
    const SumCard& k = *kp;
    float h = heightOf(k);
    RectF cr(cardsR.l, cy, cardsR.r, cy + h - 4);
    cy += h;
    if (cr.b < cardsR.t || cr.t > cardsR.b) continue;
    ui.card(cr, 8);
    RectF top(cr.l, cr.t, cr.r, cr.t + 46);
    // The urgency colour (days left) as a bar the whole height of the card — 7 px, to be seen (the user, 2026-11-26).
    ui.p->rrect(RectF(cr.l + 2, cr.t + 3, cr.l + 9, cr.b - 3), 3.f, k.hasBand ? k.band : k.closed ? green : statusColorOf(k.status));
    const float textR = k.closed ? top.r - 12 : top.r - 282;
    // The period „[10.2026]“ at the right of the title, bold — as on the browser's card; stronger when carried over (⏮).
    float pw = k.periodTag.empty() ? 0.f : ui.textWidth(k.periodTag, 12.f, 700) + 6;
    if (pw > 0) {
      RectF pr(textR - pw, top.t + 3, textR, top.t + 22);
      ui.text(pr, k.periodTag, 12.f, k.carryOver ? P.text : P.dim, 700, Align::Right);
      if (!k.periodHint.empty()) ui.tooltip(pr, k.periodHint);
    }
    ui.text(RectF(top.l + 20, top.t + 3, textR - pw - 8, top.t + 22), k.title, 12.5f, P.text, 600);  // already begins with the activity's code
    std::wstring sub = k.clientName.empty() ? L"задача" : k.clientName;
    if (!k.dueDate.empty()) sub += L"  ·  срок " + shortDate(k.dueDate);
    if (k.closed) sub += L"  ·  приключена";
    if (!k.worker.empty()) sub += L"  ·  " + k.worker;
    if (k.awaiting) sub += L"  ·  чака одобрение";
    if (k.notYetVisible && !k.visibleFrom.empty()) sub += L"  ·  на дъските от " + shortDate(k.visibleFrom);
    ui.text(RectF(top.l + 20, top.t + 21, textR, top.b - 3), sub, 11.f, P.dim);
    if (k.closed) continue;
    int id = k.id;

    RectF lr(top.r - 272, top.cy() - 15, top.r - 120, top.cy() + 15);
    if (a.movingCards.count(id)) {
      ui.text(lr, L"мести се…", 12.f, P.dim, 400, Align::Center);
    } else if (k.laneKey.empty()) {
      // A server older than web CHANGELOG 332 does not say which lane the card is in: its name, and no picker — a
      // picker would have to guess the current lane, and a wrong guess moves a card nobody meant to move.
      ui.text(lr, k.lane, 12.f, P.dim, 400, Align::Center);
    } else {
      int idx = laneIndex(k.laneKey);
      if (ui.combo(ui.id("lane", id), lr, idx, kLaneNames) && kLaneKeys[idx] != k.laneKey) {
        std::string lane = kLaneKeys[idx];
        after([id, lane] { app().moveCard(id, lane); });
      }
    }

    RectF b(top.r - 112, top.cy() - 15, top.r - 10, top.cy() + 15);
    if (a.armedEntry) {  // attaching: every open card can take the picked entry
      bool busy = a.attachingCards.count(id) > 0;
      if (ui.button(ui.id("put", id), b, busy ? L"Закача…" : L"Сложи тук", Btn::Accent, kAttach, !busy && !st.hasRunning)) {
        int e = a.armedEntry;
        after([e, id] { app().attachEntry(e, id); });
      }
    } else if (st.hasRunning && st.running.cardId == id) {
      ui.text(RectF(top.r - 112, top.t, top.r - 12, top.b), L"⏱ тече", 12.f, green, 600, Align::Right);
    } else if (st.hasRunning) {
      if (ui.button(ui.id("attach", id), b, L"Закачи", Btn::Subtle, kAttach, !a.busy && st.canTrack)) after([id] { app().startTimerOnCard(id); });
      ui.tooltip(b, L"Пуснатият хронометър отива на тази карта");
    } else {
      if (ui.button(ui.id("go", id), b, L"Пусни", Btn::Standard, kPlay, !a.busy && st.canTrack && st.pending.empty()))
        after([id] { app().startTimerOnCard(id); });
    }

    // What the server wants before it takes the picked entry: its message, the choice, „Дейност“ + „Коментар“.
    auto ait = a.attachProblems.find(id);
    if (ait != a.attachProblems.end()) {
      bool sending = a.attachingCards.count(id) > 0, send = false, dismiss = false;
      needsForm(RectF(cr.l + 16, top.b - 2, cr.r - 10, cr.b), id, ait->second, attachForm_[id], sending ? L"Закача…" : L"Сложи", kAttach,
                !sending && a.armedEntry && !st.hasRunning, &send, &dismiss);
      if (send) {
        int e = a.armedEntry;
        AttachNeeds answer = attachForm_[id];
        after([e, id, answer] { app().attachEntry(e, id, &answer); });
      }
      if (dismiss) after([id] { app().dismissAttach(id); });
      continue;
    }

    // Why the server did not move it — and, for a missing „№ вх. док.“, the place to type it.
    auto pit = a.cardProblems.find(id);
    if (pit == a.cardProblems.end()) continue;
    const CardProblem& pb = pit->second;
    RectF pr(cr.l + 16, top.b - 2, cr.r - 10, cr.b - 6);
    if (ui.iconButton(ui.id("dismiss", id), RectF(pr.r - 30, pr.cy() - 14, pr.r, pr.cy() + 14), kCancel, L"Скрий"))
      after([id] { app().dismissProblem(id); });
    if (pb.code == "reference_required") {
      ui.text(RectF(pr.l, pr.t, pr.l + 186, pr.b), L"№ вх. док. за приключване:", 12.f, P.danger, 600);
      std::wstring& ref = refInput_[id];
      bool entered = false;
      ui.textBox(ui.id("ref", id), RectF(pr.l + 190, pr.t + 3, pr.l + 400, pr.b - 3), ref, L"номер", nullptr, &entered);
      bool ok = !trim(ref).empty() && !a.movingCards.count(id);
      if (ui.button(ui.id("closeit", id), RectF(pr.l + 408, pr.t + 3, pr.l + 540, pr.b - 3), L"Приключи", Btn::Accent, kCheck, ok) ||
          (entered && ok)) {
        std::wstring r = trim(ref);
        after([id, r] { app().moveCard(id, "done", r); });
      }
    } else {
      ui.text(RectF(pr.l, pr.t, pr.r - 216, pr.b), pb.message, 11.5f, P.danger, 400, Align::Left, VAlign::Middle, true);
      if (ui.link(ui.id("openweb", id), RectF(pr.r - 206, pr.t, pr.r - 40, pr.b), L"Отвори в браузъра", 12.f))
        after([] { app().openBrowser(); });
    }
  }
  ui.endScroll();
  if (cards.empty()) ui.text(cardsR, L"Няма карти тук.", 12.f, P.faint, 400, Align::Center, VAlign::Top);
}

Color Panel::statusColorOf(const std::string& key) {
  const Summary& s = app().summary;
  if (auto it = s.bandColors.find(key); it != s.bandColors.end()) return it->second;
  for (auto& c : s.layout)
    if (c.key == "status:" + key && c.hasColor) return c.color;
  return ui.pal.faint;
}

// The views of the table, as the web's bar above the board: the executor's („Изп.") first, then the managing
// partner's („УС"). Words instead of the web's icons — the panel is wide enough, and words need no tooltip to be read.
float Panel::paintViews(RectF r) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Summary& s = a.summary;
  auto shortLabel = [](const std::wstring& label) {
    static const std::wstring kPrefix = L"Задачи по ";
    if (label.rfind(kPrefix, 0) != 0 || label.size() == kPrefix.size()) return label;
    std::wstring t = label.substr(kPrefix.size());
    CharUpperBuffW(t.data(), 1);  // towupper leaves Cyrillic alone in this C runtime
    return t;
  };
  float x = r.l;
  bool first = true;
  for (const char* set : {"executor", "partner"}) {
    bool any = false;
    for (auto& v : s.subviews) any = any || v.set == set;
    if (!any) continue;
    if (!first) {
      ui.p->rrect(RectF(x + 6, r.t + 7, x + 7, r.b - 7), 0.5f, P.stroke);
      x += 14;
    }
    first = false;
    bool exec = std::string(set) == "executor";
    std::wstring head = exec ? L"Изп." : L"УС";
    float hw = ui.textWidth(head, 10.5f, 600) + 8;
    ui.text(RectF(x, r.t, x + hw, r.b), head, 10.5f, P.faint, 600);
    ui.tooltip(RectF(x, r.t, x + hw, r.b), exec ? L"Справки в роля Изпълнител" : L"Справки в роля Управляващ съдружник");
    x += hw;
    for (size_t vi = 0; vi < s.subviews.size(); vi++) {
      const SubviewTab& v = s.subviews[vi];
      if (v.set != set) continue;
      std::wstring t = shortLabel(v.label);
      float w = ui.textWidth(t, 12.f, 600) + 22;
      if (x + w > r.r) break;  // a narrow screen: what fits
      RectF b(x, r.t, x + w, r.b);
      bool on = v.id == s.subviewId;
      bool hot = ui.hot(b);
      if (on) ui.p->rrect(b, 7, P.accent);
      else ui.p->rrect(b, 7, P.text.withA(hot ? 0.08f : 0.04f));
      ui.text(b, t, 12.f, on ? P.onAccent : P.text, 600, Align::Center);
      ui.tooltip(b, v.label + (exec ? L" — Изпълнител" : L" — Управляващ съдружник"));
      if (hot && !on) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
      if (ui.press(ui.id("view", (int)vi), b) && !on) {
        selRow_.clear();
        selCol_.clear();
        ui.scrollTo(ui.id("rows"), 0);
        std::string id = v.id;
        after([id] { app().chooseSubview(id); });
      }
      x += w + 6;
    }
  }
  return r.b;
}

// ================================================================ the rail
bool Rail::create(HINSTANCE inst, RECT at) {
  return createPopup(inst, L"KanbanDesktopRail", at, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, 0);
}

void Rail::showAt(RECT at) {
  SetWindowPos(hwnd_, HWND_TOPMOST, at.left, at.top, at.right - at.left, at.bottom - at.top, SWP_SHOWWINDOW | SWP_NOACTIVATE);
  invalidate();
}

bool Rail::onClose() {
  app().hidePanel();
  return false;
}

LRESULT Rail::onMessage(UINT m, WPARAM, LPARAM, bool& handled) {
  if (m == WM_MOUSEACTIVATE) {  // clicks, but no activation: the panel keeps it, and stays open
    handled = true;
    return MA_NOACTIVATE;
  }
  handled = false;
  return 0;
}

void Rail::paint() {
  App& a = app();
  const UiPalette& P = ui.pal;
  const float W = ui.view.w();
  struct Item {
    App::Tab tab;
    const wchar_t* icon;
    const wchar_t* label;
  };
  static const Item kItems[] = {
      {App::Tab::Table, L"\xE8FD", L"Таблица"},
      {App::Tab::Calendar, L"\xE787", L"Календар"},
      {App::Tab::Clients, L"\xE716", L"Клиенти"},
      {App::Tab::AdminDay, L"\xE823", L"Админ. дейности"},
      {App::Tab::Profile, L"\xE77B", L"Профил"},
  };
  float y = 12;
  int i = 0;
  for (auto& it : kItems) {
    RectF b(6, y, W - 6, y + 72);
    bool on = a.tab == it.tab;
    bool hot = ui.hot(b);
    if (on) {
      ui.p->rrect(b, 8, P.accent.withA(P.dark ? 0.22f : 0.14f));
      ui.p->rrect(RectF(b.l, b.t + 14, b.l + 3, b.b - 14), 1.5f, P.accent);
    } else if (hot) {
      ui.p->rrect(b, 8, P.text.withA(0.06f));
    }
    Color fg = on ? (P.dark ? P.accent : mix(P.accent, P.text, 0.15f)) : P.text;
    ui.glyph(RectF(b.l, b.t + 8, b.r, b.t + 32), it.icon, 18.f, fg);
    ui.text(RectF(b.l + 2, b.t + 34, b.r - 2, b.b), it.label, 11.f, fg, on ? 600 : 400, Align::Center, VAlign::Top, true);  // two lines
    if (hot && !on) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
    App::Tab t = it.tab;
    if (ui.press(ui.id("tab", i++), b) && !on) after([t] { app().setTab(t); });
    y = b.b + 6;
  }
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
