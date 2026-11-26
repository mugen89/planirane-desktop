// The panel's tabs besides the table — Клиенти, Административни дейности, Профил (the user, 2026-11-26: "a strip on
// the left … with option to access clients, profile settings, administrative tasks"). What they show comes from the
// server's own endpoints, as the browser's screens read them; the desktop only draws it and sends the person's input.
#include "views.h"

#include "app.h"
#include "install.h"

#include <algorithm>

namespace kd {
using namespace rt;

namespace {
constexpr const wchar_t* kSearch = L"\xE721";
constexpr const wchar_t* kReload = L"\xE72C";
constexpr const wchar_t* kPrev = L"\xE76B";
constexpr const wchar_t* kNext = L"\xE76C";
constexpr const wchar_t* kLock = L"\xE72E";
constexpr const wchar_t* kSave = L"\xE74E";
constexpr const wchar_t* kClose = L"\xE711";
constexpr const wchar_t* kSync = L"\xE895";
constexpr const wchar_t* kSignOut = L"\xF3B1";

std::wstring lower(std::wstring s) {
  if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
  return s;
}
std::wstring upper(std::wstring s) {
  if (!s.empty()) CharUpperBuffW(s.data(), (DWORD)s.size());
  return s;
}
std::wstring num(int n) { return std::to_wstring(n); }
std::wstring hm(int seconds) { return clockText(seconds, false); }

// ---- dates as the server writes them ("2026-11-26")
bool parseIso(const std::string& iso, SYSTEMTIME& t) {
  if (iso.size() < 10) return false;
  t = {};
  t.wYear = (WORD)atoi(iso.substr(0, 4).c_str());
  t.wMonth = (WORD)atoi(iso.substr(5, 2).c_str());
  t.wDay = (WORD)atoi(iso.substr(8, 2).c_str());
  t.wHour = 12;
  FILETIME f;
  if (!SystemTimeToFileTime(&t, &f)) return false;
  return FileTimeToSystemTime(&f, &t) != 0;  // fills the weekday
}
std::string isoOf(const SYSTEMTIME& t) {
  char b[16];
  snprintf(b, sizeof b, "%04d-%02d-%02d", t.wYear, t.wMonth, t.wDay);
  return b;
}
std::string todayIso() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  return isoOf(t);
}
std::string addDays(const std::string& iso, int days) {
  SYSTEMTIME t;
  if (!parseIso(iso, t)) return iso;
  FILETIME f;
  SystemTimeToFileTime(&t, &f);
  ULARGE_INTEGER u{};
  u.LowPart = f.dwLowDateTime;
  u.HighPart = f.dwHighDateTime;
  u.QuadPart += (ULONGLONG)((LONGLONG)days * 864000000000LL);
  f.dwLowDateTime = u.LowPart;
  f.dwHighDateTime = u.HighPart;
  FileTimeToSystemTime(&f, &t);
  return isoOf(t);
}
std::wstring longDate(const std::string& iso) {  // „четвъртък, 26.11.2026“
  static const wchar_t* kDays[] = {L"неделя", L"понеделник", L"вторник", L"сряда", L"четвъртък", L"петък", L"събота"};
  SYSTEMTIME t;
  if (!parseIso(iso, t)) return widen(iso);
  wchar_t b[64];
  swprintf(b, 64, L"%ls, %02d.%02d.%04d", kDays[t.wDayOfWeek % 7], t.wDay, t.wMonth, t.wYear);
  return b;
}
int daysInMonth(int y, int m) {
  static const int k[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2) return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
  return k[(m + 11) % 12];
}
std::wstring selfExe() {
  wchar_t p[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
  return std::wstring(p, n);
}
}  // namespace

// ================================================================ Календар
// GET /desktop/calendar — the board as ▧ in the browser (CalendarView.tsx): cards on their due date, future ones too (the
// calendar ignores „дни преди“, to plan ahead), those not on the boards yet faded; due in another month, or undated, in
// the two buckets above the grid. A day (or a bucket) → its cards, with everything a card has in the table.
void Panel::paintCalendar(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Calendar& cal = a.cal;
  static const wchar_t* kMonths[] = {L"", L"януари", L"февруари", L"март", L"април", L"май", L"юни",
                                     L"юли", L"август", L"септември", L"октомври", L"ноември", L"декември"};
  float y = area.t;
  std::wstring title = L"Календар" + (cal.subviewLabel.empty() ? std::wstring() : L"  ·  " + cal.subviewLabel);
  ui.text(RectF(area.l + 4, y, area.r - 340, y + 30), title, 16.f, P.text, 700);
  if (cal.loaded) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    bool current = cal.year == now.wYear && cal.month == now.wMonth;
    int py = cal.month == 1 ? cal.year - 1 : cal.year, pm = cal.month == 1 ? 12 : cal.month - 1;
    int ny = cal.month == 12 ? cal.year + 1 : cal.year, nm = cal.month == 12 ? 1 : cal.month + 1;
    RectF prev(area.r - 276, y + 1, area.r - 240, y + 29), label(area.r - 236, y, area.r - 40, y + 30), next(area.r - 36, y + 1, area.r, y + 29);
    if (ui.iconButton(ui.id("cprev"), prev, kPrev, L"Предишен месец", !a.calBusy)) {
      calSel_.clear();
      after([py, pm] { app().loadCalendar(py, pm); });
    }
    ui.text(label, std::wstring(kMonths[cal.month]) + L" " + num(cal.year), 13.f, P.text, 600, Align::Center);
    if (ui.iconButton(ui.id("cnext"), next, kNext, L"Следващ месец", !a.calBusy)) {
      calSel_.clear();
      after([ny, nm] { app().loadCalendar(ny, nm); });
    }
    if (!current && ui.link(ui.id("ctoday"), RectF(area.r - 330, y, area.r - 282, y + 30), L"днес", 12.f)) {
      calSel_.clear();
      int ty = now.wYear, tm = now.wMonth;
      after([ty, tm] { app().loadCalendar(ty, tm); });
    }
  }
  y += 40;
  if (!cal.loaded) {
    bool err = !a.calError.empty();
    ui.text(RectF(area.l + 4, y, area.r, y + 40), err ? a.calError : L"Зарежда календара…", 12.5f, err ? P.danger : P.dim, 400, Align::Left,
            VAlign::Top, true);
    return;
  }

  // the cards by day, and the two buckets
  char ym[8];
  snprintf(ym, sizeof ym, "%04d-%02d", cal.year, cal.month);
  std::wstring wym = widen(ym);
  std::map<std::wstring, std::vector<const SumCard*>> byDay;
  std::vector<const SumCard*> outside, undated;
  for (auto& k : cal.cards) {
    if (k.dueDate.size() < 10) undated.push_back(&k);
    else if (k.dueDate.compare(0, 7, wym) != 0) outside.push_back(&k);
    else byDay[k.dueDate.substr(0, 10)].push_back(&k);
  }
  std::sort(outside.begin(), outside.end(), [](const SumCard* x, const SumCard* z) { return x->dueDate < z->dueDate; });

  auto bucket = [&](RectF b, const std::wstring& text, int n, const char* key) {
    bool on = calSel_ == key;
    bool hot = ui.hot(b);
    ui.p->rrect(b, 7, on ? P.accent.withA(0.16f) : P.text.withA(hot ? 0.08f : 0.045f));
    ui.text(RectF(b.l + 12, b.t, b.r - 8, b.b), text + L"  ·  " + num(n), 12.f, P.text, on ? 700 : 600);
    if (hot) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
    if (ui.press(ui.id("cbucket", key[0]), b)) {
      calSel_ = on ? "" : key;
      ui.scrollTo(ui.id("calcards"), 0);
    }
  };
  std::wstring outsideText = L"⏮ Със срок извън " + std::wstring(kMonths[cal.month]);
  if (!outside.empty() || !undated.empty()) {
    float x = area.l;
    if (!outside.empty()) {
      float w = ui.textWidth(outsideText + L"  ·  " + num((int)outside.size()), 12.f, 700) + 26;
      bucket(RectF(x, y, x + w, y + 30), outsideText, (int)outside.size(), "outside");
      x += w + 8;
    }
    if (!undated.empty()) {
      float w = ui.textWidth(std::wstring(L"Без срок  ·  ") + num((int)undated.size()), 12.f, 700) + 26;
      bucket(RectF(x, y, x + w, y + 30), L"Без срок", (int)undated.size(), "undated");
    }
    y += 40;
  }

  // the selection: a day of this month, or a bucket
  bool isBucket = calSel_ == "outside" || calSel_ == "undated";
  if (!calSel_.empty() && !isBucket && calSel_.compare(0, 7, ym) != 0) calSel_.clear();  // another month is on screen
  std::vector<const SumCard*> selCards;
  std::wstring selTitle;
  if (calSel_ == "outside") {
    selCards = outside;
    selTitle = outsideText;
  } else if (calSel_ == "undated") {
    selCards = undated;
    selTitle = L"Без срок";
  } else if (!calSel_.empty()) {
    auto it = byDay.find(widen(calSel_));
    if (it != byDay.end()) selCards = it->second;
    selTitle = longDate(calSel_);
  }
  bool selected = !calSel_.empty();

  // the grid: Monday first; a chosen day keeps only its week, a chosen bucket none
  if (!isBucket) {
    int days = daysInMonth(cal.year, cal.month);
    SYSTEMTIME first;
    parseIso(std::string(ym) + "-01", first);
    int lead = (first.wDayOfWeek + 6) % 7;
    int weeks = (lead + days + 6) / 7;
    int selWeek = selected ? (lead + atoi(calSel_.substr(8, 2).c_str()) - 1) / 7 : -1;
    static const wchar_t* kWeekdays[] = {L"пн", L"вт", L"ср", L"чт", L"пт", L"сб", L"нд"};
    float colW = area.w() / 7;
    for (int d = 0; d < 7; d++)
      ui.text(RectF(area.l + d * colW + 8, y, area.l + (d + 1) * colW, y + 22), kWeekdays[d], 11.f, d >= 5 ? P.faint : P.dim, 600);
    y += 24;
    float cellH = selected ? 112.f : std::max(64.f, (area.b - y) / weeks);
    std::string today = todayIso();
    int row = 0;
    for (int w = 0; w < weeks; w++) {
      if (selected && w != selWeek) continue;
      for (int d = 0; d < 7; d++) {
        int day = w * 7 + d - lead + 1;
        RectF cell(area.l + d * colW + 1, y + row * cellH + 1, area.l + (d + 1) * colW - 1, y + (row + 1) * cellH - 1);
        if (day < 1 || day > days) {
          ui.p->rrect(cell, 6, P.text.withA(0.02f));
          continue;
        }
        char iso[16];
        snprintf(iso, sizeof iso, "%s-%02d", ym, day);
        auto it = byDay.find(widen(iso));
        const std::vector<const SumCard*>* list = it == byDay.end() ? nullptr : &it->second;
        bool isToday = today == iso, isSel = calSel_ == iso, hot = ui.hot(cell);
        ui.p->rrect(cell, 6, isSel ? P.accent.withA(0.14f) : d >= 5 ? P.text.withA(0.045f) : P.card);
        ui.p->rrectStroke(cell.inset(0.5f), 6, isToday ? P.accent : P.stroke, isToday ? 1.6f : 1.f);
        ui.text(RectF(cell.l + 8, cell.t + 3, cell.r - 40, cell.t + 22), num(day), 12.f, isToday ? P.accent : P.text, isToday ? 700 : 600);
        if (list) {
          int n = (int)list->size();
          ui.text(RectF(cell.r - 44, cell.t + 3, cell.r - 8, cell.t + 22), num(n), 11.f, P.dim, 600, Align::Right);
          float cy = cell.t + 24;
          int room = std::max(0, (int)((cell.b - 4 - cy) / 19));
          int show = n > room ? std::max(0, room - 1) : n;
          for (int i = 0; i < show; i++) {
            const SumCard& k = *(*list)[i];
            RectF chip(cell.l + 4, cy, cell.r - 4, cy + 17);
            float fade = k.notYetVisible ? 0.5f : 1.f;  // not on the boards yet: as the browser draws it, faded
            Color edge = k.hasBand ? k.band : statusColorOf(k.status);
            ui.p->rrect(chip, 3, P.text.withA(0.05f));
            ui.p->rrect(RectF(chip.l, chip.t, chip.l + 4, chip.b), 2, edge.alpha(fade));
            std::wstring t = (k.closed ? L"✓ " : L"") + (k.clientName.empty() ? k.title : k.clientName + L" · " + k.title);
            ui.text(RectF(chip.l + 8, chip.t, chip.r - 2, chip.b), t, 10.5f, (k.closed ? P.dim : P.text).alpha(fade));
            cy += 19;
          }
          if (show < n) ui.text(RectF(cell.l + 8, cy, cell.r - 4, cy + 17), L"+ още " + num(n - show), 10.5f, P.dim, 600);
          if (hot) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
          if (ui.press(ui.id("cday", day), cell)) {
            calSel_ = isSel ? std::string() : std::string(iso);
            ui.scrollTo(ui.id("calcards"), 0);
          }
        }
      }
      row++;
    }
    y += (selected ? 1 : weeks) * cellH + 6;
  }
  if (!selected) return;

  // the chosen day's (or bucket's) cards — the same list as the table's
  ui.text(RectF(area.l + 4, y + 4, area.r - 40, y + 28), selTitle + L"  —  " + num((int)selCards.size()) + L" карти", 12.5f, P.text, 600);
  if (ui.iconButton(ui.id("ccl"), RectF(area.r - 32, y + 2, area.r, y + 30), kClose, L"Затвори — целият месец")) calSel_.clear();
  paintCards(RectF(area.l, y + 34, area.r, area.b), selCards, ui.id("calcards"));
}

// ================================================================ Клиенти
// GET /clients — the browser's list (its visibility, the first business line in scope). A click on a client shows its
// open cards in the current view of the table, with everything a card has there (lane, start, period).
void Panel::paintClients(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const Color orange = P.dark ? Color::hex(0xFDBA74) : Color::hex(0xC2410C);
  float y = area.t;

  std::wstring q = lower(trim(clientQuery_));
  std::vector<const ClientRow*> rows;
  for (auto& c : a.clients)
    if (q.empty() || lower(c.name).find(q) != std::wstring::npos || lower(c.identifier).find(q) != std::wstring::npos)
      rows.push_back(&c);
  std::wstring head = L"Клиенти";
  if (a.clientsLoaded) head += L"  ·  " + num((int)rows.size()) + (q.empty() ? L"" : L" от " + num((int)a.clients.size()));
  ui.text(RectF(area.l + 4, y, area.r - 380, y + 30), head, 16.f, P.text, 700);
  ui.textBox(ui.id("csearch"), RectF(area.r - 370, y, area.r - 44, y + 30), clientQuery_, L"Търси по име или ЕИК", kSearch);
  if (ui.iconButton(ui.id("creload"), RectF(area.r - 36, y + 1, area.r, y + 29), kReload, L"Презареди", !a.clientsBusy))
    after([] { app().loadClients(); });
  y += 40;
  if (!a.clientsLoaded) {
    bool err = !a.clientsError.empty();
    ui.text(RectF(area.l + 4, y, area.r, y + 40), err ? a.clientsError : L"Зарежда клиентите…", 12.5f, err ? P.danger : P.dim, 400,
            Align::Left, VAlign::Top, true);
    return;
  }
  const ClientRow* sel = nullptr;
  for (auto* c : rows)
    if (c->id == selClient_) sel = c;
  if (!sel) selClient_ = 0;

  // columns: the name takes the rest
  struct Col {
    const wchar_t* head;
    float w;
  };
  static const Col kCols[] = {{L"ЕИК", 108}, {L"сл.", 40}, {L"самост.", 60}, {L"отговорник", 168},
                              {L"съдружник", 168}, {L"дейности", 70}, {L"бизнес линии", 176}};
  float fixed = 0;
  for (auto& c : kCols) fixed += c.w;
  float nameW = std::max(160.f, area.w() - fixed);
  float hh = 26;
  ui.text(RectF(area.l + 8, y, area.l + nameW, y + hh), L"КЛИЕНТ", 9.5f, P.dim, 600, Align::Left, VAlign::Bottom);
  {
    float x = area.l + nameW;
    for (auto& c : kCols) {
      ui.text(RectF(x + 2, y, x + c.w - 2, y + hh), upper(c.head), 9.5f, P.dim, 600, Align::Left, VAlign::Bottom);
      x += c.w;
    }
  }
  y += hh + 4;
  ui.divider(RectF(area.l, y - 1, area.r, y));
  const float rh = 30;

  auto drawRow = [&](const ClientRow& c, RectF rr, int salt) {
    bool isSel = c.id == selClient_;
    bool hot = ui.hot(rr);
    if (isSel) ui.p->rrect(rr, 6, P.accent.withA(0.14f));
    else if (hot) ui.p->rrect(rr, 6, P.text.withA(0.05f));
    Color fg = c.active ? P.text : P.faint;
    std::wstring name = c.name + (c.active ? L"" : L"  (неактивен)");
    ui.text(RectF(rr.l + 8, rr.t, rr.l + nameW - 8, rr.b), name, 12.5f, fg, isSel ? 600 : 400);
    float x = rr.l + nameW;
    auto cell = [&](const std::wstring& t, float w, Color col) {
      ui.text(RectF(x + 2, rr.t, x + w - 6, rr.b), t.empty() ? L"—" : t, 12.f, t.empty() ? P.faint : col);
      x += w;
    };
    cell(c.identifier, kCols[0].w, P.dim);
    cell(c.complexity, kCols[1].w, fg);
    cell(c.autonomy < 0 ? L"" : num(c.autonomy) + L"%", kCols[2].w, fg);
    cell(c.owner, kCols[3].w, fg);
    cell(c.partner, kCols[4].w, fg);
    cell(c.deadlines ? num(c.deadlines) : L"", kCols[5].w, fg);
    // business lines: green = set up there, orange = not yet (the browser's colours for the client's setup)
    float lx = x + 2;
    for (size_t i = 0; i < c.lines.size(); i++) {
      std::wstring t = c.lines[i].name + (i + 1 < c.lines.size() ? L"," : L"");
      float w = ui.textWidth(t, 12.f, 600) + 6;
      if (lx + w > rr.r - 30) {  // no room for the rest: how many more
        ui.text(RectF(lx, rr.t, rr.r - 4, rr.b), L"+" + num((int)(c.lines.size() - i)), 12.f, P.dim, 600);
        break;
      }
      ui.text(RectF(lx, rr.t, lx + w, rr.b), t, 12.f, c.lines[i].ok ? green : orange, 600);
      lx += w;
    }
    if (hot) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
    if (ui.press(ui.id("crow", salt), rr)) {
      selClient_ = isSel ? 0 : c.id;
      ui.scrollTo(ui.id("ccards"), 0);
    }
  };

  if (!sel) {
    RectF listR(area.l, y, area.r, area.b);
    float off = ui.beginScroll(ui.id("clients"), listR, rows.size() * rh);
    for (size_t i = 0; i < rows.size(); i++) {
      RectF rr(area.l, y + off + i * rh, area.r, y + off + (i + 1) * rh);
      if (rr.b < listR.t || rr.t > listR.b) continue;
      if (i % 2 == 1) ui.p->rrect(rr, 4, P.text.withA(P.dark ? 0.035f : 0.03f));
      drawRow(*rows[i], rr, (int)i + 1);
    }
    ui.endScroll();
    if (rows.empty()) ui.text(RectF(area.l + 4, y + 10, area.r, y + 40), L"Няма такъв клиент.", 12.5f, P.faint);
    return;
  }

  // One client chosen: its row, and its open cards in the table's current view.
  drawRow(*sel, RectF(area.l, y + 2, area.r, y + 2 + rh), 0);
  ui.tooltip(RectF(area.l, y + 2, area.l + nameW, y + 2 + rh), L"Щракнете отново — всички клиенти");
  y += rh + 6;
  ui.divider(RectF(area.l, y - 1, area.r, y));
  if (!selClient_) return;  // that click just closed it
  std::vector<const SumCard*> cards;
  for (auto& k : a.summary.cards)
    if (k.clientId == sel->id && !k.closed) cards.push_back(&k);
  std::wstring t = L"Отворените карти на клиента в изгледа „" + a.summary.subviewLabel + L"“  —  " + num((int)cards.size());
  ui.text(RectF(area.l + 4, y + 8, area.r - 40, y + 32), t, 12.5f, P.text, 600);
  if (ui.iconButton(ui.id("cclose"), RectF(area.r - 32, y + 6, area.r, y + 34), kClose, L"Затвори — всички клиенти")) selClient_ = 0;
  paintCards(RectF(area.l, y + 38, area.r, area.b), cards, ui.id("ccards"));
}

// ================================================================ Административни дейности
// GET /time/day — the day's activities (already filtered by the person's role), what is recorded, the totals.
// „Запиши“ sends only the rows that changed (POST /time/day touches only those); the server checks everything —
// the comment some activities require, the days that may still be written, the role — and its sentence is shown.
void Panel::paintAdminDay(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const AdminDay& d = a.day;
  float y = area.t;
  ui.text(RectF(area.l + 4, y, area.r - 340, y + 30), L"Административни дейности и неработно време", 16.f, P.text, 700);
  if (d.loaded) {
    bool today = d.date >= todayIso();
    RectF prev(area.r - 276, y + 1, area.r - 240, y + 29), dateR(area.r - 236, y, area.r - 40, y + 30), next(area.r - 36, y + 1, area.r, y + 29);
    if (ui.iconButton(ui.id("dprev"), prev, kPrev, L"Предишен ден", !a.dayBusy)) {
      std::string p = addDays(d.date, -1);
      after([p] { app().loadDay(p); });
    }
    ui.text(dateR, longDate(d.date), 12.5f, P.text, 600, Align::Center);
    if (ui.iconButton(ui.id("dnext"), next, kNext, L"Следващ ден", !a.dayBusy && !today)) {
      std::string n = addDays(d.date, 1);
      after([n] { app().loadDay(n); });
    }
    if (!today && ui.link(ui.id("dtoday"), RectF(area.r - 330, y, area.r - 282, y + 30), L"днес", 12.f))
      after([] { app().loadDay(todayIso()); });
  }
  y += 38;
  if (!d.loaded) {
    bool err = !a.dayError.empty();
    ui.text(RectF(area.l + 4, y, area.r, y + 40), err ? a.dayError : L"Зарежда деня…", 12.5f, err ? P.danger : P.dim, 400, Align::Left,
            VAlign::Top, true);
    return;
  }

  // What is recorded per activity (several lines when it asks for a colleague / a client: their sum).
  std::map<int, int> recorded;
  std::map<int, std::wstring> recordedNote;
  for (auto& e : d.entries) {
    recorded[e.activityId] += e.seconds;
    if (recordedNote[e.activityId].empty()) recordedNote[e.activityId] = e.note;
  }
  // A new answer from the server: the form starts from it.
  if (dayFormSeq_ != a.daySeq) {
    dayFormSeq_ = a.daySeq;
    dayInput_.clear();
    dayNoteIn_.clear();
    for (auto& [id, secs] : recorded) dayInput_[id] = secs ? hm(secs) : L"";
    for (auto& [id, note] : recordedNote) dayNoteIn_[id] = note;
  }

  std::wstring totals = L"Карти " + hm(d.cardSeconds) + L"   ·   Административно " + hm(d.adminSeconds) + L"   ·   Неработно време " +
                        hm(d.nonWorkingSeconds) + L"   ·   Общо " + hm(d.totalSeconds);
  ui.text(RectF(area.l + 4, y, area.r, y + 22), totals, 12.5f, P.dim);
  y += 26;
  if (!a.dayError.empty()) {
    ui.text(RectF(area.l + 4, y, area.r, y + 36), a.dayError, 12.f, P.danger, 400, Align::Left, VAlign::Top, true);
    y += 38;
  }

  // The rows: each root a heading, then its activities (or the root itself when it takes time and has none).
  struct Line {
    const DayActivity* act;
    bool header;
  };
  std::vector<Line> lines;
  for (auto& r : d.activities) {
    lines.push_back({&r, true});
    if (r.children.empty() && (r.acceptsTime || r.lines)) lines.push_back({&r, false});
    for (auto& c : r.children)
      if (c.acceptsTime || c.lines) lines.push_back({&c, false});
  }
  auto editable = [](const DayActivity& x) { return !x.lines && !x.roleLocked; };
  std::vector<App::DayRowIn> changed;
  for (auto& l : lines) {
    if (l.header || !editable(*l.act)) continue;
    int id = l.act->id;
    std::wstring was = recorded[id] ? hm(recorded[id]) : L"";
    if (trim(dayInput_[id]) != was || trim(dayNoteIn_[id]) != trim(recordedNote[id])) changed.push_back({id, dayInput_[id], dayNoteIn_[id]});
  }

  RectF bar(area.l, area.b - 40, area.r, area.b);
  RectF listR(area.l, y, area.r, bar.t - 8);
  float contentH = 0;
  for (auto& l : lines) contentH += l.header ? 32.f : 42.f;
  float off = ui.beginScroll(ui.id("dayrows"), listR, contentH);
  float yy = listR.t + off;
  for (auto& l : lines) {
    const DayActivity& x = *l.act;
    if (l.header) {
      RectF hr(listR.l + 4, yy, listR.r, yy + 32);
      yy += 32;
      if (hr.b < listR.t || hr.t > listR.b) continue;
      ui.text(RectF(hr.l, hr.t + 6, hr.r, hr.b), x.name, 13.f, P.text, 700, Align::Left, VAlign::Bottom);
      continue;
    }
    RectF row(listR.l, yy, listR.r, yy + 38);
    yy += 42;
    if (row.b < listR.t || row.t > listR.b) continue;
    ui.card(row, 6);
    int id = x.id;
    bool isChanged = std::any_of(changed.begin(), changed.end(), [id](const App::DayRowIn& c) { return c.activityId == id; });
    if (isChanged) ui.p->rrect(RectF(row.l + 2, row.t + 4, row.l + 6, row.b - 4), 2.f, P.accent);
    std::wstring name = x.name + (x.isAbsence ? L"  ·  отсъствие" : L"");
    RectF nameR(row.l + 14, row.t, row.r - 560, row.b);
    ui.text(nameR, name, 12.5f, P.text);
    if (!x.hint.empty()) ui.tooltip(nameR, x.hint);
    int rec = recorded[id];
    ui.text(RectF(row.r - 556, row.t, row.r - 470, row.b), rec ? L"записано " + hm(rec) : L"", 11.5f, P.dim, 400, Align::Right);
    if (x.lines) {
      ui.text(RectF(row.r - 460, row.t, row.r - 10, row.b), L"по колега или клиент — записва се в браузъра", 11.5f, P.faint);
    } else if (x.roleLocked) {
      ui.glyph(RectF(row.r - 460, row.t, row.r - 436, row.b), kLock, 13.f, P.faint);
      ui.text(RectF(row.r - 432, row.t, row.r - 10, row.b), L"не е за вашата роля — може само да се изчисти в браузъра", 11.5f, P.faint);
    } else {
      ui.textBox(ui.id("din", id), RectF(row.r - 460, row.t + 4, row.r - 372, row.b - 4), dayInput_[id], L"ч:мм");
      std::wstring noteHint = x.requiresNote ? (x.hint.empty() ? L"коментар (задължителен)" : x.hint) : L"коментар";
      ui.textBox(ui.id("dnote", id), RectF(row.r - 364, row.t + 4, row.r - 8, row.b - 4), dayNoteIn_[id], noteHint);
    }
  }
  ui.endScroll();

  // the bar
  if (!a.dayNote.empty() && nowSec() < a.dayNoteUntil) ui.text(RectF(bar.l + 4, bar.t, bar.l + 300, bar.b), L"✓ " + a.dayNote, 12.5f, green, 600);
  else ui.text(RectF(bar.l + 4, bar.t, bar.r - 420, bar.b), L"Време: 45 = 45 минути, 1:30 = час и половина. Празно — изтрива.", 11.5f, P.faint);
  std::wstring label = changed.empty() ? L"Няма промени" : L"Запиши промените (" + num((int)changed.size()) + L")";
  if (ui.button(ui.id("dsave"), RectF(bar.r - 240, bar.t + 4, bar.r, bar.b - 4), label, Btn::Accent, kSave, !changed.empty() && !a.dayBusy))
    after([changed] { app().saveDay(changed); });
  if (!changed.empty() && ui.button(ui.id("dundo"), RectF(bar.r - 380, bar.t + 4, bar.r - 248, bar.b - 4), L"Отмени", Btn::Subtle, kClose, !a.dayBusy))
    dayFormSeq_ = -1;  // back to what is recorded
}

// ================================================================ Профил и настройки
void Panel::paintProfile(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  float y = area.t;
  ui.text(RectF(area.l + 4, y, area.r, y + 30), L"Профил и настройки", 16.f, P.text, 700);
  y += 42;
  float colW = (area.w() - 14) / 2;
  RectF c1(area.l, y, area.l + colW, y + 330), c2(c1.r + 14, y, area.r, y + 330);
  ui.card(c1, 10);
  ui.card(c2, 10);

  // the person — GET /me
  ui.text(RectF(c1.l + 18, c1.t + 14, c1.r - 18, c1.t + 38), L"Вие", 14.f, P.text, 700);
  const Me& m = a.me;
  float yy = c1.t + 48;
  auto field = [&](RectF col, const wchar_t* label, const std::wstring& v) {
    ui.text(RectF(col.l + 18, yy, col.l + 160, yy + 26), label, 12.f, P.dim);
    ui.text(RectF(col.l + 160, yy, col.r - 18, yy + 26), v.empty() ? L"—" : v, 12.5f, v.empty() ? P.faint : P.text);
    yy += 28;
  };
  if (!m.loaded) {
    ui.text(RectF(c1.l + 18, yy, c1.r - 18, yy + 26), L"Зарежда…", 12.5f, P.dim);
  } else {
    std::wstring teams;
    for (auto& t : m.teams) teams += (teams.empty() ? L"" : L", ") + t;
    field(c1, L"Име", m.name);
    field(c1, L"Потребител", m.username);
    field(c1, L"Имейл", m.email);
    field(c1, L"Длъжност", m.jobTitle);
    field(c1, L"Бизнес линия", m.department);
    field(c1, L"Мобилен", m.mobile);
    field(c1, L"Вътрешен", m.internalPhone);
    field(c1, L"Екипи", teams);
  }
  if (ui.link(ui.id("pweb"), RectF(c1.l + 18, c1.b - 40, c1.r - 18, c1.b - 14), L"Досие, парола и изглед — в браузъра", 12.f))
    after([] { app().openBrowser(); });

  // this app
  ui.text(RectF(c2.l + 18, c2.t + 14, c2.r - 18, c2.t + 38), L"Това приложение", 14.f, P.text, 700);
  yy = c2.t + 48;
  field(c2, L"Сървър", a.api.base);
  auto toggleRow = [&](uint32_t id, const wchar_t* label, bool v, bool enabled, const wchar_t* tip) -> int {
    ui.text(RectF(c2.l + 18, yy, c2.r - 90, yy + 30), label, 12.5f, enabled ? P.text : P.faint);
    RectF tr(c2.r - 70, yy + 3, c2.r - 18, yy + 27);
    bool nv = v;
    bool changedNow = ui.toggle(id, tr, nv, enabled);
    if (tip) ui.tooltip(RectF(c2.l + 18, yy, c2.r - 18, yy + 30), tip);
    yy += 36;
    return changedNow ? (nv ? 1 : 0) : -1;
  };
  int pill = toggleRow(ui.id("ppill"), L"Бутон на лентата на задачите", a.cfg.pill, true, nullptr);
  if (pill >= 0) after([pill] { app().setPillOn(pill == 1); });
  bool installed = isInstalledCopy();
  int autostart = toggleRow(ui.id("pauto"), L"Стартирай с Windows", installed && autostartOn(), installed,
                            installed ? nullptr : L"Само за инсталираното копие на програмата.");
  if (autostart >= 0) {
    std::wstring exe = selfExe();
    after([autostart, exe] { setAutostart(autostart == 1, exe); });
  }
  ui.text(RectF(c2.l + 18, yy, c2.r - 240, yy + 32), L"Версия " + widen(KD_VERSION_STR), 12.5f, P.text);
  if (ui.button(ui.id("pupd"), RectF(c2.r - 230, yy + 1, c2.r - 18, yy + 31), a.updateBusy ? L"Обновява се…" : L"Провери за обновление",
                Btn::Standard, kSync, !a.updateBusy))
    after([] { app().checkForUpdate(true); });
  yy += 44;
  if (ui.button(ui.id("pout"), RectF(c2.l + 18, c2.b - 48, c2.l + 220, c2.b - 16), L"Изход от профила", Btn::Danger, kSignOut))
    after([] { app().signOut(); });
}

}  // namespace kd
