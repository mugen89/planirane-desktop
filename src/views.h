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

 protected:
  void paint() override;
  void onDeactivate() override;
  bool onClose() override;

 private:
  float paintTimer(rt::RectF r);         // returns where it ended
  void paintSummary(rt::RectF area);
  float paintViews(rt::RectF r);         // the views above the table; returns where it ended
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
  std::map<int, std::wstring> refInput_;  // „№ вх. док.“ typed under a card the server would not close without one
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
