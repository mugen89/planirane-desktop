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

// A finished entry without a card („за разпределяне“): the panel attaches it to a card (PATCH /time/entries/:id).
struct Unassigned {
  int id = 0, seconds = 0;
  std::wstring workDate, note;  // workDate: "2026-11-26"
};

struct TimeState {
  bool canTrack = false;
  int maxUnassigned = 0;
  bool hasRunning = false;
  Running running;
  std::vector<Pending> pending;
  int unattributed = 0;   // finished entries without a card ("за разпределяне")
  std::vector<Unassigned> unassigned;  // the same entries, listed (the oldest first, as the server sends them)
  int todaySeconds = 0;
  double skew = 0;        // server clock minus this PC's clock, seconds
};

// The summary table of the unified board (GET /desktop/summary): computed by the server with the same code as the
// browser's table, so the numbers cannot differ. The layout says which columns, in which order.
struct SumColumn {
  std::string key;          // "plan", "status:<band>", "approval", "tasks", "clients", "count", "done", "exec"
  std::wstring head, sub;
  rt::Color color;
  bool hasColor = false;
};
struct SumRow {
  std::string key;
  std::wstring label;
  std::map<std::string, int> byStatus;
  int open = 0, load = 0, awaiting = 0, done = 0, planSeconds = 0, withClient = 0, noClient = 0;
  std::vector<std::wstring> assignees;
  // „сл.“ / „самост.“ — only in the tables by client (web CHANGELOG 333): what the browser shows, resolved by the server.
  std::wstring complexity, complexityHint;                   // the level's label ("4") and its text
  int autonomy = -1;                                         // %, -1 = not set for this client
  std::wstring autonomyWindow, autonomyRange, autonomyHint;  // the window it falls in ("75%", "75–95") and its text
};
struct SumCard {
  int id = 0, boardId = 0, normSeconds = 0;
  bool hasClient = false, awaiting = false, closed = false;
  std::string status, row;
  std::string laneKey;  // upcoming / in_progress / review / done — what POST /collector/cards/:id/move takes
  std::wstring title, clientName, obligationCode, dueDate, lane, worker;
};
// A view of the unified board this person may open (the web's subview bar): ex-clients, mp-people, …
struct SubviewTab {
  std::string id, set;  // set: executor („Изп.") / partner („УС")
  std::wstring label;   // „Задачи по клиенти"
};
struct Summary {
  bool loaded = false;
  std::string subviewId, table;
  std::wstring subviewLabel, nameHead;
  std::vector<SumColumn> layout;
  std::vector<SumRow> rows;
  SumRow total;
  std::vector<SumCard> cards;
  std::vector<SubviewTab> subviews;  // only those with a table
};
Summary parseSummary(const rt::Json& j);

// What the server wants before it takes an entry on a card — 422 `time_detail_required`: a choice from the activity's
// option set and/or, on a project task, „Дейност“ + „Коментар“. Any other refusal: only `message`.
struct TimeOption {
  int id = 0;
  std::wstring name, hint;
};
struct AttachNeeds {
  std::string code;
  std::wstring message;
  bool needsDetail = false;  // проектна задача
  int maxChars = 0;          // „Дейност“ at most (0: no limit)
  std::wstring setName;
  bool multiSelect = false;
  std::vector<TimeOption> options;
  std::vector<int> chosen;          // the server's starting choice (its defaults); the form's choice when sent back
  std::wstring activity, comment;   // the same: what the server has / what the person typed
};
AttachNeeds parseAttachNeeds(const rt::Json& j, const std::wstring& fallback);

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
