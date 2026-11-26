// Where the Windows taskbar is, and the one setting of it we touch.
//
// Windows 11 has no supported way to put a program's own content on the taskbar (desk bands are gone),
// so the button is a small always-on-top window laid over the empty stretch left of the tray: the
// place Windows' weather/news button occupies when the taskbar is left-aligned. Everything here only
// reads window rectangles; nothing is injected into Explorer.
#pragma once
#include "common.h"

namespace kd {

struct TaskbarInfo {
  bool ok = false;
  HWND hwnd = nullptr;
  RECT bar{}, tray{}, tasks{};  // physical pixels; tasks = the strip of app buttons
  HMONITOR mon = nullptr;
  bool atBottom = true;
  bool slidAway = false;        // auto-hide taskbar that is currently hidden
  float scale = 1.f;
};
TaskbarInfo queryTaskbar();

bool taskbarIsLight();          // the taskbar follows the *system* theme, not the apps theme
bool fullscreenAppActive();     // a game, video or presentation owns the screen, or it is locked: stay out of the way

// Windows' Widgets button (weather / news) is the one slot Windows keeps free for it in the taskbar layout,
// which is why our button covers it instead of asking for it to be switched off. (A program cannot switch
// it off anyway: Windows 11's UCPD driver refuses every ordinary program the TaskbarDa value, and once it
// is off, the app buttons simply flow into the space.)
bool widgetsButtonShown();          // HKCU\...\Explorer\Advanced\TaskbarDa (missing = shown)
bool widgetsButtonRect(RECT& r);    // UI Automation, AutomationId "WidgetsButton"; false when it is not there
LONG appButtonsRight();             // right edge of the last app button (UI Automation); 0 if unknown

}  // namespace kd
