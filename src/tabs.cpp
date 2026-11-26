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
// civil date <-> day number (Howard Hinnant's algorithms) — the audit timeline's arithmetic
int dayNo(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}
int dayNoIso(const std::wstring& iso) {
  if (iso.size() < 10) return 0;
  return dayNo(_wtoi(iso.substr(0, 4).c_str()), _wtoi(iso.substr(5, 2).c_str()), _wtoi(iso.substr(8, 2).c_str()));
}
int dayNoIso(const std::string& iso) { return dayNoIso(widen(iso)); }
void civilOf(int z, int& y, int& m, int& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y += m <= 2;
}
int mon0Of(int z) { return ((z % 7) + 7 + 3) % 7; }  // Monday = 0 (day 0, 1970-01-01, was a Thursday)
std::wstring dmyOf(int z) {
  int y, m, d;
  civilOf(z, y, m, d);
  wchar_t b[16];
  swprintf(b, 16, L"%02d.%02d.%04d", d, m, y);
  return b;
}
std::wstring dmyFromIso(const std::string& iso) {  // "2026-11-26" -> "26.11.2026"
  if (iso.size() < 10) return widen(iso);
  return widen(iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4));
}
std::string isoFromDmy(const std::wstring& t0) {  // "26.11.2026" (or "2026-11-26") -> "2026-11-26"; else as typed
  std::wstring t = trim(t0);
  int d = 0, m = 0, y = 0;
  if (swscanf(t.c_str(), L"%d.%d.%d", &d, &m, &y) == 3 && y > 1900 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
    char b[16];
    snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    return b;
  }
  return narrow(t);  // the server answers „Невалиден период“ if it is not a date
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

// ================================================================ Одитен график
// GET /desktop/audit — as „Одитен график“ in the browser (AuditGantt.tsx): projects, their tasks, their cards on a time
// axis (year · quarter · month · own period); a project's / task's bar runs from its first start to its last deadline,
// filled by the share closed, red when something is overdue, hatched past the planned end, ✔ at the last closing.
void Panel::paintAudit(RectF area) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const AuditTimeline& t = a.audit;
  const Color green = Color::hex(0x2ECC71), red = P.dark ? Color::hex(0xF87171) : Color::hex(0xD33333);
  static const wchar_t* kMonShort[] = {L"", L"яну", L"фев", L"мар", L"апр", L"май", L"юни", L"юли", L"авг", L"сеп", L"окт", L"ное", L"дек"};
  static const wchar_t* kMonths[] = {L"", L"януари", L"февруари", L"март", L"април", L"май", L"юни",
                                     L"юли", L"август", L"септември", L"октомври", L"ноември", L"декември"};
  std::string today = t.loaded ? t.today : todayIso();
  int ty = atoi(today.substr(0, 4).c_str()), tm = atoi(today.substr(5, 2).c_str());
  if (auditY_ == 0) {
    auditY_ = ty;
    auditM_ = tm;
  }
  auto split = [](int idx, int& y, int& m) {  // idx = y*12 + (m-1)
    y = idx >= 0 ? idx / 12 : -((-idx + 11) / 12);
    m = idx - y * 12 + 1;
  };
  // the span on screen
  int fromNo = 0, toNo = 0;
  int q0 = (auditM_ - 1) / 3 * 3 + 1;
  if (auditLevel_ == 0) {
    int idx = auditY_ * 12 + auditM_ - 1, ya, ma, yb, mb;
    split(idx - auditBack_, ya, ma);
    split(idx + auditAhead_, yb, mb);
    fromNo = dayNo(ya, ma, 1);
    toNo = dayNo(yb, mb, daysInMonth(yb, mb));
  } else if (auditLevel_ == 1) {
    fromNo = dayNo(auditY_, 1, 1);
    toNo = dayNo(auditY_, 12, 31);
  } else if (auditLevel_ == 2) {
    fromNo = dayNo(auditY_, q0, 1);
    toNo = dayNo(auditY_, q0 + 2, daysInMonth(auditY_, q0 + 2));
  } else {
    fromNo = dayNo(auditY_, auditM_, 1);
    toNo = dayNo(auditY_, auditM_, daysInMonth(auditY_, auditM_));
  }
  const int total = toNo - fromNo + 1;

  // ---- title and navigation
  float y = area.t;
  ui.text(RectF(area.l + 4, y, area.l + 300, y + 30), L"Одитен график", 16.f, P.text, 700);
  std::wstring label;
  if (auditLevel_ == 0) label = dmyOf(fromNo).substr(3) + L" – " + dmyOf(toNo).substr(3);
  else if (auditLevel_ == 1) label = num(auditY_);
  else if (auditLevel_ == 2) label = num((auditM_ - 1) / 3 + 1) + L"-во тримесечие " + num(auditY_);
  else label = std::wstring(kMonths[auditM_]) + L" " + num(auditY_);
  int months = auditLevel_ == 1 ? 12 : auditLevel_ == 2 ? 3 : 1;
  RectF prev(area.r - 316, y + 1, area.r - 280, y + 29), lab(area.r - 276, y, area.r - 76, y + 30), next(area.r - 72, y + 1, area.r - 36, y + 29);
  if (ui.iconButton(ui.id("aprev"), prev, kPrev, L"Назад")) split(auditY_ * 12 + auditM_ - 1 - months, auditY_, auditM_);
  ui.text(lab, label, 13.f, P.text, 600, Align::Center);
  if (ui.iconButton(ui.id("anext"), next, kNext, L"Напред")) split(auditY_ * 12 + auditM_ - 1 + months, auditY_, auditM_);
  if (ui.link(ui.id("atoday"), RectF(area.r - 32, y, area.r, y + 30), L"днес", 12.f)) {
    auditY_ = ty;
    auditM_ = tm;
  }
  y += 38;

  // ---- the scale, the own period, the depth
  int lvl = auditLevel_ == 0 ? 3 : auditLevel_ - 1;
  std::vector<std::wstring> lv = {L"Година", L"Тримесечие", L"Месец", L"⇆ Свой период"};
  if (ui.segmented(ui.id("alevel"), RectF(area.l, y, area.l + 430, y + 30), lvl, lv)) auditLevel_ = lvl == 3 ? 0 : lvl + 1;
  float x = area.l + 442;
  if (auditLevel_ == 0) {  // months back / ahead from the anchor month
    auto stepper = [&](uint32_t id, int& v, const wchar_t* what) {
      if (ui.iconButton(id, RectF(x, y + 1, x + 26, y + 29), L"\xE738", L"По-малко") && v > 0) v--;
      ui.text(RectF(x + 28, y, x + 52, y + 30), num(v), 12.5f, P.text, 600, Align::Center);
      if (ui.iconButton(id + 1, RectF(x + 54, y + 1, x + 80, y + 29), L"\xE710", L"Повече") && v < 36) v++;
      ui.text(RectF(x + 84, y, x + 160, y + 30), what, 11.5f, P.dim);
      x += 164;
    };
    stepper(ui.id("aback"), auditBack_, L"мес. назад");
    stepper(ui.id("aahead"), auditAhead_, L"мес. напред");
  }
  // all project and task keys, for ＋ / － ниво and „Разгъни всичко“
  std::vector<std::wstring> pkeys, tkeys;
  for (auto& pr : t.projects) {
    pkeys.push_back(L"p:" + num(pr.id));
    for (auto& tk : pr.tasks) tkeys.push_back(L"t:" + num(pr.id) + L":" + tk.title);
  }
  int depth = std::any_of(auditOpen_.begin(), auditOpen_.end(), [](const std::wstring& k) { return k.rfind(L"t:", 0) == 0; }) ? 2
              : auditOpen_.empty()                                                                                       ? 0
                                                                                                                         : 1;
  auto openTo = [&](int d) {
    auditOpen_.clear();
    if (d >= 1) auditOpen_.insert(pkeys.begin(), pkeys.end());
    if (d >= 2) auditOpen_.insert(tkeys.begin(), tkeys.end());
  };
  if (auditDepthWanted_ >= 0 && !pkeys.empty()) {
    openTo(auditDepthWanted_);
    auditDepthWanted_ = -1;
  }
  float bx = area.r;
  auto small = [&](uint32_t id, const wchar_t* text, float w, bool enabled) {
    RectF b(bx - w, y, bx, y + 30);
    bx -= w + 6;
    return ui.button(id, b, text, Btn::Subtle, nullptr, enabled);
  };
  if (small(ui.id("acollapse"), L"Свий", 64, !auditOpen_.empty())) auditOpen_.clear();
  if (small(ui.id("aexpand"), L"Разгъни всичко", 132, depth < 2)) openTo(2);
  if (small(ui.id("aless"), L"－ ниво", 80, depth > 0)) openTo(depth - 1);
  if (small(ui.id("amore"), L"＋ ниво", 80, depth < 2)) openTo(depth + 1);
  y += 40;
  if (!t.loaded) {
    bool err = !a.auditError.empty();
    ui.text(RectF(area.l + 4, y, area.r, y + 40), err ? a.auditError : L"Зарежда графика…", 12.5f, err ? P.danger : P.dim, 400, Align::Left,
            VAlign::Top, true);
    return;
  }
  if (t.projects.empty()) {
    ui.text(RectF(area.l + 4, y, area.r, y + 40), L"Няма одитни проекти в този изглед.", 12.5f, P.dim);
    return;
  }

  // ---- the rows: projects, their tasks when open, their cards when open
  struct Row {
    int depth;
    const AuditProject* pr;
    const AuditTask* tk;
    const SumCard* card;
    std::wstring key;
  };
  std::vector<Row> rows;
  const SumCard* chosen = nullptr;
  for (auto& pr : t.projects) {
    std::wstring pk = L"p:" + num(pr.id);
    rows.push_back({0, &pr, nullptr, nullptr, pk});
    for (auto& tk : pr.tasks)
      for (auto& c : tk.cards)
        if (c.id == auditCard_) chosen = &c;
    if (!auditOpen_.count(pk)) continue;
    for (auto& tk : pr.tasks) {
      std::wstring tkk = L"t:" + num(pr.id) + L":" + tk.title;
      rows.push_back({1, &pr, &tk, nullptr, tkk});
      if (!auditOpen_.count(tkk)) continue;
      for (auto& c : tk.cards) rows.push_back({2, &pr, &tk, &c, L"c:" + num(c.id)});
    }
  }
  if (!chosen) auditCard_ = 0;

  // ---- the axis
  const float LABEL_W = 270, ROW_H = 28;
  float chartL = area.l + LABEL_W, chartR = area.r;
  auto xOf = [&](int no) { return chartL + (chartR - chartL) * float(no - fromNo) / float(total); };
  struct Seg {
    std::wstring label;
    int from, to;
    bool weekend;
    int level, y, m;  // a click drills there (level 0: no)
  };
  std::vector<Seg> top, bottom;
  if (auditLevel_ == 0) {
    int y0, m0, d0, y1, m1, d1;
    civilOf(fromNo, y0, m0, d0);
    civilOf(toNo, y1, m1, d1);
    for (int yy = y0; yy <= y1; yy++)
      top.push_back({num(yy), std::max(fromNo, dayNo(yy, 1, 1)), std::min(toNo, dayNo(yy, 12, 31)), false, 0, 0, 0});
    for (int idx = y0 * 12 + m0 - 1; idx <= y1 * 12 + m1 - 1; idx++) {
      int yy, mm;
      split(idx, yy, mm);
      bottom.push_back({kMonShort[mm], dayNo(yy, mm, 1), dayNo(yy, mm, daysInMonth(yy, mm)), false, 3, yy, mm});
    }
  } else if (auditLevel_ == 1) {
    for (int q = 0; q < 4; q++)
      top.push_back({num(q + 1) + L"-во трим.", dayNo(auditY_, q * 3 + 1, 1), dayNo(auditY_, q * 3 + 3, daysInMonth(auditY_, q * 3 + 3)), false, 2,
                     auditY_, q * 3 + 1});
    for (int m = 1; m <= 12; m++) bottom.push_back({kMonShort[m], dayNo(auditY_, m, 1), dayNo(auditY_, m, daysInMonth(auditY_, m)), false, 3, auditY_, m});
  } else if (auditLevel_ == 2) {
    for (int m = q0; m < q0 + 3; m++)
      top.push_back({std::wstring(kMonths[m]) + L" " + num(auditY_), dayNo(auditY_, m, 1), dayNo(auditY_, m, daysInMonth(auditY_, m)), false, 3, auditY_, m});
    for (int d = fromNo; d <= toNo;) {
      int end = std::min(toNo, d + 6 - mon0Of(d));
      int yy, mm, dd;
      civilOf(d, yy, mm, dd);
      wchar_t b[16];
      swprintf(b, 16, L"%02d.%02d", dd, mm);
      bottom.push_back({b, d, end, false, 0, 0, 0});
      d = end + 1;
    }
  } else {
    top.push_back({std::wstring(kMonths[auditM_]) + L" " + num(auditY_), fromNo, toNo, false, 0, 0, 0});
    for (int d = fromNo; d <= toNo; d++) {
      int yy, mm, dd;
      civilOf(d, yy, mm, dd);
      bottom.push_back({num(dd), d, d, mon0Of(d) >= 5, 0, 0, 0});
    }
  }
  auto axisRow = [&](const std::vector<Seg>& segs, float yy, const char* idBase) {
    int i = 0;
    for (auto& g : segs) {
      RectF r(xOf(g.from), yy, xOf(g.to + 1), yy + 20);
      ui.p->rrect(RectF(r.l, r.t, r.l + 1, r.b), 0, P.stroke);
      bool can = g.level != 0, hot = can && ui.hot(r);
      if (hot) ui.p->rrect(r, 3, P.text.withA(0.06f));
      ui.text(r, g.label, 11.f, P.dim, 600, Align::Center);
      if (hot) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
      if (can && ui.press(ui.id(idBase, i), r)) {
        auditLevel_ = g.level;
        auditY_ = g.y;
        auditM_ = g.m;
      }
      i++;
    }
  };
  axisRow(top, y, "atop");
  axisRow(bottom, y + 20, "abot");
  y += 44;
  ui.divider(RectF(area.l, y - 1, area.r, y));

  // ---- the body; with a card chosen, the chart keeps the upper part and the card is listed under it
  float chartB = chosen ? std::max(y + 180.f, area.b - 140.f) : area.b;
  RectF bodyR(area.l, y, area.r, chartB);
  for (auto& g : bottom) {  // the grid and the weekends, under the rows
    float gx = xOf(g.from);
    if (g.weekend) ui.p->rrect(RectF(gx, bodyR.t, xOf(g.to + 1), bodyR.b), 0, P.text.withA(0.04f));
    ui.p->rrect(RectF(gx, bodyR.t, gx + 1, bodyR.b), 0, P.stroke.alpha(0.6f));
  }
  int todayNo = dayNoIso(today);
  float off = ui.beginScroll(ui.id("arows"), bodyR, rows.size() * ROW_H);
  auto clamp = [&](int s0, int e0, float& l, float& r, bool& clipL, bool& clipR) {
    if (e0 < fromNo || s0 > toNo) return false;
    clipL = s0 < fromNo;
    clipR = e0 > toNo;
    l = xOf(std::max(s0, fromNo));
    r = std::max(l + 3.f, xOf(std::min(e0, toNo) + 1));
    return true;
  };
  for (size_t i = 0; i < rows.size(); i++) {
    const Row& row = rows[i];
    RectF rr(area.l, bodyR.t + off + i * ROW_H, area.r, bodyR.t + off + (i + 1) * ROW_H);
    if (rr.b < bodyR.t || rr.t > bodyR.b) continue;
    ui.p->rrect(RectF(rr.l, rr.b - 1, rr.r, rr.b), 0, P.stroke.alpha(0.5f));
    // the label
    RectF lr(rr.l + 6 + row.depth * 16, rr.t, chartL - 6, rr.b);
    bool isAgg = row.card == nullptr;
    const AuditAgg* agg = isAgg ? (row.tk ? &row.tk->agg : &row.pr->agg) : nullptr;
    std::wstring text = row.card ? row.card->title : row.tk ? row.tk->title : row.pr->name;
    bool on = isAgg && auditOpen_.count(row.key);
    if (isAgg) ui.glyph(RectF(lr.l, rr.t, lr.l + 14, rr.b), on ? L"\xE70D" : L"\xE76C", 9.f, P.dim);
    RectF tr(lr.l + (isAgg ? 16 : 0), rr.t, lr.r, rr.b);
    ui.text(tr, text + (isAgg ? L"  · " + num(agg->done) + L"/" + num(agg->total) : L""), 12.f,
            row.card && row.card->id == auditCard_ ? P.accent : P.text, row.depth == 0 ? 600 : 400);
    ui.tooltip(lr, text);
    bool hot = ui.hot(lr);
    if (hot) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
    bool clicked = ui.press(ui.id("alabel", (int)i), lr);
    // the bar
    float bl, br;
    bool cl, cr;
    if (isAgg) {
      int s0 = dayNoIso(agg->start), e0 = dayNoIso(agg->end);
      bool late = agg->late;
      Color edge = late ? red : P.accent;
      if (clamp(s0, e0, bl, br, cl, cr)) {
        RectF b(bl, rr.t + 6, br, rr.t + 22);
        ui.p->rrect(b, 4, P.text.withA(0.10f));
        float share = agg->total ? float(agg->done) / float(agg->total) : 0.f;
        if (share > 0) ui.p->rrect(RectF(b.l, b.t, b.l + b.w() * share, b.b), 4, edge.alpha(0.75f));
        ui.p->rrectStroke(b.inset(0.5f), 4, edge, 1.f);
        std::wstring tip = text + L"\
" + dmyOf(s0) + L" → " + dmyOf(e0) + L"\
приключени " + num(agg->done) + L" от " + num(agg->total);
        if (!agg->lastClosed.empty()) tip += L"\
последно приключване " + dmyOf(dayNoIso(agg->lastClosed));
        if (late) tip += L"\
⚠ има просрочени карти";
        ui.tooltip(b, tip);
        if (ui.hot(b)) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
        if (ui.press(ui.id("abar", (int)i), b)) clicked = true;
      }
      if (!agg->lastClosed.empty()) {  // past the planned end: hatched red; ✔ at the last closing
        int lc = dayNoIso(agg->lastClosed);
        int lateD = lc - e0;
        float ol, orr;
        bool ocl, ocr;
        if (lateD > 0 && clamp(e0 + 1, lc, ol, orr, ocl, ocr)) {
          RectF ob(ol, rr.t + 6, orr, rr.t + 22);
          ui.p->rrect(ob, 3, red.alpha(0.30f));
          ui.p->rrectStroke(ob.inset(0.5f), 3, red, 1.f);
          ui.tooltip(ob, L"Приключване след планирания край: " + dmyOf(e0) + L" → " + dmyOf(lc) + L" (" + num(lateD) + L" дни)");
        }
        bool allDone = agg->done == agg->total;
        if (lc >= fromNo && lc <= toNo && (allDone || lateD > 0)) {
          float mx = xOf(lc) + (xOf(lc + 1) - xOf(lc)) / 2;
          Color mc = lateD > 0 ? red : green;
          ui.p->rrect(RectF(mx - 1, rr.t + 2, mx + 1, rr.b - 2), 0, mc);
          ui.text(RectF(mx - 8, rr.t - 4, mx + 8, rr.t + 8), L"✔", 8.f, mc, 700, Align::Center);
        }
      }
      if (clicked) {
        if (on) auditOpen_.erase(row.key);
        else auditOpen_.insert(row.key);
      }
    } else {
      const SumCard& c = *row.card;
      int due = c.dueDate.size() >= 10 ? dayNoIso(c.dueDate) : 0;
      int st0 = c.startDate.size() >= 10 ? dayNoIso(c.startDate) : due ? due : fromNo;
      int en0 = due ? due : st0;
      bool fin = c.closed;
      Color base = c.hasBand ? c.band : P.accent;
      if (clamp(st0, en0, bl, br, cl, cr)) {
        RectF b(bl, rr.t + 6, br, rr.t + 22);
        ui.p->rrect(b, 4, base.alpha(fin ? 0.65f : 1.f));
        if (cl) ui.text(RectF(b.l + 2, b.t, b.l + 14, b.b), L"◀", 8.f, Color(1, 1, 1), 700);
        if (cr) ui.text(RectF(b.r - 14, b.t, b.r - 2, b.b), L"▶", 8.f, Color(1, 1, 1), 700, Align::Right);
        int closedNo = c.closedAt.size() >= 10 ? dayNoIso(c.closedAt) : 0;
        int lateDays = closedNo && due ? closedNo - due : 0;
        std::wstring tip = c.title + L"\
начало " + (c.startDate.size() >= 10 ? dmyOf(st0) : L"—") + L" · срок " + (due ? dmyOf(due) : L"—");
        if (!c.worker.empty()) tip += L"\
" + c.worker;
        if (fin)
          tip += closedNo ? L"\
✔ приключена " + dmyOf(closedNo) +
                                (!due ? L"" : lateDays > 0 ? L" — със закъснение " + num(lateDays) + L" дни"
                                              : lateDays < 0 ? L" — " + num(-lateDays) + L" дни преди срока"
                                                             : L" — в срока")
                          : L"\
✔ приключена";
        ui.tooltip(b, tip);
        if (ui.hot(b)) ui.cursor = LoadCursorW(nullptr, IDC_HAND);
        if (ui.press(ui.id("acbar", (int)i), b)) clicked = true;
        float ol, orr;
        bool ocl, ocr;
        if (lateDays > 0 && clamp(due + 1, closedNo, ol, orr, ocl, ocr)) {
          RectF ob(ol, rr.t + 6, orr, rr.t + 22);
          ui.p->rrect(ob, 3, red.alpha(0.30f));
          ui.p->rrectStroke(ob.inset(0.5f), 3, red, 1.f);
        }
        if (closedNo && closedNo >= fromNo && closedNo <= toNo) {
          float mx = xOf(closedNo) + (xOf(closedNo + 1) - xOf(closedNo)) / 2;
          Color mc = lateDays > 0 ? red : green;
          ui.p->rrect(RectF(mx - 1, rr.t + 2, mx + 1, rr.b - 2), 0, mc);
          ui.text(RectF(mx - 8, rr.t - 4, mx + 8, rr.t + 8), L"✔", 8.f, mc, 700, Align::Center);
        }
      }
      if (clicked) {
        auditCard_ = auditCard_ == c.id ? 0 : c.id;
        ui.scrollTo(ui.id("acards"), 0);
      }
    }
  }
  ui.endScroll();
  if (todayNo >= fromNo && todayNo <= toNo) {  // today: a red line over everything
    float tx = xOf(todayNo) + (xOf(todayNo + 1) - xOf(todayNo)) / 2;
    ui.p->rrect(RectF(tx - 1, bodyR.t, tx + 1, bodyR.b), 0, red);
  }
  if (!chosen) return;

  // ---- the chosen card, with everything a card has in the table
  float cy = chartB + 6;
  ui.text(RectF(area.l + 4, cy, area.r - 40, cy + 24), L"Избраната карта", 12.5f, P.text, 600);
  if (ui.iconButton(ui.id("acl"), RectF(area.r - 32, cy - 2, area.r, cy + 26), kClose, L"Затвори")) auditCard_ = 0;
  std::vector<const SumCard*> one = {chosen};
  paintCards(RectF(area.l, cy + 28, area.r, area.b), one, ui.id("acards"));
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
      clientPane_ = 0;
      clientAsked_ = 0;  // its nomenclature is (re)loaded below
      ui.scrollTo(ui.id("ccards"), 0);
      ui.scrollTo(ui.id("cnom"), 0);
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
  // Its assigned nomenclature comes from GET /clients/:id — asked once per choice (a refusal must not be asked again).
  if (clientAsked_ != sel->id) {
    clientAsked_ = sel->id;
    int id = sel->id;
    after([id] { app().loadClient(id); });
  }
  std::vector<const SumCard*> cards;
  for (auto& k : a.summary.cards)
    if (k.clientId == sel->id && !k.closed) cards.push_back(&k);
  // Two panes, as the person needs them: what is assigned to the client (as the browser's client screen) — or its cards.
  int pane = clientPane_;
  std::vector<std::wstring> panes = {L"Номенклатура", L"Отворени карти (" + num((int)cards.size()) + L")"};
  if (ui.segmented(ui.id("cpane"), RectF(area.l, y + 8, area.l + 380, y + 38), pane, panes)) clientPane_ = pane;
  if (ui.iconButton(ui.id("cclose"), RectF(area.r - 32, y + 9, area.r, y + 37), kClose, L"Затвори — всички клиенти")) selClient_ = 0;
  y += 46;
  if (clientPane_ == 0) {
    paintClientNomenclature(RectF(area.l, y, area.r, area.b));
    return;
  }
  std::wstring t = L"Отворените карти на клиента в изгледа „" + a.summary.subviewLabel + L"“";
  ui.text(RectF(area.l + 4, y, area.r - 40, y + 24), t, 12.5f, P.dim, 600);
  paintCards(RectF(area.l, y + 28, area.r, area.b), cards, ui.id("ccards"));
}

// The client's assigned nomenclature, read-only, as the browser's client screen lists it (GET /clients/:id — the server
// already limits it to the person's business lines): per business line, the deadlines before the monthly services, by
// code. The value of a column is the client's own setting, else the activity's default — what the browser shows.
void Panel::paintClientNomenclature(RectF r) {
  App& a = app();
  const UiPalette& P = ui.pal;
  const ClientDetail& d = a.clientDetail;
  const Color green = P.dark ? Color::hex(0x4ADE80) : Color::hex(0x15803D);
  const Color amber = P.dark ? Color::hex(0xFBBF24) : Color::hex(0xB45309);
  if (!d.loaded || d.id != selClient_) {
    bool err = !a.clientError.empty() && !a.clientBusy;
    ui.text(RectF(r.l + 4, r.t, r.r, r.t + 40), err ? a.clientError : L"Зарежда номенклатурата…", 12.5f, err ? P.danger : P.dim, 400,
            Align::Left, VAlign::Top, true);
    return;
  }
  static const wchar_t* kWeekdays[] = {L"", L"пн", L"вт", L"ср", L"чт", L"пт", L"сб", L"нд"};
  auto cycleText = [](int c) -> std::wstring {
    switch (c) {
      case -1: return L"Седмично";
      case 1: return L"Месечно";
      case 3: return L"Тримесечно";
      case 6: return L"Полугодишно";
      case 12: return L"Годишно";
      default: return c > 0 ? std::to_wstring(c) + L" мес." : L"—";
    }
  };
  auto dueText = [&](const ClientObligation& o) -> std::wstring {
    if (o.cycle == -1) return o.dueMonth >= 1 && o.dueMonth <= 7 ? kWeekdays[o.dueMonth] : L"—";  // weekly: the weekday
    if (o.dueDay <= 0) return L"—";
    wchar_t b[16];
    if (o.cycle != 1 && o.dueMonth > 0) swprintf(b, 16, L"%d.%02d", o.dueDay, o.dueMonth);
    else swprintf(b, 16, L"%d", o.dueDay);
    return b;
  };
  auto monthlyOf = [](const ClientObligation& o) -> int {  // the browser's `monthlyNormOf`
    if (o.normSeconds < 0) return -1;
    if (o.monthly) return o.normSeconds;
    return o.normSeconds / (o.cycle > 0 ? o.cycle : 1);
  };
  struct Col {
    const wchar_t* head;
    float w;
  };
  static const Col kCols[] = {{L"код", 50},  {L"вид", 78},        {L"прил.", 46},   {L"цикъл", 100}, {L"срок", 60}, {L"дни преди", 70},
                              {L"план", 62}, {L"месечно", 70}, {L"одобр.", 56}, {L"отговорник", 178}};
  float fixed = 0;
  for (auto& c : kCols) fixed += c.w;
  float nameW = std::max(180.f, r.w() - fixed);
  // header: код, дейност, then the rest
  float y = r.t;
  auto colX = [&](int i) {  // i: index into kCols, the name sits after the first
    float x = r.l + kCols[0].w + nameW;
    for (int k = 1; k < i; k++) x += kCols[k].w;
    return x;
  };
  ui.text(RectF(r.l + 8, y, r.l + kCols[0].w, y + 22), upper(kCols[0].head), 9.5f, P.dim, 600, Align::Left, VAlign::Bottom);
  ui.text(RectF(r.l + kCols[0].w, y, r.l + kCols[0].w + nameW, y + 22), L"ДЕЙНОСТ", 9.5f, P.dim, 600, Align::Left, VAlign::Bottom);
  for (int i = 1; i < 10; i++)
    ui.text(RectF(colX(i) + 2, y, colX(i) + kCols[i].w - 2, y + 22), upper(kCols[i].head), 9.5f, P.dim, 600, Align::Left, VAlign::Bottom);
  y += 26;
  ui.divider(RectF(r.l, y - 1, r.r, y));

  // per business line, in the server's order (a line the client has but the person does not see is not sent)
  struct Item {
    const ClientObligation* o;
    const std::wstring* line;  // non-null: a heading
    int count = 0, applicable = 0;
  };
  std::vector<Item> items;
  int planSum = 0, monthlySum = 0;
  std::vector<std::pair<int, std::wstring>> lines = d.lines;
  for (auto& o : d.obligations)  // a line not among the partners (should not happen): still shown
    if (std::none_of(lines.begin(), lines.end(), [&](auto& l) { return l.first == o.departmentId; })) lines.push_back({o.departmentId, L""});
  for (auto& line : lines) {
    std::vector<const ClientObligation*> rows;
    for (auto& o : d.obligations)
      if (o.departmentId == line.first) rows.push_back(&o);
    if (rows.empty()) continue;
    std::sort(rows.begin(), rows.end(), [](const ClientObligation* x, const ClientObligation* z) {
      if (x->monthly != z->monthly) return !x->monthly;  // deadlines first
      int cx = _wtoi(x->code.c_str()), cz = _wtoi(z->code.c_str());
      if (cx != cz) return cx < cz;
      return x->name < z->name;
    });
    Item h{nullptr, &line.second};
    h.count = (int)rows.size();
    for (auto* o : rows) h.applicable += o->applicable ? 1 : 0;
    items.push_back(h);
    for (auto* o : rows) {
      items.push_back({o, nullptr});
      if (o->applicable && o->normSeconds >= 0) {
        planSum += o->normSeconds;
        monthlySum += monthlyOf(*o);
      }
    }
  }
  RectF footer(r.l, r.b - 28, r.r, r.b);
  RectF listR(r.l, y, r.r, footer.t - 4);
  float contentH = 0;
  for (auto& it : items) contentH += it.o ? 28.f : 34.f;
  float off = ui.beginScroll(ui.id("cnom"), listR, contentH);
  float yy = listR.t + off;
  for (auto& it : items) {
    if (!it.o) {
      RectF hr(listR.l, yy, listR.r, yy + 34);
      yy += 34;
      if (hr.b < listR.t || hr.t > listR.b) continue;
      std::wstring t = (it.line->empty() ? L"Бизнес линия" : *it.line) + L"  ·  " + num(it.count) + L" дейности, " + num(it.applicable) + L" приложими";
      ui.text(RectF(hr.l + 6, hr.t + 8, hr.r, hr.b), t, 12.5f, P.text, 700, Align::Left, VAlign::Bottom);
      continue;
    }
    const ClientObligation& o = *it.o;
    RectF row(listR.l, yy, listR.r, yy + 28);
    yy += 28;
    if (row.b < listR.t || row.t > listR.b) continue;
    if (o.approval) ui.p->rrect(row, 4, amber.withA(P.dark ? 0.14f : 0.10f));  // the browser's amber: needs approval
    Color fg = o.applicable ? P.text : P.faint;
    ui.text(RectF(row.l + 8, row.t, row.l + kCols[0].w, row.b), o.code, 12.f, fg, 600);
    RectF nameR(row.l + kCols[0].w, row.t, row.l + kCols[0].w + nameW - 6, row.b);
    ui.text(nameR, o.name, 12.f, fg);
    if (!o.hint.empty()) ui.tooltip(nameR, o.hint);
    auto cell = [&](int i, const std::wstring& t, Color c, int weight = 400) {
      ui.text(RectF(colX(i) + 2, row.t, colX(i) + kCols[i].w - 4, row.b), t, 12.f, c, weight);
    };
    cell(1, o.monthly ? L"услуга" : L"срок", P.dim);
    cell(2, o.applicable ? L"✓" : L"—", o.applicable ? green : P.faint, 700);
    cell(3, cycleText(o.cycle), fg);
    cell(4, dueText(o), fg);
    cell(5, o.lead > 0 ? num(o.lead) : L"—", fg);
    cell(6, o.normSeconds >= 0 ? hm(o.normSeconds) : L"—", o.normSeconds >= 0 ? fg : P.faint);
    int m = monthlyOf(o);
    cell(7, m >= 0 ? hm(m) : L"—", m >= 0 ? fg : P.faint);
    cell(8, o.approval ? L"✓" : L"", amber, 700);
    cell(9, o.assignee.empty() ? L"—" : o.assignee, o.assignee.empty() ? P.faint : fg);
  }
  ui.endScroll();
  ui.divider(RectF(r.l, footer.t, r.r, footer.t + 1));
  ui.text(RectF(footer.l + 6, footer.t, footer.r, footer.b),
          L"Приложимите:  планирано време " + hm(planSum) + L"  ·  месечно " + hm(monthlySum) +
              L"  ·  промени по номенклатурата — в браузъра",
          12.f, P.dim);
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
      std::string pd = addDays(d.date, -1);
      after([pd] { app().loadDay(pd); });
    }
    ui.text(dateR, longDate(d.date), 12.5f, P.text, 600, Align::Center);
    if (ui.iconButton(ui.id("dnext"), next, kNext, L"Следващ ден", !a.dayBusy && !today)) {
      std::string nd = addDays(d.date, 1);
      after([nd] { app().loadDay(nd); });
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

  // ---- what is recorded, per activity
  std::map<int, int> recorded, recordedSub;
  std::map<int, std::wstring> recordedNote;
  std::map<int, std::vector<const DayEntry*>> entriesOf;
  for (auto& e : d.entries) {
    recorded[e.activityId] += e.seconds;
    entriesOf[e.activityId].push_back(&e);
    if (recordedNote[e.activityId].empty()) recordedNote[e.activityId] = e.note;
    if (!recordedSub[e.activityId]) recordedSub[e.activityId] = e.substituteId;
  }
  std::map<int, const DayActivity*> act;
  for (auto& r : d.activities) {
    act[r.id] = &r;
    for (auto& c : r.children) act[c.id] = &c;
  }
  auto serverLines = [&](const DayActivity& x) {  // a line activity's lines as the server has them
    std::vector<DayLineDraft> v;
    for (auto* e : entriesOf[x.id]) {
      DayLineDraft l;
      l.colleagueId = e->colleagueId;
      l.clientId = e->clientId ? e->clientId : (x.clientMode == "or_na" ? -1 : 0);
      l.input = e->seconds ? hm(e->seconds) : L"";
      l.note = e->note;
      v.push_back(l);
    }
    return v;
  };
  // A new answer from the server: the form starts from it.
  if (dayFormSeq_ != a.daySeq) {
    dayFormSeq_ = a.daySeq;
    dayInput_.clear();
    dayNoteIn_.clear();
    dayLines_.clear();
    dayRanged_.clear();
    dayFrom_.clear();
    dayTo_.clear();
    daySub_.clear();
    for (auto& [id, secs] : recorded) dayInput_[id] = secs ? hm(secs) : L"";
    for (auto& [id, note] : recordedNote) dayNoteIn_[id] = note;
    for (auto& [id, sub] : recordedSub) daySub_[id] = sub;
    for (auto& [id, x] : act)
      if (x->lines) dayLines_[id] = serverLines(*x);
  }

  std::wstring totals = L"Карти " + hm(d.cardSeconds) + L"   ·   Административно " + hm(d.adminSeconds) + L"   ·   Неработно време " +
                        hm(d.nonWorkingSeconds) + L"   ·   Общо " + hm(d.totalSeconds);
  ui.text(RectF(area.l + 4, y, area.r, y + 22), totals, 12.5f, P.dim);
  y += 26;
  if (!a.dayError.empty()) {
    ui.text(RectF(area.l + 4, y, area.r, y + 36), a.dayError, 12.f, P.danger, 400, Align::Left, VAlign::Top, true);
    y += 38;
  }

  // ---- what changed: rows (with an absence's period and substitute) and line sets (all of an activity's lines)
  std::vector<App::DayRowIn> rowsOut;
  std::vector<App::DayLineSetIn> setsOut;
  std::set<int> changedIds;
  for (auto& [id, xp] : act) {
    const DayActivity& x = *xp;
    if (x.roleLocked) continue;
    if (x.lines) {
      auto& drafts = dayLines_[id];
      auto server = serverLines(x);
      bool same = drafts.size() == server.size();
      for (size_t i = 0; same && i < drafts.size(); i++)
        same = drafts[i].colleagueId == server[i].colleagueId && drafts[i].clientId == server[i].clientId &&
               trim(drafts[i].input) == server[i].input && trim(drafts[i].note) == trim(server[i].note);
      if (!same) {
        App::DayLineSetIn set;
        set.activityId = id;
        for (auto& l : drafts) set.lines.push_back({l.colleagueId, l.clientId, l.input, l.note});
        setsOut.push_back(set);
        changedIds.insert(id);
      }
    } else if (x.acceptsTime) {
      std::wstring was = recorded[id] ? hm(recorded[id]) : L"";
      bool ranged = x.isAbsence && dayRanged_.count(id) && !trim(dayFrom_[id]).empty() && !trim(dayTo_[id]).empty();
      bool changed = trim(dayInput_[id]) != was || trim(dayNoteIn_[id]) != trim(recordedNote[id]) || ranged ||
                     (x.isAbsence && daySub_[id] != recordedSub[id]);
      if (changed) {
        App::DayRowIn row;
        row.activityId = id;
        row.input = dayInput_[id];
        row.note = dayNoteIn_[id];
        if (ranged) {
          row.from = isoFromDmy(dayFrom_[id]);
          row.to = isoFromDmy(dayTo_[id]);
        }
        if (x.isAbsence) row.substituteId = daySub_[id];
        rowsOut.push_back(row);
        changedIds.insert(id);
      }
    }
  }

  // ---- the rows to draw
  enum class K { Head, Row, Range, Sub, LineHead, Line, NoLines };
  struct Item {
    K k;
    const DayActivity* x;
    int line;
    float h;
  };
  std::vector<Item> items;
  for (auto& r : d.activities) {
    items.push_back({K::Head, &r, -1, 32});
    std::vector<const DayActivity*> xs;
    if (r.children.empty() && (r.acceptsTime || r.lines)) xs.push_back(&r);
    for (auto& c : r.children)
      if (c.acceptsTime || c.lines) xs.push_back(&c);
    for (auto* x : xs) {
      if (x->lines) {
        items.push_back({K::LineHead, x, -1, 42});
        int n = (int)dayLines_[x->id].size();
        if (n == 0) items.push_back({K::NoLines, x, -1, 24});
        for (int i = 0; i < n; i++) items.push_back({K::Line, x, i, 40});
      } else {
        items.push_back({K::Row, x, -1, 42});
        if (x->isAbsence && !x->roleLocked && x->acceptsTime) {
          bool on = dayRanged_.count(x->id) > 0;
          if (on) items.push_back({K::Range, x, -1, 38});
          if (on || !trim(dayInput_[x->id]).empty() || daySub_[x->id] > 0) items.push_back({K::Sub, x, -1, 38});
        }
      }
    }
  }

  // the pickers' lists — the server's (colleagues of the firm, my clients, the substitutes of my department)
  std::vector<std::wstring> colleagueNames = {L"— изберете колега —"};
  for (auto& o : d.colleagues) colleagueNames.push_back(o.name + (o.inactive ? L" (неактивен)" : L""));
  std::vector<std::wstring> subNames = {L"— без заместник —"};
  for (auto& o : d.substitutes) subNames.push_back(o.name);
  auto indexOf = [](const std::vector<DayOption>& v, int id) {
    for (size_t i = 0; i < v.size(); i++)
      if (v[i].id == id) return (int)i;
    return -1;
  };

  RectF bar(area.l, area.b - 40, area.r, area.b);
  RectF listR(area.l, y, area.r, bar.t - 8);
  float contentH = 0;
  for (auto& it : items) contentH += it.h;
  if (dayScrollWanted_ >= 0) {
    ui.scrollTo(ui.id("dayrows"), dayScrollWanted_);
    dayScrollWanted_ = -1;
  }
  float off = ui.beginScroll(ui.id("dayrows"), listR, contentH);
  float yy = listR.t + off;
  int addTo = 0, removeAct = 0, removeIdx = -1;
  for (auto& it : items) {
    RectF row(listR.l, yy, listR.r, yy + it.h - 4);
    yy += it.h;
    if (row.b < listR.t || row.t > listR.b) continue;
    const DayActivity& x = *it.x;
    int id = x.id;
    bool isChanged = changedIds.count(id) > 0;
    switch (it.k) {
      case K::Head:
        ui.text(RectF(row.l + 4, row.t + 6, row.r, row.b + 4), x.name, 13.f, P.text, 700, Align::Left, VAlign::Bottom);
        break;
      case K::Row:
      case K::LineHead: {
        ui.card(row, 6);
        if (isChanged) ui.p->rrect(RectF(row.l + 2, row.t + 4, row.l + 6, row.b - 4), 2.f, P.accent);
        std::wstring name = x.name + (x.isAbsence ? L"  ·  отсъствие" : L"");
        RectF nameR(row.l + 14, row.t, row.r - 610, row.b);
        ui.text(nameR, name, 12.5f, P.text);
        if (!x.hint.empty()) ui.tooltip(nameR, x.hint);
        int rec = recorded[id];
        ui.text(RectF(row.r - 606, row.t, row.r - 506, row.b), rec ? L"записано " + hm(rec) : L"", 11.5f, P.dim, 400, Align::Right);
        if (x.roleLocked) {
          ui.glyph(RectF(row.r - 496, row.t, row.r - 472, row.b), kLock, 13.f, P.faint);
          ui.text(RectF(row.r - 468, row.t, row.r - 10, row.b), L"не е за вашата роля — може само да се изчисти в браузъра", 11.5f, P.faint);
          break;
        }
        if (it.k == K::LineHead) {
          std::wstring who = x.asksColleague && x.clientMode != "none" ? L"по колега и клиент" : x.asksColleague ? L"по колега" : L"по клиент";
          ui.text(RectF(row.r - 496, row.t, row.r - 140, row.b), L"записва се на редове " + who, 11.5f, P.dim);
          if (ui.button(ui.id("dladd", id), RectF(row.r - 132, row.t + 5, row.r - 8, row.b - 5), L"+ ред", Btn::Standard, nullptr, !a.dayBusy))
            addTo = id;
          break;
        }
        if (x.isAbsence) {  // 📅 — the same time on each day of a period
          bool on = dayRanged_.count(id) > 0;
          RectF cb(row.r - 500, row.t + 5, row.r - 466, row.b - 5);
          if (ui.iconButton(ui.id("drng", id), cb, L"\xE787", on ? L"Само за деня" : L"Същото време за всеки ден от период", !a.dayBusy)) {
            if (on) {
              dayRanged_.erase(id);
            } else {
              dayRanged_.insert(id);
              if (trim(dayFrom_[id]).empty()) dayFrom_[id] = dmyFromIso(d.date);
              if (trim(dayTo_[id]).empty()) dayTo_[id] = dmyFromIso(d.date);
            }
          }
          if (on) ui.p->rrectStroke(cb.inset(0.5f), 5, P.accent, 1.5f);
        }
        ui.textBox(ui.id("din", id), RectF(row.r - 460, row.t + 4, row.r - 372, row.b - 4), dayInput_[id], L"ч:мм");
        std::wstring noteHint = x.requiresNote ? (x.hint.empty() ? L"коментар (задължителен)" : x.hint) : L"коментар";
        ui.textBox(ui.id("dnote", id), RectF(row.r - 364, row.t + 4, row.r - 8, row.b - 4), dayNoteIn_[id], noteHint);
        break;
      }
      case K::Range: {
        float lx = row.l + 40;
        ui.text(RectF(lx, row.t, lx + 70, row.b), L"период от", 12.f, P.dim);
        ui.textBox(ui.id("dfrom", id), RectF(lx + 74, row.t + 4, lx + 194, row.b - 4), dayFrom_[id], L"дд.мм.гггг");
        ui.text(RectF(lx + 204, row.t, lx + 230, row.b), L"до", 12.f, P.dim);
        ui.textBox(ui.id("dto", id), RectF(lx + 234, row.t + 4, lx + 354, row.b - 4), dayTo_[id], L"дд.мм.гггг");
        ui.text(RectF(lx + 366, row.t, row.r - 8, row.b), L"същото време за всеки ден от периода — сървърът решава кои дни", 11.5f, P.faint);
        break;
      }
      case K::Sub: {
        float lx = row.l + 40;
        ui.text(RectF(lx, row.t, lx + 110, row.b), L"⮌ заместник", 12.f, P.dim);
        int idx = indexOf(d.substitutes, daySub_[id]) + 1;
        if (ui.combo(ui.id("dsub", id), RectF(lx + 114, row.t + 3, lx + 434, row.b - 3), idx, subNames))
          daySub_[id] = idx > 0 ? d.substitutes[(size_t)idx - 1].id : 0;
        if (d.substitutes.empty()) ui.text(RectF(lx + 444, row.t, row.r - 8, row.b), L"няма колеги от вашата бизнес линия", 11.5f, P.faint);
        break;
      }
      case K::NoLines:
        ui.text(RectF(row.l + 40, row.t, row.r, row.b), L"няма редове за деня — „+ ред“ добавя", 11.5f, P.faint);
        break;
      case K::Line: {
        auto& ls = dayLines_[id];
        if (it.line >= (int)ls.size()) break;
        DayLineDraft& l = ls[(size_t)it.line];
        int key = id * 64 + it.line;
        float lx = row.l + 40;
        if (x.asksColleague) {
          int idx = indexOf(d.colleagues, l.colleagueId) + 1;
          if (ui.combo(ui.id("dlcol", key), RectF(lx, row.t + 3, lx + 220, row.b - 3), idx, colleagueNames))
            l.colleagueId = idx > 0 ? d.colleagues[(size_t)idx - 1].id : 0;
          lx += 228;
        }
        if (x.clientMode != "none") {
          bool na = x.clientMode == "or_na";
          std::vector<std::wstring> names = {L"— изберете клиент —"};
          if (na) names.push_back(L"неприложимо");
          for (auto& o : d.clients) names.push_back(o.name + (o.inactive ? L" (неактивен)" : L""));
          int base = na ? 2 : 1;
          int idx = l.clientId < 0 ? 1 : l.clientId > 0 ? indexOf(d.clients, l.clientId) + base : 0;
          if (idx < 0) idx = 0;
          if (ui.combo(ui.id("dlcli", key), RectF(lx, row.t + 3, lx + 250, row.b - 3), idx, names))
            l.clientId = idx == 0 ? 0 : (na && idx == 1) ? -1 : d.clients[(size_t)(idx - base)].id;
          lx += 258;
        }
        ui.textBox(ui.id("dlt", key), RectF(lx, row.t + 3, lx + 88, row.b - 3), l.input, L"ч:мм");
        lx += 96;
        std::wstring noteHint = x.requiresNote ? (x.hint.empty() ? L"коментар (задължителен)" : x.hint) : L"коментар";
        ui.textBox(ui.id("dln", key), RectF(lx, row.t + 3, row.r - 44, row.b - 3), l.note, noteHint);
        if (ui.iconButton(ui.id("dlx", key), RectF(row.r - 38, row.t + 3, row.r - 6, row.b - 3), kClose, L"Махни реда", !a.dayBusy)) {
          removeAct = id;
          removeIdx = it.line;
        }
        break;
      }
    }
  }
  ui.endScroll();
  if (addTo) {
    const DayActivity* x = act[addTo];
    DayLineDraft l;
    if (x && x->clientMode == "or_na") l.clientId = 0;
    dayLines_[addTo].push_back(l);
  }
  if (removeAct && removeIdx >= 0 && removeIdx < (int)dayLines_[removeAct].size())
    dayLines_[removeAct].erase(dayLines_[removeAct].begin() + removeIdx);

  // ---- the bar
  int nChanged = (int)changedIds.size();
  if (!a.dayNote.empty() && nowSec() < a.dayNoteUntil) ui.text(RectF(bar.l + 4, bar.t, bar.l + 300, bar.b), L"✓ " + a.dayNote, 12.5f, green, 600);
  else ui.text(RectF(bar.l + 4, bar.t, bar.r - 420, bar.b), L"Време: 45 = 45 минути, 1:30 = час и половина. Празно — изтрива.", 11.5f, P.faint);
  std::wstring label = nChanged == 0 ? L"Няма промени" : L"Запиши промените (" + num(nChanged) + L")";
  if (ui.button(ui.id("dsave"), RectF(bar.r - 240, bar.t + 4, bar.r, bar.b - 4), label, Btn::Accent, kSave, nChanged > 0 && !a.dayBusy))
    after([rowsOut, setsOut] { app().saveDay(rowsOut, setsOut); });
  if (nChanged > 0 && ui.button(ui.id("dundo"), RectF(bar.r - 380, bar.t + 4, bar.r - 248, bar.b - 4), L"Отмени", Btn::Subtle, kClose, !a.dayBusy))
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
  int wq = toggleRow(ui.id("pwinq"), L"Win+Q — таблицата на цял екран", a.cfg.winq, true,
                     L"Затъмнява екрана и показва таблицата голяма; Esc или щракване встрани я затваря. Докато е включено, "
                     L"Win+Q не отваря търсенето на Windows — Win+S го отваря.");
  if (wq >= 0) after([wq] { app().setWinQ(wq == 1); });
  ui.text(RectF(c2.l + 18, yy, c2.r - 240, yy + 32), L"Версия " + widen(KD_VERSION_STR), 12.5f, P.text);
  if (ui.button(ui.id("pupd"), RectF(c2.r - 230, yy + 1, c2.r - 18, yy + 31), a.updateBusy ? L"Обновява се…" : L"Провери за обновление",
                Btn::Standard, kSync, !a.updateBusy))
    after([] { app().checkForUpdate(true); });
  yy += 44;
  if (ui.button(ui.id("pout"), RectF(c2.l + 18, c2.b - 48, c2.l + 220, c2.b - 16), L"Изход от профила", Btn::Danger, kSignOut))
    after([] { app().signOut(); });
}

}  // namespace kd
