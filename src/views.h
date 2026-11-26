// The two windows: the panel above the taskbar button, and sign-in.
#pragma once
#include "api.h"
#include "ui.h"

#include <map>
#include <set>

namespace kd {

class Panel : public rt::UiWindow {
 public:
  bool create(HINSTANCE inst, RECT at);
  void showAt(RECT at);
  // What a click on a row (column empty) or on a number does — also `--select row:column` from the command line.
  void select(const std::string& row, const std::string& col);
  void selectDay(const std::string& day) { calSel_ = day; }  // the calendar's day / bucket — also `--shot` for checks
  void selectClient(int id) { selClient_ = id; }  // --shot on the Клиенти tab
  void scrollDayTo(float y) { dayScrollWanted_ = y; }  // --shot on Админ. дейности: how far down the list
  void auditView(int depth, int card) {  // --shot on the Одитен график tab: how far opened, which card chosen
    auditDepthWanted_ = depth;
    auditCard_ = card;
  }

 protected:
  void paint() override;
  void onDeactivate() override;
  bool onClose() override;

 private:
  float paintTimer(rt::RectF r);         // returns where it ended
  void paintSummary(rt::RectF area);
  float paintViews(rt::RectF r);         // the views above the table; returns where it ended
  // A list of cards — the table's, a client's: lane menu, start / attach, period, the urgency bar, the forms under it.
  void paintCards(rt::RectF r, const std::vector<const SumCard*>& cards, uint32_t scrollId);
  rt::Color statusColorOf(const std::string& key);  // the urgency band's colour
  void paintCalendar(rt::RectF area);
  void paintAudit(rt::RectF area);
  void paintClients(rt::RectF area);
  void paintAdminDay(rt::RectF area);
  void paintProfile(rt::RectF area);
  float paintUnassigned(rt::RectF r);    // „за разпределяне“ under the stopwatch; returns where it ended
  // „What the server still wants“ (422 time_detail_required): its message, the option choice, Дейност + Коментар and
  // the button. Draws from r.t across r; returns the bottom. *send / *dismiss: the person pressed the button / ✕.
  float needsForm(rt::RectF r, int key, const AttachNeeds& need, AttachNeeds& form, const wchar_t* label, const wchar_t* icon,
                  bool enabled, bool* send, bool* dismiss);
  static float needsHeight(const AttachNeeds& need);
  static std::wstring toUpperBg(const std::wstring& s);
  double armedCancel_ = 0;  // "Откажи" asks once more for three seconds
  std::string selRow_, selCol_;  // the clicked row ("__total" = ОБЩО) and column ("" = the whole row)
  std::map<int, AttachNeeds> pendingForm_;  // stopped entry id → its ✓ form
  std::map<int, AttachNeeds> attachForm_;  // card id → the attach form (the choice, Дейност, Коментар), seeded from the server
  std::string calSel_;        // Календар: the chosen day (YYYY-MM-DD), or „outside“ / „undated“
  // Одитен график: the scale (0 own period, 1 year, 2 quarter, 3 month), its anchor month, the own period's months
  // back / ahead, the opened projects („p:<id>“) and tasks („t:<id>:<title>“), the chosen card (shown under the chart)
  int auditLevel_ = 1, auditY_ = 0, auditM_ = 0, auditBack_ = 1, auditAhead_ = 6, auditCard_ = 0;
  std::set<std::wstring> auditOpen_;
  int auditDepthWanted_ = -1;
  std::wstring clientQuery_;  // Клиенти: the search
  int selClient_ = 0;         // Клиенти: the chosen one (its nomenclature / cards under it)
  int clientPane_ = 0;        // 0 — „Номенклатура“, 1 — „Отворени карти“
  int clientAsked_ = 0;       // the client whose nomenclature was asked for (once per choice)
  void paintClientNomenclature(rt::RectF r);
  int dayFormSeq_ = -1;       // Админ. дейности: which answer of the server the form was filled from
  std::map<int, std::wstring> dayInput_, dayNoteIn_;  // activity id → the typed time / comment
  struct DayLineDraft {  // a line of an activity that asks for a colleague / a client
    int colleagueId = 0, clientId = 0;  // clientId -1: „неприложимо“
    std::wstring input, note;
  };
  std::map<int, std::vector<DayLineDraft>> dayLines_;  // line activity → its lines (the form)
  std::set<int> dayRanged_;                            // absences written over a period
  std::map<int, std::wstring> dayFrom_, dayTo_;        // that period, as typed (дд.мм.гггг)
  std::map<int, int> daySub_;                          // an absence's substitute (0: none)
  float dayScrollWanted_ = -1;
  std::map<int, std::wstring> refInput_;  // „№ вх. док.“ typed under a card the server would not close without one
};

// The tabs, a strip of its own left of the panel and a little apart from it (the user's sketch). It never takes the
// keyboard: the panel closes when it loses activation, and a click on a tab must not close it.
class Rail : public rt::UiWindow {
 public:
  static constexpr float kWidth = 96.f;
  bool create(HINSTANCE inst, RECT at);
  void showAt(RECT at);

 protected:
  void paint() override;
  bool onClose() override;
  LRESULT onMessage(UINT m, WPARAM w, LPARAM l, bool& handled) override;
};

class LoginWindow : public rt::UiWindow {
 public:
  bool create(HINSTANCE inst);
  // activate=false at start-up: shown, but the keyboard stays where the person is typing
  void open(const std::wstring& server, const std::wstring& username, bool activate);

 protected:
  void paint() override;

 private:
  void submit();
  std::wstring server_, user_, pass_;
  bool focusPending_ = true;
};

}  // namespace kd
