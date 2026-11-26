// The kanban server's REST API — the ONLY way this app reads or changes data.
//
// Deliberately not the database: every rule (permissions, the вх.№ gate, ✓/✕ time confirmation, the cap
// on time without a card, the audit logs) lives in the server, so going through it keeps the desktop
// and the browser consistent by construction. Same address, same username and password as the browser.
#pragma once
#include "common.h"
#include "json.h"
#include "net.h"

namespace kd {

struct User {
  int id = 0;
  std::wstring name, username;
  bool isAdmin = false, mustChangePassword = false;
};

// The running хронометър (GET /time/state → running). The clock is computed like elapsedOfSession() on
// the web: banked seconds plus the open segment, never "now - startedAt" (that ignores pauses).
struct Running {
  int id = 0, cardId = 0;
  std::wstring cardTitle, clientName;
  double accumulated = 0;
  double segmentStart = 0;  // unix seconds on the server's clock; 0 = no open segment
  bool paused = false;
};

// A stopped session waiting for ✓ / ✕ (the time verification model: stop is a proposal).
struct Pending {
  int id = 0, cardId = 0, seconds = 0;
  std::wstring cardTitle, clientName;
  bool detailRequired = false;  // проектна задача: ✓ needs Дейност + Коментар (done in the browser for now)
};

struct TimeState {
  bool canTrack = false;
  int maxUnassigned = 0;
  bool hasRunning = false;
  Running running;
  std::vector<Pending> pending;
  int unattributed = 0;   // finished entries without a card ("за разпределяне")
  int todaySeconds = 0;
  double skew = 0;        // server clock minus this PC's clock, seconds
};

struct Reply {
  int status = 0;         // 0: the server was not reached
  rt::Json json;
  std::wstring error;     // ready to show when !ok()
  bool ok() const { return status >= 200 && status < 300; }
};

class Api {
 public:
  std::wstring base;      // "http://host:8106", no trailing slash
  std::string cookie;     // "kb_session_<port>=…", empty when signed out

  void send(const wchar_t* method, const std::string& path, const rt::Json* body, std::function<void(const Reply&)> done);
  void login(const std::wstring& user, const std::wstring& pass, std::function<void(const Reply&, const User&)> done);
  void logout(std::function<void()> done);
  void session(std::function<void(const Reply&, const User&)> done);
  void timeState(std::function<void(const Reply&, const TimeState&)> done);
};

// The session cookie from a login answer: kb_session_<port>=… (the name follows the server's port).
std::string sessionCookieFrom(const std::vector<std::string>& setCookies);

double unixNow();
double parseIsoTime(const std::string& s);          // "2026-11-26T08:32:11.123Z" → unix seconds, 0 if unreadable
std::wstring normalizeServer(std::wstring s);       // adds http://, drops the trailing slash
User parseUser(const rt::Json& j);
TimeState parseTimeState(const rt::Json& j, double receivedAt);
int elapsedSeconds(const Running& r, double skew);
std::wstring clockText(int seconds, bool withSeconds);  // 1:02:03 / 1:02

}  // namespace kd
