// Small immediate-mode UI drawn with Direct2D: used by Settings and by the installer.
#pragma once
#include "common.h"
#include "gfx.h"

namespace rt {

struct UiPalette {
  bool dark = false;
  Color bg, panel, card, cardHover, stroke, strokeStrong, text, dim, faint, control, controlHover, controlDown, accent, accentHover, onAccent, danger, popup;
};
UiPalette makePalette(bool dark, Color accent);

struct UiKey {
  UINT vk;
  bool ctrl, shift;
};

enum class Btn { Standard, Accent, Subtle, Danger };

class Ui {
 public:
  UiPalette pal;
  Painter* p = nullptr;
  float dt = 0.016f;
  bool animating = false;
  RectF view;

  // input for this frame
  V2 mouse{-1000, -1000};
  bool down = false, pressed = false, released = false, dbl = false;
  bool rightPressed = false;
  float wheel = 0;
  std::vector<UiKey> keys;
  std::wstring chars;
  HWND hwnd = nullptr;
  HCURSOR cursor = nullptr;

  void begin(ID2D1DeviceContext* dc, RectF viewport, float dtSec);
  void end();
  void endInput();

  uint32_t id(std::string_view s, int salt = 0) const;
  float anim(uint32_t id, float target, float speed = 16.f);
  float& store(uint32_t id, float init = 0.f);

  bool hot(RectF r) const;                 // mouse over, honouring clips and popups
  bool press(uint32_t id, RectF r);        // becomes true on click
  bool held(uint32_t id) const { return active_ == id; }
  bool anyActive() const { return active_ != 0; }
  void capture(uint32_t id) { active_ = id; }
  void releaseCapture() { active_ = 0; }
  void clearFocus() { focus_ = 0; }
  bool focused(uint32_t id) const { return focus_ == id; }
  bool anyFocus() const { return focus_ != 0; }
  void focusOn(uint32_t id, bool selectAll = true);

  // text
  TextStyle font(float size, int weight = 400, Align a = Align::Left, VAlign v = VAlign::Middle) const;
  void text(RectF r, std::wstring_view s, float size, Color c, int weight = 400, Align a = Align::Left, VAlign v = VAlign::Middle, bool wrap = false);
  void glyph(RectF r, const wchar_t* g, float size, Color c);
  float textWidth(std::wstring_view s, float size, int weight = 400);

  // surfaces
  void card(RectF r, float corner = 8.f);
  void divider(RectF r);

  // controls
  bool button(uint32_t id, RectF r, std::wstring_view label, Btn kind = Btn::Standard, const wchar_t* icon = nullptr, bool enabled = true);
  bool iconButton(uint32_t id, RectF r, const wchar_t* icon, const wchar_t* tip = nullptr, bool enabled = true, float size = 14.f);
  bool toggle(uint32_t id, RectF r, bool& v, bool enabled = true);
  bool slider(uint32_t id, RectF r, float& v, float lo, float hi, float step, const wchar_t* fmt, float showScale = 1.f);
  bool segmented(uint32_t id, RectF r, int& idx, const std::vector<std::wstring>& opts);
  bool radio(uint32_t id, RectF r, std::wstring_view label, bool on, std::wstring_view desc = {});
  // A tick box with its label; flips `v` on a click and says so. `tip`: shown on hover.
  bool check(uint32_t id, RectF r, std::wstring_view label, bool& v, const wchar_t* tip = nullptr);
  bool combo(uint32_t id, RectF r, int& idx, const std::vector<std::wstring>& opts);
  bool textBox(uint32_t id, RectF r, std::wstring& s, std::wstring_view hint = {}, const wchar_t* icon = nullptr, bool* entered = nullptr);
  // Same editing as textBox, shown as dots and never copied to the clipboard.
  bool passwordBox(uint32_t id, RectF r, std::wstring& s, std::wstring_view hint = {}, const wchar_t* icon = nullptr, bool* entered = nullptr) {
    mask_ = true;
    bool c = textBox(id, r, s, hint, icon, entered);
    mask_ = false;
    return c;
  }
  bool colorBox(uint32_t id, RectF r, Color& c);
  bool link(uint32_t id, RectF r, std::wstring_view s, float size = 13.f);
  void tooltip(RectF r, std::wstring_view s);

  // scrolling region; returns the y offset to add to content
  float beginScroll(uint32_t id, RectF r, float contentH);
  void endScroll();
  void scrollTo(uint32_t id, float y) { store(id) = y; }

  void openPopup(uint32_t id, RectF r, std::function<void()> draw);
  bool popupOpen() const { return popupId_ != 0; }
  uint32_t popupOwner() const { return popupId_; }
  void closePopup() { popupId_ = 0; }
  RectF popupRect() const { return popup_; }

  void pushClip(RectF r);
  void popClip();

 private:
  std::unordered_map<uint32_t, float> anims_, vals_;
  uint32_t active_ = 0, focus_ = 0, popupId_ = 0;
  RectF popup_;
  std::function<void()> popupDraw_;
  bool inPopup_ = false, popupFresh_ = false;
  std::vector<RectF> clips_;

  struct Scroll { uint32_t id; RectF r; float contentH, off; };
  std::vector<Scroll> scrolls_;

  // text editing
  int caret_ = 0, anchor_ = 0;
  float editScroll_ = 0;
  double blink_ = 0;
  bool focusHit_ = false, selectAllOnFocus_ = false;
  bool mask_ = false;  // passwordBox in progress
  // colour popup
  Hsv hsv_;
  std::wstring hex_;
  // tooltip
  std::wstring tip_;
  RectF tipFor_;
  float tipT_ = 0;
  bool tipSeen_ = false;
};

// What was dropped: files, or a picture or link dragged from another program such as a browser.
struct DropData {
  std::vector<std::wstring> files;
  std::string picture;               // the bytes of a picture file
  std::wstring pictureName, url, title;
  bool empty() const { return files.empty() && picture.empty() && url.empty(); }
};

// A top-level window that hosts a Ui.
class UiWindow {
 public:
  virtual ~UiWindow();
  void dragTo(bool over, POINT screen);      // something is being dragged across the window
  void dropped(const DropData& d, POINT screen);
  bool createWindow(HINSTANCE inst, const wchar_t* cls, const wchar_t* title, int w, int h, int minW, int minH, bool resizable);
  // A borderless popup such as a flyout, at a rectangle in physical pixels. backdrop: 0 none, 2 Mica, 3 Acrylic.
  bool createPopup(HINSTANCE inst, const wchar_t* cls, RECT at, DWORD exStyle, int backdrop);
  float scale() const { return scale_; }
  HWND hwnd() const { return hwnd_; }
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
  bool wantsFrame() const { return visible() && !IsIconic(hwnd_) && (dirty_ || busy_); }
  HANDLE frameHandle() const { return wait_; }
  void frame();
  void invalidate() { dirty_ = true; }
  void dropDevice();
  void themeChanged();
  // Headless: draws one page into a bitmap.
  bool renderTo(ID2D1Bitmap1* target, float w, float h, int frames, bool dark);

 protected:
  virtual void paint() = 0;
  virtual bool onClose() { return true; }      // false keeps the window
  virtual void onDeactivate() {}              // the window lost activation (a click elsewhere)
  virtual void onDrop(const DropData&, V2) {}
  virtual LRESULT onMessage(UINT, WPARAM, LPARAM, bool& handled) { handled = false; return 0; }
  virtual Color backdropTint() const { return ui.pal.bg; }
  // Runs f once no frame is on the stack. Anything that shows a dialog, pumps messages or replaces data
  // the page code may be holding on to belongs here.
  void after(std::function<void()> f);
  Ui ui;
  HWND hwnd_ = nullptr;
  float scale_ = 1.f;
  bool mica_ = false;
  bool popup_ = false;
  int backdrop_ = 2;
  bool headless_ = false;
  bool dragOver_ = false;                    // a drag is above us, at dragAt_
  V2 dragAt_;
  ID2D1Bitmap1* backTarget() const { return headless_ ? headlessTarget_ : back_.Get(); }

 private:
  static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l);
  LRESULT handle(UINT m, WPARAM w, LPARAM l);
  bool ensureSurface();
  void applyTheme();
  std::vector<std::pair<DropData, V2>> drops_;
  int minW_ = 0, minH_ = 0;
  bool dirty_ = true, busy_ = false, painting_ = false;
  std::vector<std::function<void()>> later_;
  double last_ = 0;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual> visual_;
  ComPtr<IDXGISwapChain1> swap_;
  ComPtr<ID2D1Bitmap1> back_;
  ID2D1Bitmap1* headlessTarget_ = nullptr;
  HANDLE wait_ = nullptr;
  uint32_t gen_ = 0;
  int pw_ = 0, ph_ = 0;
};

}  // namespace rt
