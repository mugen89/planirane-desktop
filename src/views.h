// The two windows: the panel above the taskbar button, and sign-in.
#pragma once
#include "api.h"
#include "ui.h"

#include <map>

namespace kd {

class Panel : public rt::UiWindow {
 public:
  bool create(HINSTANCE inst, RECT at);
  void showAt(RECT at);
  // What a click on a row (column empty) or on a number does — also `--select row:column` from the command line.
  void select(const std::string& row, const std::string& col);
  void selectDay(const std::string& day) { calSel_ = day; }  // the calendar's day / bucket — also `--shot` for checks

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
  std::wstring clientQuery_;  // Клиенти: the search
  int selClient_ = 0;         // Клиенти: the chosen one (its cards under it)
  int dayFormSeq_ = -1;       // Админ. дейности: which answer of the server the form was filled from
  std::map<int, std::wstring> dayInput_, dayNoteIn_;  // activity id → the typed time / comment
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
