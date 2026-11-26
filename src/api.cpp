#include "api.h"

namespace kd {
using namespace rt;

namespace {

int num(const Json& j, std::string_view k, int d = 0) {
  auto v = j.find(k);
  return v && v->t == Json::T::Num ? (int)v->n : d;
}
std::wstring wstr(const Json& j, std::string_view k) {
  auto v = j.find(k);
  return v && v->t == Json::T::Str ? widen(v->s) : std::wstring();
}

Reply decode(const HttpResult& r) {
  Reply out;
  out.status = r.status;
  if (!r.body.empty()) Json::parse(r.body, out.json);
  if (out.ok()) return out;
  if (r.status == 0) {
    out.error = netErrorText(r.error);
  } else if (auto e = out.json.find("error"); e && e->t == Json::T::Str && !e->s.empty()) {
    out.error = widen(e->s);
  } else if (r.status == 401) {
    out.error = L"Сесията е изтекла — влезте отново.";
  } else {
    wchar_t buf[64];
    swprintf(buf, 64, L"Сървърът отговори с грешка %d.", r.status);
    out.error = buf;
  }
  return out;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
long long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}

}  // namespace

std::string sessionCookieFrom(const std::vector<std::string>& setCookies) {
  for (auto& c : setCookies) {
    if (c.rfind("kb_session", 0) != 0) continue;
    std::string pair = c.substr(0, c.find(';'));
    if (pair.find('=') == std::string::npos || pair.back() == '=') continue;  // a cleared cookie
    return pair;
  }
  return {};
}

namespace {
rt::Color hexColor(const std::string& s) {
  rt::Color c;
  if (!rt::Color::parse(s, c)) c = rt::Color::hex(0x6C7A89);
  return c;
}
SumRow parseRow(const Json& j) {
  SumRow r;
  r.key = j.str("key");
  r.label = wstr(j, "label");
  if (auto b = j.find("byStatus"); b && b->isObj())
    for (auto& kv : b->o)
      if (kv.second.t == Json::T::Num) r.byStatus[kv.first] = (int)kv.second.n;
  r.open = num(j, "open");
  r.load = num(j, "load");
  r.awaiting = num(j, "awaiting");
  r.done = num(j, "done");
  r.planSeconds = num(j, "planSeconds");
  r.withClient = num(j, "withClient");
  r.noClient = num(j, "noClient");
  if (auto a = j.find("assignees"); a && a->isArr())
    for (auto& x : a->a)
      if (x.t == Json::T::Str) r.assignees.push_back(widen(x.s));
  if (auto c = j.find("complexity"); c && c->isObj()) {
    r.complexity = wstr(*c, "label");
    r.complexityHint = wstr(*c, "hint");
  }
  if (auto a = j.find("autonomy"); a && a->isObj()) {
    r.autonomy = num(*a, "pct");
    if (auto w = a->find("window"); w && w->isObj()) {
      r.autonomyWindow = wstr(*w, "label");
      r.autonomyRange = wstr(*w, "range");
      r.autonomyHint = wstr(*w, "hint");
    }
  }
  return r;
}
}  // namespace

namespace {
DayActivity parseActivity(const Json& n) {
  DayActivity a;
  a.id = num(n, "id");
  a.name = wstr(n, "name");
  a.hint = wstr(n, "hint");
  a.acceptsTime = n.flag("acceptsTime", false);
  a.requiresNote = n.flag("requiresNote", false);
  a.isAbsence = n.flag("isAbsence", false);
  a.roleLocked = n.flag("roleLocked", false);
  std::string mode = n.str("clientMode");
  a.lines = n.flag("asksColleague", false) || (!mode.empty() && mode != "none");
  if (auto c = n.find("children"); c && c->isArr())
    for (auto& x : c->a)
      if (x.flag("isActive", true)) a.children.push_back(parseActivity(x));
  return a;
}
}  // namespace

AdminDay parseDay(const Json& j) {
  AdminDay d;
  d.date = j.str("date");
  d.loaded = !d.date.empty();
  if (auto t = j.find("activities"); t && t->isArr())
    for (auto& n : t->a)
      if (n.flag("isActive", true)) d.activities.push_back(parseActivity(n));
  if (auto e = j.find("entries"); e && e->isArr())
    for (auto& x : e->a) d.entries.push_back({num(x, "activityId"), num(x, "seconds"), wstr(x, "note")});
  if (auto c = j.find("collectors"); c && c->isObj()) {
    d.adminSeconds = num(*c, "adminSeconds");
    d.nonWorkingSeconds = num(*c, "nonWorkingSeconds");
    d.cardSeconds = num(*c, "cardSeconds");
    d.totalSeconds = num(*c, "totalSeconds");
  }
  return d;
}

std::vector<ClientRow> parseClients(const Json& j) {
  std::vector<ClientRow> out;
  if (!j.isArr()) return out;
  for (auto& c : j.a) {
    ClientRow r;
    r.id = num(c, "id");
    r.name = wstr(c, "name");
    r.identifier = wstr(c, "identifier");
    r.complexity = wstr(c, "complexity_label");
    if (auto p = c.find("autonomy_pct"); p && p->t == Json::T::Num) r.autonomy = (int)p->n;
    r.owner = wstr(c, "owner_name");
    r.partner = wstr(c, "partner_name");
    r.deadlines = num(c, "deadline_count");
    r.active = c.flag("is_active", true);
    if (auto l = c.find("setup_lines"); l && l->isArr())
      for (auto& x : l->a) r.lines.push_back({wstr(x, "department"), x.flag("ok", false)});
    out.push_back(std::move(r));
  }
  return out;
}

ClientDetail parseClientDetail(const Json& j) {
  ClientDetail d;
  d.id = num(j, "id");
  d.loaded = d.id > 0;
  if (auto p = j.find("departmentPartners"); p && p->isArr())
    for (auto& x : p->a) d.lines.push_back({num(x, "department_id"), wstr(x, "department_name")});
  auto numOr = [](const Json& o, std::string_view override_, std::string_view fallback, int none) {
    if (auto v = o.find(override_); v && v->t == Json::T::Num) return (int)v->n;
    if (auto v = o.find(fallback); v && v->t == Json::T::Num) return (int)v->n;
    return none;
  };
  if (auto obs = j.find("obligations"); obs && obs->isArr())
    for (auto& o : obs->a) {
      if (!o.flag("is_active", true)) continue;
      ClientObligation c;
      c.departmentId = num(o, "department_id");
      c.cycle = numOr(o, "cycle_months_override", "default_cycle_months", 1);
      c.dueDay = numOr(o, "due_day_override", "default_due_day", 0);
      c.dueMonth = numOr(o, "anchor_month", "default_due_month", 0);
      c.lead = num(o, "effective_lead_days");
      c.normSeconds = numOr(o, "norm_seconds", "obligation_norm_seconds", -1);
      c.applicable = o.flag("is_applicable", false);
      c.approval = o.flag("requires_approval", false);
      c.monthly = o.str("nomenclature_kind") == "monthly";
      c.code = wstr(o, "code");
      c.name = wstr(o, "obligation_name");
      c.hint = wstr(o, "obligation_hint");
      c.assignee = wstr(o, "assigned_user_name");
      d.obligations.push_back(std::move(c));
    }
  return d;
}

Me parseMe(const Json& j) {
  Me m;
  m.loaded = j.isObj();
  m.name = wstr(j, "name");
  m.username = wstr(j, "username");
  m.email = wstr(j, "email");
  m.jobTitle = wstr(j, "job_title");
  m.department = wstr(j, "department_name");
  m.mobile = wstr(j, "mobile_phone");
  m.internalPhone = wstr(j, "internal_phone");
  if (auto t = j.find("teams"); t && t->isArr())
    for (auto& x : t->a) m.teams.push_back(wstr(x, "name"));
  return m;
}

AttachNeeds parseAttachNeeds(const Json& j, const std::wstring& fallback) {
  AttachNeeds n;
  n.code = j.str("error");
  n.message = wstr(j, "message");
  if (n.message.empty()) n.message = fallback;
  n.needsDetail = j.flag("needsDetail", false);
  n.maxChars = num(j, "maxChars");
  if (auto s = j.find("optionSet"); s && s->isObj()) {
    n.setName = wstr(*s, "name");
    n.multiSelect = s->flag("multiSelect", false);
    if (auto it = s->find("items"); it && it->isArr())
      for (auto& i : it->a) n.options.push_back({num(i, "id"), wstr(i, "name"), wstr(i, "hint")});
  }
  if (auto ids = j.find("optionIds"); ids && ids->isArr())
    for (auto& v : ids->a)
      if (v.t == Json::T::Num) n.chosen.push_back((int)v.n);
  n.activity = wstr(j, "activity");
  n.comment = wstr(j, "comment");
  return n;
}

SumCard parseCard(const Json& c) {
  SumCard k;
  k.id = num(c, "id");
  k.boardId = num(c, "boardId");
  k.normSeconds = num(c, "normSeconds");
  auto cid = c.find("clientId");
  k.hasClient = cid && cid->t == Json::T::Num;
  k.awaiting = c.flag("awaiting", false);
  k.closed = c.flag("closed", false);
  k.status = c.str("status");
  k.row = c.str("row");
  k.laneKey = c.str("laneKey");
  k.clientId = num(c, "clientId");
  if (auto p = c.find("period"); p && p->isObj()) {
    k.periodTag = wstr(*p, "tag");
    k.periodHint = wstr(*p, "hint");
    k.carryOver = p->flag("carryOver", false);
  }
  k.title = wstr(c, "title");
  k.clientName = wstr(c, "clientName");
  k.obligationCode = wstr(c, "obligationCode");
  k.dueDate = wstr(c, "dueDate");
  k.lane = wstr(c, "lane");
  k.worker = wstr(c, "worker");
  k.notYetVisible = c.flag("notYetVisible", false);
  k.startDate = wstr(c, "startDate");
  k.closedAt = wstr(c, "closedAt");
  k.visibleFrom = wstr(c, "visibleFrom");
  if (auto b = c.find("band"); b && b->isObj())
    if (auto h = b->find("color"); h && h->t == Json::T::Str) {
      k.band = hexColor(h->s);
      k.hasBand = true;
      k.bandLabel = wstr(*b, "label");
    }
  return k;
}

namespace {
AuditAgg parseAgg(const Json* a) {
  AuditAgg g;
  if (!a || !a->isObj()) return g;
  g.start = a->str("start");
  g.end = a->str("end");
  g.lastClosed = a->str("lastClosed");
  g.total = num(*a, "total");
  g.done = num(*a, "done");
  g.late = a->flag("late", false);
  return g;
}
}  // namespace

AuditTimeline parseAudit(const Json& j) {
  AuditTimeline t;
  t.today = j.str("today");
  t.loaded = !t.today.empty();
  if (auto ps = j.find("projects"); ps && ps->isArr())
    for (auto& p : ps->a) {
      AuditProject ap;
      ap.id = num(p, "id");
      ap.name = wstr(p, "name");
      ap.agg = parseAgg(p.find("agg"));
      if (auto ts = p.find("tasks"); ts && ts->isArr())
        for (auto& x : ts->a) {
          AuditTask at;
          at.title = wstr(x, "title");
          at.agg = parseAgg(x.find("agg"));
          if (auto cs = x.find("cards"); cs && cs->isArr())
            for (auto& c : cs->a) at.cards.push_back(parseCard(c));
          ap.tasks.push_back(std::move(at));
        }
      t.projects.push_back(std::move(ap));
    }
  return t;
}

Calendar parseCalendar(const Json& j) {
  Calendar cal;
  if (auto p = j.find("period"); p && p->isObj()) {
    cal.year = num(*p, "year");
    cal.month = num(*p, "month");
  }
  cal.loaded = cal.year > 0;
  if (auto v = j.find("subview"); v && v->isObj()) cal.subviewLabel = wstr(*v, "label");
  if (auto cards = j.find("cards"); cards && cards->isArr())
    for (auto& c : cards->a) cal.cards.push_back(parseCard(c));
  return cal;
}

Summary parseSummary(const Json& j) {
  Summary s;
  auto sv = j.find("subview");
  if (!sv || !sv->isObj()) return s;
  s.loaded = true;
  s.subviewId = sv->str("id");
  s.hasAuditCards = j.flag("hasAuditCards", false);
  s.table = sv->str("table");
  s.subviewLabel = wstr(*sv, "label");
  s.nameHead = wstr(j, "nameHead");
  if (auto l = j.find("layout"); l && l->isArr())
    for (auto& c : l->a) {
      SumColumn col;
      col.key = c.str("key");
      col.head = wstr(c, "head");
      col.sub = wstr(c, "sub");
      if (auto h = c.find("color"); h && h->t == Json::T::Str) {
        col.color = hexColor(h->s);
        col.hasColor = true;
      }
      s.layout.push_back(std::move(col));
    }
  if (auto cs = j.find("columns"); cs && cs->isArr())
    for (auto& c : cs->a)
      if (auto h = c.find("color"); h && h->t == Json::T::Str) s.bandColors[c.str("key")] = hexColor(h->s);
  if (auto v = j.find("subviews"); v && v->isArr())
    for (auto& x : v->a) s.subviews.push_back({x.str("id"), x.str("set"), wstr(x, "label")});
  if (auto rows = j.find("rows"); rows && rows->isArr())
    for (auto& r : rows->a) s.rows.push_back(parseRow(r));
  if (auto t = j.find("total"); t && t->isObj()) s.total = parseRow(*t);
  if (auto cards = j.find("cards"); cards && cards->isArr())
    for (auto& c : cards->a) s.cards.push_back(parseCard(c));
  return s;
}

double unixNow() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER u{{ft.dwLowDateTime, ft.dwHighDateTime}};
  return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
}

double parseIsoTime(const std::string& s) {
  int y, mo, d, h, mi;
  double sec = 0;
  char tail[16]{};
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf%15s", &y, &mo, &d, &h, &mi, &sec, tail) < 6) return 0;
  double t = (double)daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400.0 + h * 3600.0 + mi * 60.0 + sec;
  // The server sends UTC ("Z"); an explicit offset is honoured too.
  std::string z = tail;
  if (!z.empty() && (z[0] == '+' || z[0] == '-')) {
    int oh = 0, om = 0;
    if (sscanf(z.c_str() + 1, "%d:%d", &oh, &om) >= 1) t -= (z[0] == '+' ? 1 : -1) * (oh * 3600.0 + om * 60.0);
  }
  return t;
}

std::wstring normalizeServer(std::wstring s) {
  s = trim(s);
  if (s.empty()) return s;
  if (s.find(L"://") == std::wstring::npos) s = L"http://" + s;
  while (!s.empty() && s.back() == L'/') s.pop_back();
  return s;
}

User parseUser(const Json& j) {
  User u;
  u.id = num(j, "id");
  u.name = wstr(j, "name");
  u.username = wstr(j, "username");
  u.isAdmin = j.flag("isAdmin", false);
  u.mustChangePassword = j.flag("mustChangePassword", false);
  return u;
}

TimeState parseTimeState(const Json& j, double receivedAt) {
  TimeState t;
  t.canTrack = j.flag("canTrack", false);
  t.maxUnassigned = num(j, "maxUnassigned");
  t.todaySeconds = num(j, "todaySeconds");
  if (double now = parseIsoTime(j.str("serverNow")); now > 0) t.skew = now - receivedAt;
  if (auto r = j.find("running"); r && r->isObj()) {
    t.hasRunning = true;
    t.running.id = num(*r, "id");
    t.running.cardId = num(*r, "cardId");
    t.running.cardTitle = wstr(*r, "cardTitle");
    t.running.clientName = wstr(*r, "clientName");
    t.running.accumulated = r->num("accumulatedSeconds", 0);
    t.running.paused = r->str("pausedAt").size() > 0;
    t.running.segmentStart = parseIsoTime(r->str("segmentStartedAt"));
  }
  if (auto p = j.find("pending"); p && p->isArr()) {
    for (auto& e : p->a) {
      Pending x;
      x.id = num(e, "id");
      x.cardId = num(e, "cardId");
      x.seconds = num(e, "seconds");
      x.cardTitle = wstr(e, "cardTitle");
      x.clientName = wstr(e, "clientName");
      x.detailRequired = e.flag("detailRequired", false);
      t.pending.push_back(std::move(x));
    }
  }
  if (auto u = j.find("unattributed"); u && u->isArr()) {
    // Pending rows are shown on their own (✓ / ✕ first); "за разпределяне" is what is already decided.
    for (auto& e : u->a)
      if (!e.flag("pending", false)) {
        t.unattributed++;
        Unassigned x;
        x.id = num(e, "id");
        x.seconds = num(e, "seconds");
        x.workDate = wstr(e, "workDate");
        x.note = wstr(e, "note");
        t.unassigned.push_back(std::move(x));
      }
  }
  return t;
}

int elapsedSeconds(const Running& r, double skew) {
  double banked = std::max(0.0, std::round(r.accumulated));
  if (r.paused || r.segmentStart <= 0) return (int)banked;
  double open = std::floor(unixNow() + skew - r.segmentStart);
  return (int)(banked + std::max(0.0, open));
}

std::wstring clockText(int s, bool withSeconds) {
  if (s < 0) s = 0;
  wchar_t buf[32];
  if (withSeconds) swprintf(buf, 32, L"%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  else swprintf(buf, 32, L"%d:%02d", s / 3600, s / 60 % 60);
  return buf;
}

// ---------------------------------------------------------------- calls
void Api::send(const wchar_t* method, const std::string& path, const Json* body, std::function<void(const Reply&)> done) {
  HttpRequest r;
  r.method = method;
  r.url = base + widen(path);
  r.cookie = cookie;
  if (body) r.body = body->dump();
  netPost(std::move(r), [done = std::move(done)](const HttpResult& res) { done(decode(res)); });
}

void Api::login(const std::wstring& user, const std::wstring& pass, std::function<void(const Reply&, const User&)> done) {
  Json b = Json::object();
  b.set("username", narrow(user));
  b.set("password", narrow(pass));
  b.set("remember", true);  // the 90-day sliding session; the password itself is never kept
  HttpRequest r;
  r.method = L"POST";
  r.url = base + L"/api/auth/login";
  r.body = b.dump();
  netPost(std::move(r), [this, done = std::move(done)](const HttpResult& res) {
    Reply rep = decode(res);
    User u;
    if (rep.ok()) {
      // The cookie is named after the server's port (kb_session_8106), so take whichever kb_session_* came back.
      cookie = sessionCookieFrom(res.setCookies);
      if (auto ju = rep.json.find("user"); ju && ju->isObj()) u = parseUser(*ju);
      if (cookie.empty() || !u.id) {
        rep.status = 500;
        rep.error = L"Сървърът не върна сесия. Проверете адреса (трябва да е адресът на приложението).";
      }
    }
    done(rep, u);
  });
}

void Api::logout(std::function<void()> done) {
  send(L"POST", "/api/auth/logout", nullptr, [done = std::move(done)](const Reply&) {
    if (done) done();
  });
  // Forget it now, not when the answer comes: a new sign-in may already be under way by then.
  cookie.clear();
}

void Api::session(std::function<void(const Reply&, const User&)> done) {
  send(L"GET", "/api/auth/session", nullptr, [done = std::move(done)](const Reply& r) {
    User u;
    if (auto ju = r.json.find("user"); r.ok() && ju && ju->isObj()) u = parseUser(*ju);
    done(r, u);
  });
}

void Api::timeState(std::function<void(const Reply&, const TimeState&)> done) {
  send(L"GET", "/api/time/state", nullptr, [done = std::move(done)](const Reply& r) {
    TimeState t;
    if (r.ok()) t = parseTimeState(r.json, unixNow());
    done(r, t);
  });
}

}  // namespace kd
