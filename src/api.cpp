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
  return r;
}
}  // namespace

Summary parseSummary(const Json& j) {
  Summary s;
  auto sv = j.find("subview");
  if (!sv || !sv->isObj()) return s;
  s.loaded = true;
  s.subviewId = sv->str("id");
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
  if (auto v = j.find("subviews"); v && v->isArr())
    for (auto& x : v->a) s.subviews.push_back({x.str("id"), x.str("set"), wstr(x, "label")});
  if (auto rows = j.find("rows"); rows && rows->isArr())
    for (auto& r : rows->a) s.rows.push_back(parseRow(r));
  if (auto t = j.find("total"); t && t->isObj()) s.total = parseRow(*t);
  if (auto cards = j.find("cards"); cards && cards->isArr())
    for (auto& c : cards->a) {
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
      k.title = wstr(c, "title");
      k.clientName = wstr(c, "clientName");
      k.obligationCode = wstr(c, "obligationCode");
      k.dueDate = wstr(c, "dueDate");
      k.lane = wstr(c, "lane");
      k.worker = wstr(c, "worker");
      s.cards.push_back(std::move(k));
    }
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
    // Pending rows are shown on their own; "за разпределяне" counts only what is already decided.
    for (auto& e : u->a)
      if (!e.flag("pending", false)) t.unattributed++;
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
