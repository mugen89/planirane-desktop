// End-to-end check of the API layer against a running server (a development server, never production):
//
//   cmake -S . -B build -DKD_TESTS=ON && cmake --build build
//   set KD_TEST_SERVER=http://localhost:8106   (KD_TEST_USER, KD_TEST_PASSWORD likewise)
//   build\test_api.exe
//
// It signs in, starts / pauses / resumes / cancels a stopwatch without a card, stops one and discards it,
// then signs out — so it leaves no time behind. It refuses to run the timer part if a stopwatch is
// already running for that user (that one is real work).
#include <cstdio>
#include <cstdlib>

#include "api.h"

using namespace kd;
using namespace rt;

static int fails = 0;
static void check(bool c, const char* what) {
  printf("  %s %s\n", c ? "ok  " : "FAIL", what);
  if (!c) fails++;
}

static std::string env(const char* k) {
  const char* v = getenv(k);
  return v ? v : "";
}

struct Client {
  std::wstring base;
  std::string cookie;
  HttpResult last;
  Reply call(const wchar_t* method, const std::string& path, const std::string& body = {}) {
    HttpRequest r;
    r.method = method;
    r.url = base + widen(path);
    r.cookie = cookie;
    r.body = body;
    last = httpSend(r);
    Reply out;
    out.status = last.status;
    if (!last.body.empty()) Json::parse(last.body, out.json);
    return out;
  }
  TimeState state() {
    Reply r = call(L"GET", "/api/time/state");
    return r.ok() ? parseTimeState(r.json, unixNow()) : TimeState{};
  }
};

int main() {
  SetConsoleOutputCP(CP_UTF8);
  printf("pure functions\n");
  check(parseIsoTime("1970-01-02T00:00:01.500Z") == 86401.5, "parseIsoTime UTC with milliseconds");
  check(parseIsoTime("2026-11-26T10:00:00+02:00") == parseIsoTime("2026-11-26T08:00:00Z"), "parseIsoTime with an offset");
  check(parseIsoTime("garbage") == 0, "parseIsoTime refuses garbage");
  check(clockText(3725, true) == L"1:02:05" && clockText(3725, false) == L"1:02", "clockText");
  check(normalizeServer(L" localhost:8106/ ") == L"http://localhost:8106", "normalizeServer adds http:// and drops /");
  check(sessionCookieFrom({"x=1; Path=/", "kb_session_8106=abc; Path=/; HttpOnly"}) == "kb_session_8106=abc", "session cookie found");
  check(sessionCookieFrom({"kb_session_8106=; Max-Age=0"}).empty(), "a cleared session cookie is ignored");
  {
    Running r;
    r.accumulated = 100;
    r.paused = true;
    check(elapsedSeconds(r, 0) == 100, "paused clock = banked seconds");
    r.paused = false;
    r.segmentStart = unixNow() - 30;
    int e = elapsedSeconds(r, 0);
    check(e >= 129 && e <= 131, "running clock = banked + open segment");
  }

  std::string server = env("KD_TEST_SERVER"), user = env("KD_TEST_USER"), pass = env("KD_TEST_PASSWORD");
  if (server.empty() || user.empty() || pass.empty()) {
    printf("server part skipped: set KD_TEST_SERVER, KD_TEST_USER, KD_TEST_PASSWORD\n");
    return fails ? 1 : 0;
  }
  Client c;
  c.base = normalizeServer(widen(server));
  printf("server %s\n", narrow(c.base).c_str());

  Json body = Json::object();
  body.set("username", user);
  body.set("password", std::string("wrong-") + pass);
  Reply bad = c.call(L"POST", "/api/auth/login", body.dump());
  check(bad.status == 401, "a wrong password is refused (401)");

  body.set("password", pass);
  body.set("remember", true);
  Reply in = c.call(L"POST", "/api/auth/login", body.dump());
  c.cookie = sessionCookieFrom(c.last.setCookies);
  check(in.ok() && !c.cookie.empty(), "sign-in returns a session cookie");
  if (c.cookie.empty()) return 1;
  User u = in.json.find("user") ? parseUser(*in.json.find("user")) : User{};
  check(u.id > 0, "sign-in returns the user");

  Reply s = c.call(L"GET", "/api/auth/session");
  check(s.ok() && s.json.find("user") && s.json.find("user")->isObj(), "the session is recognised");

  TimeState t = c.state();
  check(t.canTrack, "the user may track time");
  printf("  clock skew %.2f s, pending %zu, unattributed %d (max %d)\n", t.skew, t.pending.size(), t.unattributed, t.maxUnassigned);
  bool timerTests = !t.hasRunning && t.pending.empty() && (t.maxUnassigned == 0 || t.unattributed < t.maxUnassigned);
  if (!timerTests) {
    printf("timer part skipped: this user has a running or undecided stopwatch, or is at the cap\n");
  } else {
    printf("stopwatch without a card\n");
    check(c.call(L"POST", "/api/time/start").ok(), "start");
    t = c.state();
    check(t.hasRunning && t.running.cardId == 0 && !t.running.paused, "running, no card");
    Sleep(2200);
    t = c.state();
    int e1 = elapsedSeconds(t.running, t.skew);
    check(e1 >= 2 && e1 <= 6, "the clock counts");
    check(c.call(L"POST", "/api/time/pause").ok(), "pause");
    t = c.state();
    int p1 = elapsedSeconds(t.running, t.skew);
    Sleep(1300);
    check(t.running.paused && elapsedSeconds(t.running, t.skew) == p1, "paused clock stands still");
    check(c.call(L"POST", "/api/time/resume").ok(), "resume");
    t = c.state();
    check(t.hasRunning && !t.running.paused, "running again");
    check(c.call(L"DELETE", "/api/time/running").ok(), "cancel deletes the session");
    t = c.state();
    check(!t.hasRunning, "nothing running after cancel");

    printf("stop -> pending -> discard\n");
    check(c.call(L"POST", "/api/time/start").ok(), "start again");
    Sleep(1100);
    Reply stop = c.call(L"POST", "/api/time/stop");
    check(stop.ok(), "stop");
    t = c.state();
    int pid = t.pending.empty() ? 0 : t.pending.front().id;
    check(!t.hasRunning && pid > 0 && t.pending.front().cardId == 0, "the stopped session waits for a decision");
    Reply again = c.call(L"POST", "/api/time/start");
    check(again.status == 409, "no new start while time waits for ✓/✕ (409)");
    if (pid) check(c.call(L"POST", "/api/time/entries/" + std::to_string(pid) + "/discard").ok(), "discard");
    t = c.state();
    check(t.pending.empty() && !t.hasRunning, "nothing left behind");
  }

  check(c.call(L"POST", "/api/auth/logout").ok(), "sign out");
  Reply gone = c.call(L"GET", "/api/time/state");
  check(gone.status == 401, "the old session no longer works (401)");
  printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
  return fails ? 1 : 0;
}
