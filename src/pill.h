// The button on the taskbar, in the spot Windows' weather/news button used: a stopwatch and two lines
// (the running time and what it is for; idle, today's time and the table's most urgent counts as coloured badges).
// Click: the panel. Right click: the menu.
//
// It is a layered window drawn with Direct2D's *software* renderer into a small bitmap, so it costs no
// GPU device and almost no memory while it sits there all day; it redraws once a second only while
// the stopwatch runs.
#pragma once
#include "common.h"
#include "taskbar.h"

namespace kd {

enum class PillIcon { Idle, Running, Paused, Offline, SignedOut };

// A count on the idle button (App::urgentBadges): the number in a badge of its column's colour, the column's name
// after it when the button is wide enough for all of them.
struct PillBadge {
  int count = 0;
  std::wstring label;
  rt::Color color;
  bool operator==(const PillBadge& o) const { return count == o.count && label == o.label && color == o.color; }
};

class Pill {
 public:
  std::function<void()> onClick;
  std::function<void(POINT)> onMenu;

  bool create(HINSTANCE inst);
  void destroy();
  // seconds turns the stopwatch hand
  // badges replace line2 (the idle counts)
  void show(PillIcon icon, const std::wstring& line1, const std::wstring& line2, int seconds, const std::vector<PillBadge>& badges = {});
  void setEnabled(bool on);
  void sync();   // dock to the taskbar, step aside for fullscreen apps; cheap enough for every second
  void raise();  // back above the taskbar after someone clicked it
  void relayout() { slotCheckedAt_ = -100; sync(); }  // the taskbar changed: look again now
  HWND hwnd() const { return hwnd_; }
  RECT rect() const { return at_; }
  RECT taskbarRect() const { return bar_; }
  bool shown() const { return shown_; }
  float scale() const { return scale_; }
  // Drawn off screen as it is now, at 100 % over a plate in the taskbar's usual colour, as when covering the weather
  // button (`--pillshot`, for checks): nothing appears on screen.
  bool snapshot(const std::wstring& png, int width, bool light);

 private:
  static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l);
  LRESULT handle(UINT m, WPARAM w, LPARAM l);
  void render();
  bool paint(int w, int h);  // into the bitmap; render() puts it on screen
  float badgesWidth(bool labels);
  bool ensureSurface(int w, int h);
  void releaseSurface();
  int measureWidth(bool compact);
  void findSlot(const TaskbarInfo& t);
  double slotCheckedAt_ = -100;
  bool cover_ = false;     // sitting exactly on Windows' weather/news button
  RECT slot_{};
  LONG appsRight_ = 0;     // where the app buttons end (fallback when there is no weather button)
  rt::Color plate_;        // the taskbar's colour beside the slot, painted under our content when covering

  HWND hwnd_ = nullptr;
  RECT at_{}, bar_{};
  float scale_ = 1.f;
  bool loggedHidden_ = false, slotLogged_ = false, plateChanged_ = false, dirty_ = true;
  bool enabled_ = true, shown_ = false, hover_ = false, down_ = false, light_ = false, compact_ = false;
  PillIcon icon_ = PillIcon::SignedOut;
  std::wstring l1_, l2_;
  std::vector<PillBadge> badges_;
  int seconds_ = 0;

  HDC mem_ = nullptr;
  HBITMAP dib_ = nullptr;
  void* bits_ = nullptr;  // the bitmap's pixels: premultiplied BGRA, top-down
  HGDIOBJ oldBmp_ = nullptr;
  int dw_ = 0, dh_ = 0;
  ComPtr<ID2D1DCRenderTarget> rt_;
};

}  // namespace kd
