// The resident process: one hidden window, the tray icon, the taskbar button, and the state they show.
// Windows (sign-in, panel) are created on demand and drawn by the shared immediate-mode UI.
#pragma once
#include "api.h"
#include "common.h"
#include "pill.h"
#include "store.h"

#include <map>
#include <set>

namespace kd {

class Panel;
class Rail;

// Why the server refused to move a card (POST /collector/cards/:id/move answered 4xx) — shown under the card.
struct CardProblem {
  std::string code;  // reference_required, approval_required, time_required, timer_running, …
  std::wstring message;
};
class LoginWindow;

enum : UINT {
  WM_APP_NET = WM_APP + 1,   // an HTTP answer is ready (net.cpp)
  WM_APP_TRAY,               // tray icon callback
  WM_APP_SHOWPANEL,          // a second copy was started: show ourselves
  WM_APP_COMMAND,            // run App::command once the loop is up
};

class App {
 public:
  App();
  ~App();
  int run(HINSTANCE inst);

  // ---- what the windows read
  Config cfg;
  Api api;
  User user;
  bool signedIn = false;
  bool haveState = false;
  TimeState st;
  bool offline = false;       // the last call did not reach the server
  bool busy = false;          // an action is on its way
  std::wstring error;         // last action's error, shown in the panel
  std::wstring loginError;
  bool loginBusy = false;
  Summary summary;             // the unified board's summary table (GET /desktop/summary)
  std::map<int, CardProblem> cardProblems;  // card id → the server's last refusal to move it
  std::set<int> movingCards;                // moves on their way
  std::wstring summaryError;
  bool summaryBusy = false;
  std::string updateVersion;   // a newer published version, when there is one
  std::wstring updateNote;     // what the update is doing, or why it did not
  bool updateBusy = false;
  HINSTANCE inst() const { return inst_; }

  int runningSeconds() const { return st.hasRunning ? elapsedSeconds(st.running, st.skew) : 0; }

  // ---- what the windows do
  void signIn(const std::wstring& server, const std::wstring& username, const std::wstring& password);
  void signOut();
  void refresh();
  void startTimerOnCard(int cardId);  // or, while one runs, attach it to that card
  void refreshSummary();
  // The web's own move (the collector's lanes), so every rule stays on the server: approval, „№ вх. док.“,
  // time required, a running stopwatch. reference = „№ вх. док.“ sent along when closing.
  void moveCard(int cardId, const std::string& lane, const std::wstring& reference = {});
  void dismissProblem(int cardId);
  // Another view of the table. Remembered on the server for the person, as the web does (PATCH /me/view
  // boardSubview), so the browser opens on it too; until that is stored it is asked for with ?subview=.
  void chooseSubview(const std::string& id);

  // The tabs on the rail left of the panel (the user's sketch, 2026-11-26).
  enum class Tab { Table, Calendar, Clients, AdminDay, Audit, Profile };
  Tab tab = Tab::Table;
  void setTab(Tab t);
  // Only the tabs the person may use — the same rules as the browser and the server: Админ. дейности needs the right
  // to track time (GET /time/day answers 403 without it).
  bool tabAllowed(Tab t) const;
  // „Административни дейности“: one day, as GET /time/day gives it; saved with POST /time/day (only the rows sent).
  struct DayRowIn {
    int activityId = 0;
    std::wstring input, note;
  };
  AdminDay day;
  int daySeq = 0;  // a new answer from the server: the form re-reads it
  bool dayBusy = false;
  std::wstring dayError, dayNote;
  double dayNoteUntil = 0;
  void loadDay(const std::string& date = {});  // today when empty
  void saveDay(const std::vector<DayRowIn>& rows);
  // Календар
  Calendar cal;
  bool calBusy = false;
  std::wstring calError;
  void loadCalendar(int year = 0, int month = 0);  // 0: the month on screen (or this one)
  double calAt_ = 0;
  // Одитен график
  AuditTimeline audit;
  bool auditBusy = false;
  std::wstring auditError;
  void loadAudit();
  // Клиенти
  std::vector<ClientRow> clients;
  bool clientsLoaded = false, clientsBusy = false;
  std::wstring clientsError;
  void loadClients();
  ClientDetail clientDetail;
  bool clientBusy = false;
  std::wstring clientError;
  void loadClient(int id);
  // Профил
  Me me;
  void loadMe();
  void setPillOn(bool on);
  // „За разпределяне“: an entry is picked (armed) under the stopwatch, then a card in the table takes it — „Сложи тук“.
  // The browser has no screen for this any more (its time pane went on 2026-09-08), so the desktop is where it is done.
  void armEntry(int entryId);  // 0 = none
  void attachEntry(int entryId, int cardId, const AttachNeeds* answer = nullptr);  // answer: the choice / Дейност / Коментар
  void dismissAttach(int cardId);
  int armedEntry = 0;
  std::map<int, AttachNeeds> attachProblems;  // card id → what the server wants first, or why it refused
  std::set<int> attachingCards;
  std::wstring attachNote;  // „0:45 → 5 ДДС“ for a few seconds after it worked
  double attachNoteUntil = 0;
  std::string subviewChoice;
  void pauseTimer();
  void resumeTimer();
  void stopTimer();
  void cancelTimer();
  // ✓ on stopped time. When the card needs a choice or Дейност + Коментар the server says what (422); the panel asks
  // for it under the stopwatch and sends it back here as `answer`.
  void confirmPending(int id, const AttachNeeds* answer = nullptr);
  void dismissPendingNeeds(int id);
  std::map<int, AttachNeeds> pendingNeeds;  // stopped entry id → what its ✓ still needs
  void discardPending(int id);
  void openBrowser();
  void togglePanel();
  void hidePanel();
  void showLogin(bool activate = true);
  void command(const std::wstring& cmd);  // panel, hide, quit, start, pause, resume, stop, cancel, refresh, signin, signout
  void quit();
  void checkForUpdate(bool manual);

 private:
  static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l);
  LRESULT handle(UINT m, WPARAM w, LPARAM l);
  bool makeWindow();
  void action(const wchar_t* method, const std::string& path, const rt::Json* body);
  void sessionLost();
  void updatePill();
  void updateTray();
  void trayMenu(POINT at);
  void notify(const std::wstring& title, const std::wstring& text);
  void updateToast();  // a manual check's outcome: the panel's footer shows it, a toast only when the panel is closed
  void tick();
  void startUpdate(const std::string& version, bool manual);
  static void CALLBACK onForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr;
  UINT taskbarCreated_ = 0;
  bool trayOn_ = false;
  std::wstring trayTip_;
  Pill pill_;
  std::unique_ptr<Panel> panel_;
  std::unique_ptr<LoginWindow> login_;
  HWINEVENTHOOK fgHook_ = nullptr;
  double lastPoll_ = 0;
  double nextUpdateCheck_ = 0;
  double summaryAt_ = 0;
  bool summaryAgain_ = false;  // asked for while one was on its way (e.g. right after a move): once more when it lands
  RECT panelRect(float& scale, RECT* rail = nullptr);  // above the taskbar button, almost as tall as the work area; the rail left of it
  std::unique_ptr<Rail> rail_;
  double panelHiddenAt_ = 0;  // a click on the tray icon first closes the flyout; it must not reopen it
  int stateSeq_ = 0;          // answers older than the newest request are ignored
};

App& app();

}  // namespace kd
