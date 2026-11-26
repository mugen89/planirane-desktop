// The two windows: the panel above the taskbar button, and sign-in.
#pragma once
#include "ui.h"

namespace kd {

class Panel : public rt::UiWindow {
 public:
  bool create(HINSTANCE inst, RECT at);
  void showAt(RECT at);

 protected:
  void paint() override;
  void onDeactivate() override;
  bool onClose() override;

 private:
  double armedCancel_ = 0;  // "Откажи" asks once more for three seconds
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
