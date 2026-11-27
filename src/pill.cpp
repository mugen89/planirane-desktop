#include "pill.h"

#include "taskbar.h"

#include "gfx.h"
#include "taskbar.h"

namespace kd {
using namespace rt;

namespace {
constexpr const wchar_t* kClass = L"KanbanDesktopPill";

TextStyle lineStyle(float size, int weight) {
  TextStyle ts;
  ts.family = L"Segoe UI Variable Text";
  ts.size = size;
  ts.weight = weight;
  ts.align = Align::Left;
  ts.valign = VAlign::Middle;
  ts.wrap = false;
  ts.ellipsis = true;
  return ts;
}

Color stateColor(PillIcon i, bool light) {
  switch (i) {
    case PillIcon::Running: return light ? Color::hex(0x15803D) : Color::hex(0x4ADE80);
    case PillIcon::Paused: return light ? Color::hex(0xB45309) : Color::hex(0xFBBF24);
    case PillIcon::Offline: return light ? Color::hex(0xB91C1C) : Color::hex(0xF87171);
    default: return light ? Color(0.1f, 0.1f, 0.1f, 0.78f) : Color(1, 1, 1, 0.82f);
  }
}

// The idle counts: the number in its badge, the column's name after it.
std::wstring badgeText(int n) { return n > 999 ? std::wstring(L"999+") : std::to_wstring(n); }
TextStyle badgeNumber(float s) {
  TextStyle ts = lineStyle(10.5f * s, 700);
  ts.align = Align::Center;
  ts.ellipsis = false;
  return ts;
}
TextStyle badgeLabel(float s) { return lineStyle(11.5f * s, 400); }
// Near-black or white on a badge, whichever stays readable: the server's `readableOn` (Rec. 601 luma > 0.6), so the
// numbers read as on the browser's band pills.
Color readableOn(Color c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b > 0.6f ? Color::hex(0x10151F) : Color(1, 1, 1); }
}  // namespace

bool Pill::create(HINSTANCE inst) {
  gfx().ensureFactories();
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"Контрол на срокове",
                          WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
  return hwnd_ != nullptr;
}

void Pill::destroy() {
  releaseSurface();
  rt_.Reset();
  if (hwnd_) DestroyWindow(hwnd_);
  hwnd_ = nullptr;
  shown_ = false;
}

void Pill::releaseSurface() {
  if (mem_) {
    if (oldBmp_) SelectObject(mem_, oldBmp_);
    DeleteDC(mem_);
  }
  if (dib_) DeleteObject(dib_);
  mem_ = nullptr;
  dib_ = nullptr;
  bits_ = nullptr;
  oldBmp_ = nullptr;
  dw_ = dh_ = 0;
}

bool Pill::ensureSurface(int w, int h) {
  if (mem_ && w == dw_ && h == dh_) return rt_ != nullptr;
  releaseSurface();
  mem_ = CreateCompatibleDC(nullptr);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  dib_ = CreateDIBSection(mem_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  bits_ = bits;
  if (!mem_ || !dib_) {
    releaseSurface();
    return false;
  }
  oldBmp_ = SelectObject(mem_, dib_);
  dw_ = w;
  dh_ = h;
  if (!rt_) {
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(gfx().d2d->CreateDCRenderTarget(&p, &rt_))) return false;
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // there is no opaque background for ClearType
  }
  return true;
}

void Pill::show(PillIcon icon, const std::wstring& line1, const std::wstring& line2, int seconds, const std::vector<PillBadge>& badges) {
  bool same = icon == icon_ && line1 == l1_ && line2 == l2_ && seconds == seconds_ && badges == badges_;
  bool widthMayChange = line1.size() != l1_.size() || line2 != l2_ || badges != badges_;
  icon_ = icon;
  l1_ = line1;
  l2_ = line2;
  badges_ = badges;
  seconds_ = seconds;
  if (same) return;
  dirty_ = true;  // drawn by sync() when shown again, or right here
  if (!shown_) return;
  // A different text can need a different width (re-dock); in the weather slot the place never moves,
  // so sync() must still repaint — dirty_ makes it (it did not, and a stopped stopwatch kept showing).
  if (widthMayChange) sync();
  else render();
}

void Pill::setEnabled(bool on) {
  enabled_ = on;
  sync();
}

int Pill::measureWidth(bool compact) {
  float s = scale_;
  float w1 = gfx().measure(l1_, lineStyle(13.f * s, 600)).x;
  float w2 = 0.f;
  if (!badges_.empty()) {  // the counts: with their names when they fit, the numbers alone otherwise
    float full = badgesWidth(true);
    w2 = !compact && full <= 170.f * s ? full : badgesWidth(false);
  } else if (!compact) {
    w2 = gfx().measure(l2_, lineStyle(11.5f * s, 400)).x;
  }
  float text = std::min(std::max(w1, w2), (compact ? 90.f : 170.f) * s);
  return (int)std::ceil(34.f * s + text + 12.f * s);
}

float Pill::badgesWidth(bool labels) {
  float s = scale_, w = 0.f;
  for (size_t i = 0; i < badges_.size(); i++) {
    w += std::max(14.f * s, gfx().measure(badgeText(badges_[i].count), badgeNumber(s)).x + 8.f * s);
    if (labels) w += 4.f * s + gfx().measure(badges_[i].label, badgeLabel(s)).x;
    if (i + 1 < badges_.size()) w += (labels ? 9.f : 5.f) * s;
  }
  return w;
}

void Pill::sync() {
  if (!hwnd_) return;
  TaskbarInfo t = queryTaskbar();
  bool full = fullscreenAppActive();
  bool want = enabled_ && t.ok && t.atBottom && !t.slidAway && !full;
  if (!want) {
    if (shown_ || !loggedHidden_)
      logf("pill: hidden (enabled %d, taskbar %d, bottom %d, slid away %d, fullscreen %d)", enabled_, t.ok, t.atBottom, t.slidAway, full);
    loggedHidden_ = true;
    if (shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    return;
  }
  loggedHidden_ = false;
  bool light = taskbarIsLight();
  bool themeChanged = light != light_ || t.scale != scale_;
  light_ = light;
  scale_ = t.scale;
  if (nowSec() - slotCheckedAt_ > 4 || !EqualRect(&t.bar, &bar_)) findSlot(t);
  bar_ = t.bar;
  RECT at;
  if (cover_) {
    // Exactly on Windows' weather/news button: the taskbar keeps this space free, whatever else is open.
    at = slot_;
    compact_ = slot_.right - slot_.left < (LONG)(118 * scale_);
  } else {
    int gap = (int)(4 * scale_);
    int right = t.tray.left - gap, leftLimit = std::max(t.tasks.right, appsRight_) + gap;
    int wide = measureWidth(false), small = measureWidth(true);
    if (right - leftLimit >= wide) {
      compact_ = false;
      at = RECT{right - wide, t.bar.top, right, t.bar.bottom};
    } else if (right - leftLimit >= small) {
      compact_ = true;
      at = RECT{right - small, t.bar.top, right, t.bar.bottom};
    } else {
      // No room between the app buttons and the tray: the tray icon still tells the time.
      if (shown_) logf("pill: no room (tray %ld, apps end %ld, needs %d)", t.tray.left, leftLimit, small);
      if (shown_) ShowWindow(hwnd_, SW_HIDE);
      shown_ = false;
      return;
    }
  }
  bool moved = !EqualRect(&at, &at_);
  at_ = at;
  if (moved || themeChanged || !shown_ || plateChanged_ || dirty_) {
    plateChanged_ = false;
    render();
  }
  if (!shown_) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    shown_ = true;
  }
  raise();
}

void Pill::findSlot(const TaskbarInfo& t) {
  slotCheckedAt_ = nowSec();
  RECT wr{};
  bool shownSetting = widgetsButtonShown(), found = shownSetting && widgetsButtonRect(wr);
  bool wasCover = cover_;
  RECT wasSlot = slot_;
  Color wasPlate = plate_;
  cover_ = found && wr.left >= t.bar.left && wr.right <= t.tray.left + 2 && wr.right - wr.left > (LONG)(40 * t.scale);
  if (cover_) {
    slot_ = RECT{wr.left, t.bar.top, wr.right, t.bar.bottom};
    // The taskbar's own colour, taken from the empty strip between the slot and the tray (outside our window).
    int sx = wr.right + std::max<LONG>(1, (t.tray.left - wr.right) / 2), sy = (t.bar.top + t.bar.bottom) / 2;
    if (t.tray.left - wr.right < 3) {
      sx = wr.left - 2;
      sy = t.bar.top + 2;
    }
    HDC sdc = GetDC(nullptr);
    COLORREF c = GetPixel(sdc, sx, sy);
    ReleaseDC(nullptr, sdc);
    plate_ = c != CLR_INVALID ? Color(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1.f)
                              : (taskbarIsLight() ? Color::hex(0xF3F3F3) : Color::hex(0x202020));
    appsRight_ = 0;
  } else {
    appsRight_ = appButtonsRight();
  }
  if (cover_ != wasCover || (cover_ && !EqualRect(&slot_, &wasSlot)) || !slotLogged_) {
    slotLogged_ = true;
    logf("pill: slot (widgets setting %d, found %d, rect %ld..%ld, tray %ld) -> %s", shownSetting, found, wr.left, wr.right, t.tray.left,
         cover_ ? "cover" : "gap");
  }
  if (cover_ && !(plate_ == wasPlate)) plateChanged_ = true;  // wallpaper or theme changed under the taskbar
}

void Pill::raise() {
  if (shown_) SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Pill::render() {
  dirty_ = false;
  int w = at_.right - at_.left, h = at_.bottom - at_.top;
  if (!paint(w, h)) return;
  POINT dst{at_.left, at_.top}, src{0, 0};
  SIZE sz{w, h};
  BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd_, nullptr, &dst, &sz, mem_, &src, 0, &bf, ULW_ALPHA);
}

bool Pill::paint(int w, int h) {
  if (w <= 0 || h <= 0 || !ensureSurface(w, h)) return false;
  RECT rc{0, 0, w, h};
  if (FAILED(rt_->BindDC(mem_, &rc))) return false;
  float s = scale_;
  rt_->BeginDraw();
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
  rt_->Clear(D2D1::ColorF(0, 0, 0, 0));
  ComPtr<ID2D1SolidColorBrush> br;
  rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1.f / 255.f), &br);
  auto fillRect = [&](D2D1_RECT_F r, Color c, float corner) {
    br->SetColor(c.f());
    if (corner > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, corner, corner), br.Get());
    else rt_->FillRectangle(r, br.Get());
  };
  // Covering the weather button: an opaque plate in the taskbar's colour hides it. Otherwise almost
  // transparent — but never fully, or the clicks would fall through the layered window.
  fillRect(D2D1::RectF(0, 0, (float)w, (float)h), cover_ ? plate_ : Color(0, 0, 0, 1.f / 255.f), 0);
  Color ink = light_ ? Color::hex(0x1A1A1A) : Color(1, 1, 1);
  // Running or on pause, the button is a solid plate of its colour with white on it (the user, 2026-11-26: "more accent
  // on the taskbar if the timer is on") — green while it runs, amber on pause: seen at a glance, from across the room.
  bool lit = icon_ == PillIcon::Running || icon_ == PillIcon::Paused;
  if (lit) {
    bool up = hover_ || down_;
    Color plate = icon_ == PillIcon::Running ? Color::hex(up ? 0x16A34A : 0x15803D) : Color::hex(up ? 0xD97706 : 0xB45309);
    fillRect(D2D1::RectF(2 * s, 4 * s, w - 2 * s, h - 4 * s), plate, 5 * s);
    ink = Color(1, 1, 1);
  } else if (hover_ || down_) {
    Color plate = light_ ? Color(0, 0, 0, down_ ? 0.09f : 0.055f) : Color(1, 1, 1, down_ ? 0.11f : 0.07f);
    fillRect(D2D1::RectF(2 * s, 4 * s, w - 2 * s, h - 4 * s), plate, 4 * s);
  }

  // The stopwatch: its hand shows the seconds while running.
  Color c = lit ? Color(1, 1, 1) : stateColor(icon_, light_);
  float cx = 18.f * s, cy = h * 0.5f + 1.f * s, r = 7.f * s, sw = 1.6f * s;
  br->SetColor(c.f());
  rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), br.Get(), sw);
  rt_->DrawLine(D2D1::Point2F(cx, cy - r - 0.6f * s), D2D1::Point2F(cx, cy - r - 2.8f * s), br.Get(), sw);
  rt_->DrawLine(D2D1::Point2F(cx - 2.2f * s, cy - r - 2.8f * s), D2D1::Point2F(cx + 2.2f * s, cy - r - 2.8f * s), br.Get(), sw);
  if (icon_ == PillIcon::Paused) {
    fillRect(D2D1::RectF(cx - 2.6f * s, cy - 3.f * s, cx - 0.9f * s, cy + 3.f * s), c, 0.5f * s);
    fillRect(D2D1::RectF(cx + 0.9f * s, cy - 3.f * s, cx + 2.6f * s, cy + 3.f * s), c, 0.5f * s);
  } else {
    float a = icon_ == PillIcon::Running ? (seconds_ % 60) * (kTau / 60.f) : 0.f;
    float len = r - 2.4f * s;
    rt_->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx + sinf(a) * len, cy - cosf(a) * len), br.Get(), sw);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 1.3f * s, 1.3f * s), br.Get());
  }

  float x = 34.f * s, right = w - 8.f * s;
  auto drawText = [&](const std::wstring& t, const TextStyle& ts, D2D1_RECT_F r, Color col) {
    auto f = gfx().format(ts);
    if (!f) return;
    br->SetColor(col.f());
    rt_->DrawText(t.c_str(), (UINT32)t.size(), f.Get(), r, br.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  };
  // The idle counts: a badge per group in its column's colour (the browser's band colours), the column's name after it
  // when all of them fit, the numbers alone otherwise. They keep the second line even on a narrow (compact) button.
  auto drawBadges = [&](float top, float bottom) {
    bool labels = badgesWidth(true) <= right - x;
    float bh = 14.f * s, y0 = (top + bottom - bh) * 0.5f, at = x;
    for (auto& b : badges_) {
      if (at >= right) break;
      std::wstring t = badgeText(b.count);
      float bw = std::max(bh, gfx().measure(t, badgeNumber(s)).x + 8.f * s);
      fillRect(D2D1::RectF(at, y0, at + bw, y0 + bh), b.color, bh * 0.5f);
      drawText(t, badgeNumber(s), D2D1::RectF(at, y0, at + bw, y0 + bh), readableOn(b.color));
      at += bw;
      if (labels) {
        float lw = gfx().measure(b.label, badgeLabel(s)).x;
        drawText(b.label, badgeLabel(s), D2D1::RectF(at + 4.f * s, top, std::min(right, at + 6.f * s + lw), bottom), ink.alpha(lit ? 0.9f : 0.72f));
        at += 4.f * s + lw;
      }
      at += (labels ? 9.f : 5.f) * s;
    }
  };
  if (compact_ && badges_.empty()) {
    drawText(l1_, lineStyle(13.f * s, 600), D2D1::RectF(x, 0, right, (float)h), ink);
  } else {
    drawText(l1_, lineStyle(13.f * s, 600), D2D1::RectF(x, h * 0.5f - 17.f * s, right, h * 0.5f + 1.f * s), ink);
    if (!badges_.empty()) drawBadges(h * 0.5f + 0.f * s, h * 0.5f + 16.f * s);
    else drawText(l2_, lineStyle(11.5f * s, 400), D2D1::RectF(x, h * 0.5f + 0.f * s, right, h * 0.5f + 16.f * s), ink.alpha(lit ? 0.9f : 0.72f));
  }
  HRESULT hr = rt_->EndDraw();
  if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
    rt_.Reset();
    releaseSurface();
    return false;
  }
  return true;
}

bool Pill::snapshot(const std::wstring& png, int width, bool light) {
  if (!gfx().ensureFactories() || width <= 0) return false;
  bool cover = cover_, compact = compact_, wasLight = light_, hover = hover_, down = down_;
  float scale = scale_;
  Color plate = plate_;
  cover_ = true;
  compact_ = width < 118;  // the weather slot's rule at 100 %
  light_ = light;
  hover_ = down_ = false;
  scale_ = 1.f;
  plate_ = light ? Color::hex(0xEEEEEE) : Color::hex(0x1C1C1C);
  bool ok = paint(width, 48);
  if (ok) {
    GdiFlush();
    ok = savePngPixels((const uint8_t*)bits_, width, 48, width * 4, png);
  }
  cover_ = cover;
  compact_ = compact;
  light_ = wasLight;
  hover_ = hover;
  down_ = down;
  scale_ = scale;
  plate_ = plate;
  dirty_ = true;  // the bitmap holds the snapshot now: the next sync paints the real button again
  return ok;
}

LRESULT CALLBACK Pill::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
  auto* self = (Pill*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (self && self->hwnd_ == h) return self->handle(m, w, l);
  if (self && !self->hwnd_) {
    self->hwnd_ = h;
    return self->handle(m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

LRESULT Pill::handle(UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
      if (!hover_) {
        hover_ = true;
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        render();
      }
      return 0;
    case WM_MOUSELEAVE:
      hover_ = false;
      down_ = false;
      render();
      return 0;
    case WM_LBUTTONDOWN:
      down_ = true;
      SetCapture(hwnd_);
      render();
      return 0;
    case WM_LBUTTONUP: {
      bool click = down_ && hover_;
      down_ = false;
      ReleaseCapture();
      render();
      if (click && onClick) onClick();
      return 0;
    }
    case WM_RBUTTONUP: {
      POINT pt;
      GetCursorPos(&pt);
      if (onMenu) onMenu(pt);
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, m, w, l);
}

}  // namespace kd
