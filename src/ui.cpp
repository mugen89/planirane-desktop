#include "ui.h"

namespace rt {

static const wchar_t* kTextFont = L"Segoe UI Variable Text";
static const wchar_t* kDisplayFont = L"Segoe UI Variable Display";
static const wchar_t* kIconFont = L"Segoe Fluent Icons";

UiPalette makePalette(bool dark, Color accent) {
  UiPalette q;
  q.dark = dark;
  Hsv h = toHsv(accent);
  if (dark) {
    q.bg = Color::hex(0x1B1C20);
    q.panel = Color(1, 1, 1, 0.035f);
    q.card = Color(1, 1, 1, 0.050f);
    q.cardHover = Color(1, 1, 1, 0.085f);
    q.stroke = Color(1, 1, 1, 0.075f);
    q.strokeStrong = Color(1, 1, 1, 0.16f);
    q.text = Color(1, 1, 1, 0.96f);
    q.dim = Color(1, 1, 1, 0.62f);
    q.faint = Color(1, 1, 1, 0.40f);
    q.control = Color(1, 1, 1, 0.065f);
    q.controlHover = Color(1, 1, 1, 0.10f);
    q.controlDown = Color(1, 1, 1, 0.04f);
    h.v = std::max(h.v, 0.92f);
    h.s = std::min(h.s, 0.72f);
    q.accent = fromHsv(h);
    q.accentHover = mix(q.accent, Color(1, 1, 1), 0.10f);
    q.onAccent = Color(0, 0, 0, 0.92f);
    q.danger = Color::hex(0xFF8F8F);
    q.popup = Color::hex(0x2A2B31);
  } else {
    q.bg = Color::hex(0xF4F5F8);
    q.panel = Color(1, 1, 1, 0.42f);
    q.card = Color(1, 1, 1, 0.74f);
    q.cardHover = Color(1, 1, 1, 0.98f);
    q.stroke = Color(0, 0, 0, 0.070f);
    q.strokeStrong = Color(0, 0, 0, 0.15f);
    q.text = Color(0.07f, 0.08f, 0.10f, 0.96f);
    q.dim = Color(0.07f, 0.08f, 0.10f, 0.64f);
    q.faint = Color(0.07f, 0.08f, 0.10f, 0.42f);
    q.control = Color(1, 1, 1, 0.80f);
    q.controlHover = Color(0.97f, 0.975f, 0.985f, 1.f);
    q.controlDown = Color(0.94f, 0.945f, 0.955f, 1.f);
    h.v = std::min(h.v, 0.80f);
    q.accent = fromHsv(h);
    q.accentHover = mix(q.accent, Color(1, 1, 1), 0.12f);
    q.onAccent = q.accent.luma() > 0.6f ? Color(0, 0, 0, 0.92f) : Color(1, 1, 1);
    q.danger = Color::hex(0xC42B1C);
    q.popup = Color::hex(0xFCFCFD);
  }
  return q;
}

// ---------------------------------------------------------------- frame
void Ui::begin(ID2D1DeviceContext* dc, RectF viewport, float dtSec) {
  p = &painter();
  p->attach(dc);
  view = viewport;
  dt = dtSec;
  animating = false;
  clips_.clear();
  scrolls_.clear();
  cursor = nullptr;
  tipSeen_ = false;
  popupFresh_ = false;
  if (popupId_ && pressed && !popup_.contains(mouse)) {  // click-away
    popupId_ = 0;
    pressed = false;
  }
  if (popupId_) {
    for (auto& k : keys)
      if (k.vk == VK_ESCAPE) popupId_ = 0;
  }
  if (pressed) {
    blink_ = nowSec();
  }
}

void Ui::end() {
  if (popupId_ && popupDraw_ && !popupFresh_) {
    inPopup_ = true;
    auto draw = popupDraw_;  // the callback may close or replace itself
    uint32_t pid = popupId_;
    float a = anim(pid ^ 0x50505050u, 1.f, 18.f);
    (void)a;
    RectF r = popup_;
    p->rrect(r.offset(0, 6).inset(-3), 12, Color(0, 0, 0, pal.dark ? 0.35f : 0.13f));
    p->rrect(r.offset(0, 2).inset(-1), 10, Color(0, 0, 0, pal.dark ? 0.25f : 0.08f));
    p->rrect(r, 9, pal.popup);
    p->rrectStroke(r.inset(0.5f), 9, pal.strokeStrong, 1);
    draw();
    inPopup_ = false;
  } else if (popupFresh_) {
    animating = true;
  }
  if (!popupId_) popupDraw_ = nullptr;

  if (tipSeen_ && !popupId_) {
    if (tipT_ < 0.5f) { tipT_ += dt; animating = true; }
    if (tipT_ >= 0.5f) {
      float w = std::min(320.f, textWidth(tip_, 12.5f) + 20.f);
      TextStyle ts = font(12.5f, 400, Align::Left, VAlign::Middle);
      ts.wrap = true;
      V2 m = gfx().measure(tip_, ts, w - 18.f);
      float h = m.y + 12.f;
      RectF r = RectF::xywh(tipFor_.cx() - w * 0.5f, tipFor_.b + 8.f, w, h);
      if (r.b > view.b - 4) r = RectF::xywh(r.l, tipFor_.t - h - 8.f, w, h);
      float dx = std::max(0.f, view.l + 6 - r.l) - std::max(0.f, r.r - (view.r - 6));
      r = r.offset(dx, 0);
      p->rrect(r.offset(0, 2).inset(-1), 7, Color(0, 0, 0, 0.14f));
      p->rrect(r, 6, pal.popup);
      p->rrectStroke(r.inset(0.5f), 6, pal.strokeStrong, 1);
      p->text(tip_, ts, r.inset(9, 2), pal.text);
    }
  } else {
    tipT_ = 0;
  }
  if (released) active_ = 0;
  if (pressed && !focusHit_) focus_ = 0;
}

void Ui::endInput() {
  pressed = released = dbl = rightPressed = false;
  wheel = 0;
  keys.clear();
  chars.clear();
  focusHit_ = false;
}

uint32_t Ui::id(std::string_view s, int salt) const {
  uint32_t h = 2166136261u;
  for (char c : s) h = (h ^ (uint8_t)c) * 16777619u;
  h = (h ^ (uint32_t)salt) * 16777619u;
  h ^= h >> 15;
  return h ? h : 1u;
}

float Ui::anim(uint32_t i, float target, float speed) {
  auto it = anims_.find(i);
  if (it == anims_.end()) {
    anims_[i] = target;
    return target;
  }
  float& v = it->second;
  float k = 1.f - expf(-speed * dt);
  v += (target - v) * k;
  if (fabsf(v - target) < 0.004f) v = target;
  else animating = true;
  return v;
}
float& Ui::store(uint32_t i, float init) {
  auto it = vals_.find(i);
  if (it == vals_.end()) it = vals_.emplace(i, init).first;
  return it->second;
}

bool Ui::hot(RectF r) const {
  if (popupId_ && !inPopup_) return false;
  if (!r.contains(mouse)) return false;
  if (!inPopup_ && !clips_.empty() && !clips_.back().contains(mouse)) return false;
  return true;
}

bool Ui::press(uint32_t i, RectF r) {
  bool h = hot(r);
  if (h && pressed && active_ == 0) active_ = i;
  return released && active_ == i && h;
}

void Ui::pushClip(RectF r) {
  if (!clips_.empty()) {
    RectF c = clips_.back();
    r = RectF(std::max(r.l, c.l), std::max(r.t, c.t), std::min(r.r, c.r), std::min(r.b, c.b));
  }
  clips_.push_back(r);
  p->dc->PushAxisAlignedClip(r.f(), D2D1_ANTIALIAS_MODE_ALIASED);
}
void Ui::popClip() {
  p->dc->PopAxisAlignedClip();
  if (!clips_.empty()) clips_.pop_back();
}

// ---------------------------------------------------------------- text
TextStyle Ui::font(float size, int weight, Align a, VAlign v) const {
  TextStyle t;
  t.family = size >= 20.f ? kDisplayFont : kTextFont;
  t.size = size;
  t.weight = weight;
  t.align = a;
  t.valign = v;
  return t;
}
void Ui::text(RectF r, std::wstring_view s, float size, Color c, int weight, Align a, VAlign v, bool wrap) {
  TextStyle t = font(size, weight, a, v);
  t.wrap = wrap;
  p->text(s, t, r, c);
}
void Ui::glyph(RectF r, const wchar_t* g, float size, Color c) {
  TextStyle t;
  t.family = kIconFont;
  t.size = size;
  t.align = Align::Center;
  t.ellipsis = false;
  p->text(g, t, r, c);
}
float Ui::textWidth(std::wstring_view s, float size, int weight) { return gfx().measure(s, font(size, weight)).x; }

// ---------------------------------------------------------------- surfaces
void Ui::card(RectF r, float corner) {
  p->rrect(r, corner, pal.card);
  p->rrectStroke(r.inset(0.5f), corner, pal.stroke, 1.f);
}
void Ui::divider(RectF r) { p->dc->FillRectangle(r.f(), p->solid(pal.stroke)); }

// ---------------------------------------------------------------- controls
bool Ui::button(uint32_t i, RectF r, std::wstring_view label, Btn kind, const wchar_t* icon, bool enabled) {
  bool clicked = enabled && press(i, r);
  bool h = enabled && hot(r), d = held(i) && h;
  float a = anim(i, h ? 1.f : 0.f);
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  float op = enabled ? 1.f : 0.42f;
  Color fg = pal.text;
  if (kind == Btn::Accent) {
    Color c = mix(pal.accent, pal.accentHover, a);
    if (d) c = mix(c, Color(0, 0, 0), 0.10f);
    p->rrect(r, 6, c.alpha(op));
    fg = pal.onAccent;
  } else if (kind == Btn::Subtle) {
    p->rrect(r, 6, pal.controlHover.alpha(a * (d ? 0.6f : 1.f) * op));
  } else {
    Color c = mix(pal.control, pal.controlHover, a);
    if (d) c = pal.controlDown;
    p->rrect(r, 6, c.alpha(op));
    p->rrectStroke(r.inset(0.5f), 6, pal.strokeStrong.alpha(0.75f * op), 1);
  }
  if (kind == Btn::Danger) fg = pal.danger;
  fg = fg.alpha(op * (d ? 0.8f : 1.f));
  if (icon) {
    float tw = label.empty() ? 0.f : textWidth(label, 13.5f, kind == Btn::Accent ? 600 : 400);
    float total = 18.f + (label.empty() ? 0.f : 7.f + tw);
    float x = r.cx() - total * 0.5f;
    glyph(RectF::xywh(x, r.t, 18, r.h()), icon, 14.f, fg);
    if (!label.empty()) text(RectF::xywh(x + 25, r.t - 0.5f, tw + 4, r.h()), label, 13.5f, fg, kind == Btn::Accent ? 600 : 400);
  } else {
    text(RectF(r.l + 8, r.t - 0.5f, r.r - 8, r.b - 0.5f), label, 13.5f, fg, kind == Btn::Accent ? 600 : 400, Align::Center);
  }
  return clicked;
}

bool Ui::iconButton(uint32_t i, RectF r, const wchar_t* icon, const wchar_t* tip, bool enabled, float size) {
  bool clicked = enabled && press(i, r);
  bool h = enabled && hot(r);
  float a = anim(i, h ? 1.f : 0.f);
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  p->rrect(r, 6, (pal.dark ? Color(1, 1, 1, 0.10f) : Color(0, 0, 0, 0.06f)).alpha(a));
  glyph(r, icon, size, pal.text.alpha(enabled ? lerp(0.72f, 1.f, a) : 0.3f));
  if (tip) tooltip(r, tip);
  return clicked;
}

bool Ui::toggle(uint32_t i, RectF r, bool& v, bool enabled) {
  RectF t = RectF::xywh(r.r - 42, r.cy() - 10.5f, 42, 21);
  bool changed = false;
  if (enabled && press(i, r)) {
    v = !v;
    changed = true;
  }
  bool h = enabled && hot(r);
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  float a = anim(i, v ? 1.f : 0.f, 20.f), q = anim(i ^ 0x77u, h ? 1.f : 0.f);
  float op = enabled ? 1.f : 0.4f;
  p->rrect(t, 10.5f, mix(pal.control, mix(pal.accent, pal.accentHover, q), a).alpha(op));
  if (a < 0.98f) p->rrectStroke(t.inset(0.5f), 10.f, pal.faint.alpha((1 - a) * op), 1.f);
  float kr = lerp(5.6f, 6.6f, std::max(a, q));
  p->circle({lerp(t.l + 10.5f, t.r - 10.5f, a), t.cy()}, kr, mix(pal.dim, pal.onAccent, a).alpha(op));
  return changed;
}

bool Ui::slider(uint32_t i, RectF r, float& v, float lo, float hi, float step, const wchar_t* fmt, float showScale) {
  float valW = 58.f;
  RectF tr(r.l + 8, r.cy() - 2, r.r - valW - 8, r.cy() + 2);
  RectF hit(r.l, r.t, r.r - valW, r.b);
  bool changed = false;
  if (hot(hit) && pressed && active_ == 0) active_ = i;
  if (active_ == i && (down || released)) {
    float t = saturate((mouse.x - tr.l) / std::max(1.f, tr.w()));
    float nv = lo + (hi - lo) * t;
    if (step > 0) nv = lo + roundf((nv - lo) / step) * step;
    nv = clampf(nv, lo, hi);
    if (fabsf(nv - v) > 1e-5f) { v = nv; changed = true; }
  }
  bool h = hot(hit) || active_ == i;
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  if (hot(r) && wheel != 0 && (GetKeyState(VK_CONTROL) & 0x8000)) {
    v = clampf(v + (wheel > 0 ? step : -step), lo, hi);
    wheel = 0;
    changed = true;
  }
  float t = saturate((v - lo) / (hi - lo));
  float ta = anim(i ^ 0x1234u, t, 30.f), ha = anim(i, h ? 1.f : 0.f);
  p->rrect(tr, 2, pal.dark ? Color(1, 1, 1, 0.18f) : Color(0, 0, 0, 0.14f));
  float x = lerp(tr.l, tr.r, ta);
  p->rrect(RectF(tr.l, tr.t, x, tr.b), 2, pal.accent);
  p->circle({x, r.cy()}, 9.5f, pal.dark ? Color::hex(0x45464E) : Color(1, 1, 1));
  p->circleStroke({x, r.cy()}, 9.5f, pal.strokeStrong, 1);
  p->circle({x, r.cy()}, lerp(5.f, held(i) ? 5.5f : 6.6f, ha), pal.accent);
  wchar_t b[48];
  swprintf(b, 48, fmt, (double)(v * showScale));
  text(RectF(r.r - valW, r.t, r.r, r.b), b, 13.f, pal.dim, 400, Align::Right);
  return changed;
}

bool Ui::segmented(uint32_t i, RectF r, int& idx, const std::vector<std::wstring>& opts) {
  bool changed = false;
  int n = (int)opts.size();
  if (!n) return false;
  p->rrect(r, 7, pal.dark ? Color(0, 0, 0, 0.22f) : Color(0, 0, 0, 0.045f));
  p->rrectStroke(r.inset(0.5f), 7, pal.stroke, 1);
  float w = (r.w() - 6.f) / n;
  float x = anim(i, (float)idx, 20.f);
  RectF sel = RectF::xywh(r.l + 3 + x * w, r.t + 3, w, r.h() - 6);
  p->rrect(sel.offset(0, 1), 5, Color(0, 0, 0, pal.dark ? 0.22f : 0.06f));
  p->rrect(sel, 5, pal.dark ? Color::hex(0x3B3D46) : Color(1, 1, 1));
  p->rrect(RectF::xywh(sel.cx() - 9, sel.b - 3, 18, 2.4f), 1.2f, pal.accent);
  for (int k = 0; k < n; k++) {
    RectF c = RectF::xywh(r.l + 3 + k * w, r.t + 3, w, r.h() - 6);
    uint32_t ci = i + 101u * (uint32_t)(k + 1);
    if (press(ci, c) && idx != k) { idx = k; changed = true; }
    bool h = hot(c);
    if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
    float a = anim(ci, k == idx ? 1.f : (h ? 0.6f : 0.f));
    text(RectF(c.l + 3, c.t - 1, c.r - 3, c.b - 1), opts[(size_t)k], 13.f, mix(pal.dim, pal.text, a), k == idx ? 600 : 400, Align::Center);
  }
  return changed;
}

bool Ui::radio(uint32_t i, RectF r, std::wstring_view lbl, bool on, std::wstring_view desc) {
  bool c = press(i, r);
  bool h = hot(r);
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  float a = anim(i, on ? 1.f : 0.f, 20.f), q = anim(i ^ 0x99u, h ? 1.f : 0.f);
  V2 o{r.l + 12, desc.empty() ? r.cy() : r.t + 15};
  p->rrect(r, 6, (pal.dark ? Color(1, 1, 1, 0.05f) : Color(0, 0, 0, 0.03f)).alpha(q));
  p->circle(o, 9, mix(pal.control, pal.accent, a));
  if (a < 0.98f) p->circleStroke(o, 8.5f, pal.faint.alpha(1 - a), 1.2f);
  p->circle(o, lerp(0.f, on && h ? 5.f : 4.2f, a), pal.onAccent.alpha(a));
  if (desc.empty()) {
    text(RectF(r.l + 30, r.t - 0.5f, r.r, r.b), lbl, 13.5f, pal.text, on ? 600 : 400);
  } else {
    text(RectF(r.l + 30, r.t + 5, r.r, r.t + 25), lbl, 13.5f, pal.text, on ? 600 : 400);
    text(RectF(r.l + 30, r.t + 25, r.r - 4, r.b - 2), desc, 12.f, pal.dim, 400, Align::Left, VAlign::Top, true);
  }
  return c;
}

bool Ui::combo(uint32_t i, RectF r, int& idx, const std::vector<std::wstring>& opts) {
  bool changed = false;
  uint32_t chosen = i ^ 0xC0B0u;
  float& pick = store(chosen, -1.f);
  if (pick >= 0) {
    if ((int)pick != idx) { idx = (int)pick; changed = true; }
    pick = -1;
  }
  bool h = hot(r);
  if (press(i, r)) {
    float ih = 32.f;
    float ph = std::min(340.f, opts.size() * ih + 10.f);
    RectF pr = RectF::xywh(r.l, r.b + 4, r.w(), ph);
    if (pr.b > view.b - 6) pr = RectF::xywh(r.l, std::max(view.t + 6, r.t - 4 - ph), r.w(), ph);
    store(i ^ 0x5C5Cu) = std::max(0.f, idx * ih - ph * 0.5f);
    openPopup(i, pr, [this, i, opts, pr, chosen, idx, ih] {
      float off = beginScroll(i ^ 0x5C5Cu, pr.inset(5), opts.size() * ih);
      for (size_t k = 0; k < opts.size(); k++) {
        RectF c = RectF::xywh(pr.l + 5, pr.t + 5 + k * ih + off, pr.w() - 10, ih - 2);
        uint32_t ci = i + 777u * (uint32_t)(k + 1);
        bool hv = hot(c);
        if (hv) cursor = LoadCursorW(nullptr, IDC_HAND);
        p->rrect(c, 5, (pal.dark ? Color(1, 1, 1, 0.09f) : Color(0, 0, 0, 0.05f)).alpha((int)k == idx ? 0.8f : anim(ci, hv ? 1.f : 0.f)));
        if ((int)k == idx) p->rrect(RectF::xywh(c.l + 1, c.cy() - 8, 3, 16), 1.5f, pal.accent);
        text(RectF(c.l + 12, c.t, c.r - 8, c.b), opts[k], 13.5f, pal.text);
        if (press(ci, c)) {
          store(chosen) = (float)k;
          closePopup();
          animating = true;
        }
      }
      endScroll();
    });
  }
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  float a = anim(i, h ? 1.f : 0.f);
  p->rrect(r, 6, mix(pal.control, pal.controlHover, a));
  p->rrectStroke(r.inset(0.5f), 6, pal.strokeStrong.alpha(0.75f), 1);
  if (idx >= 0 && idx < (int)opts.size()) text(RectF(r.l + 12, r.t - 0.5f, r.r - 30, r.b), opts[(size_t)idx], 13.5f, pal.text);
  glyph(RectF(r.r - 30, r.t, r.r - 4, r.b), L"\xE70D", 10.f, pal.dim);
  return changed;
}

bool Ui::link(uint32_t i, RectF r, std::wstring_view s, float size) {
  bool c = press(i, r);
  bool h = hot(r);
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  text(r, s, size, mix(pal.accent, pal.text, anim(i, h ? 0.35f : 0.f)), 600);
  return c;
}

void Ui::tooltip(RectF r, std::wstring_view s) {
  if (!hot(r) || down) return;
  tipSeen_ = true;
  if (tip_ != s || tipFor_.l != r.l || tipFor_.t != r.t) {
    tip_ = s;
    tipFor_ = r;
    tipT_ = 0;
  }
}

// ---------------------------------------------------------------- text box
void Ui::focusOn(uint32_t i, bool selectAll) {
  focus_ = i;
  focusHit_ = true;
  selectAllOnFocus_ = selectAll;
  blink_ = nowSec();
  editScroll_ = 0;
  caret_ = anchor_ = 0;
}

static std::wstring clipboardText(HWND h) {
  std::wstring s;
  if (!OpenClipboard(h)) return s;
  if (HANDLE d = GetClipboardData(CF_UNICODETEXT)) {
    if (auto* q = (const wchar_t*)GlobalLock(d)) {
      s = q;
      GlobalUnlock(d);
    }
  }
  CloseClipboard();
  for (auto& c : s)
    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
  return s;
}
static void setClipboard(HWND h, const std::wstring& s) {
  if (!OpenClipboard(h)) return;
  EmptyClipboard();
  size_t cb = (s.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, cb)) {
    if (void* q = GlobalLock(g)) {
      memcpy(q, s.c_str(), cb);
      GlobalUnlock(g);
      if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
  }
  CloseClipboard();
}

bool Ui::textBox(uint32_t i, RectF r, std::wstring& s, std::wstring_view hint, const wchar_t* icon, bool* entered) {
  bool changed = false;
  float left = r.l + (icon ? 34.f : 10.f);
  RectF tr(left, r.t, r.r - 8.f, r.b);
  TextStyle ts = font(13.5f);
  ts.ellipsis = false;
  auto lay = [&](const std::wstring& t) { return gfx().layout(mask_ ? std::wstring(t.size(), L'•') : t, ts, 100000.f, r.h()); };
  auto xOf = [&](IDWriteTextLayout* l, int pos) {
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS m{};
    if (l) l->HitTestTextPosition((UINT32)pos, FALSE, &x, &y, &m);
    return x;
  };
  auto posAt = [&](IDWriteTextLayout* l, float x) {
    BOOL trail = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    if (!l) return 0;
    l->HitTestPoint(x, r.h() * 0.5f, &trail, &inside, &m);
    return clampi((int)m.textPosition + (trail ? (int)m.length : 0), 0, (int)s.size());
  };
  bool h = hot(r);
  if (h) cursor = LoadCursorW(nullptr, IDC_IBEAM);
  if (h && pressed) {
    if (focus_ != i) {
      focus_ = i;
      editScroll_ = 0;
      selectAllOnFocus_ = false;
    }
    focusHit_ = true;
    active_ = i;
    auto l = lay(s);
    caret_ = anchor_ = posAt(l.Get(), mouse.x - tr.l + editScroll_);
    if (dbl) { anchor_ = 0; caret_ = (int)s.size(); }
    blink_ = nowSec();
  }
  bool f = focus_ == i;
  if (f) {
    if (selectAllOnFocus_) {
      anchor_ = 0;
      caret_ = (int)s.size();
      selectAllOnFocus_ = false;
    }
    caret_ = clampi(caret_, 0, (int)s.size());
    anchor_ = clampi(anchor_, 0, (int)s.size());
    if (active_ == i && down && !pressed) {
      auto l = lay(s);
      caret_ = posAt(l.Get(), mouse.x - tr.l + editScroll_);
    }
    auto sel = [&] { return std::pair<int, int>(std::min(caret_, anchor_), std::max(caret_, anchor_)); };
    auto erase = [&] {
      auto q = sel();
      if (q.first == q.second) return false;
      s.erase((size_t)q.first, (size_t)(q.second - q.first));
      caret_ = anchor_ = q.first;
      changed = true;
      return true;
    };
    auto insert = [&](const std::wstring& t) {
      erase();
      if (s.size() + t.size() > 1800) return;
      s.insert((size_t)caret_, t);
      caret_ = anchor_ = caret_ + (int)t.size();
      changed = true;
    };
    auto wordLeft = [&](int c) {
      while (c > 0 && s[(size_t)c - 1] == L' ') c--;
      while (c > 0 && s[(size_t)c - 1] != L' ') c--;
      return c;
    };
    auto wordRight = [&](int c) {
      int n = (int)s.size();
      while (c < n && s[(size_t)c] != L' ') c++;
      while (c < n && s[(size_t)c] == L' ') c++;
      return c;
    };
    for (auto& k : keys) {
      blink_ = nowSec();
      auto move = [&](int to) {
        caret_ = clampi(to, 0, (int)s.size());
        if (!k.shift) anchor_ = caret_;
      };
      switch (k.vk) {
        case VK_LEFT:
          if (!k.shift && caret_ != anchor_ && !k.ctrl) move(sel().first);
          else move(k.ctrl ? wordLeft(caret_) : caret_ - 1);
          break;
        case VK_RIGHT:
          if (!k.shift && caret_ != anchor_ && !k.ctrl) move(sel().second);
          else move(k.ctrl ? wordRight(caret_) : caret_ + 1);
          break;
        case VK_HOME: move(0); break;
        case VK_END: move((int)s.size()); break;
        case VK_BACK:
          if (!erase() && caret_ > 0) {
            int to = k.ctrl ? wordLeft(caret_) : caret_ - 1;
            // keep surrogate pairs together
            if (!k.ctrl && to > 0 && IS_LOW_SURROGATE(s[(size_t)to])) to--;
            s.erase((size_t)to, (size_t)(caret_ - to));
            caret_ = anchor_ = to;
            changed = true;
          }
          break;
        case VK_DELETE:
          if (!erase() && caret_ < (int)s.size()) {
            int to = k.ctrl ? wordRight(caret_) : caret_ + 1;
            if (!k.ctrl && to < (int)s.size() && IS_LOW_SURROGATE(s[(size_t)to])) to++;
            s.erase((size_t)caret_, (size_t)(to - caret_));
            changed = true;
          }
          break;
        case 'A':
          if (k.ctrl) { anchor_ = 0; caret_ = (int)s.size(); }
          break;
        case 'C':
        case 'X':
          if (k.ctrl && !mask_) {
            auto q = sel();
            if (q.first != q.second) {
              setClipboard(hwnd, s.substr((size_t)q.first, (size_t)(q.second - q.first)));
              if (k.vk == 'X') erase();
            }
          }
          break;
        case 'V':
          if (k.ctrl) insert(clipboardText(hwnd));
          break;
        case VK_RETURN:
          if (entered) *entered = true;
          break;
        case VK_ESCAPE: focus_ = 0; break;
      }
    }
    for (wchar_t c : chars)
      if (c >= 32 && c != 127) insert(std::wstring(1, c));
  }
  float a = anim(i, h ? 1.f : 0.f), fa = anim(i ^ 0xF0F0u, f ? 1.f : 0.f, 22.f);
  p->rrect(r, 6, mix(mix(pal.control, pal.controlHover, a), pal.dark ? Color::hex(0x1E1F24) : Color(1, 1, 1), fa));
  p->rrectStroke(r.inset(0.5f), 6, pal.strokeStrong.alpha(0.75f), 1);
  if (fa > 0.01f) p->rrectStroke(r.inset(0.75f), 5.5f, pal.accent.alpha(fa), 1.6f);
  if (icon) glyph(RectF(r.l + 8, r.t, r.l + 30, r.b), icon, 13.f, pal.dim);

  auto l = lay(s);
  float cx = xOf(l.Get(), clampi(caret_, 0, (int)s.size()));
  if (f) {
    if (cx - editScroll_ > tr.w() - 2) editScroll_ = cx - tr.w() + 2;
    if (cx - editScroll_ < 0) editScroll_ = cx;
    if (editScroll_ < 0) editScroll_ = 0;
  }
  float sc = f ? editScroll_ : 0.f;
  pushClip(RectF(tr.l - 1, tr.t, tr.r + 1, tr.b));
  if (f && caret_ != anchor_) {
    float x0 = xOf(l.Get(), std::min(caret_, anchor_)), x1 = xOf(l.Get(), std::max(caret_, anchor_));
    p->rrect(RectF(tr.l + x0 - sc, r.cy() - 9, tr.l + x1 - sc, r.cy() + 9), 2, pal.accent.withA(0.35f));
  }
  if (s.empty() && !hint.empty()) text(RectF(tr.l, tr.t - 0.5f, tr.r, tr.b), hint, 13.5f, pal.faint);
  else if (l) p->dc->DrawTextLayout(D2D1::Point2F(tr.l - sc, tr.t - 0.5f), l.Get(), p->solid(pal.text), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
  if (f) {
    animating = true;
    if (fmod(nowSec() - blink_, 1.06) < 0.55) p->line({tr.l + cx - sc + 0.5f, r.cy() - 8.5f}, {tr.l + cx - sc + 0.5f, r.cy() + 8.5f}, pal.text, 1.2f, false);
  }
  popClip();
  return changed;
}

// ---------------------------------------------------------------- colour
bool Ui::colorBox(uint32_t i, RectF r, Color& c) {
  bool changed = false;
  // The popup edits through stored values; pick up what it wrote since last frame.
  float &pr = store(i + 1), &pg = store(i + 2), &pb = store(i + 3), &has = store(i + 4);
  if (has > 0.5f) {
    Color n(pr, pg, pb, c.a);
    if (!(n == c)) { c = n; changed = true; }
    has = 0;
  }
  bool h = hot(r);
  if (h) cursor = LoadCursorW(nullptr, IDC_HAND);
  if (press(i, r)) {
    hsv_ = toHsv(c);
    hex_ = widen(c.withA(1).str());
    RectF pop = RectF::xywh(r.cx() - 124, r.b + 6, 248, 262);
    if (pop.b > view.b - 8) pop = pop.offset(0, r.t - 6 - pop.b);
    if (pop.r > view.r - 8) pop = pop.offset(view.r - 8 - pop.r, 0);
    if (pop.l < view.l + 8) pop = pop.offset(view.l + 8 - pop.l, 0);
    if (pop.t < view.t + 8) pop = pop.offset(0, view.t + 8 - pop.t);
    openPopup(i, pop, [this, i, pop] {
      RectF sv = RectF::xywh(pop.l + 12, pop.t + 12, 224, 150), hu = RectF::xywh(pop.l + 12, sv.b + 12, 224, 16);
      uint32_t isv = i ^ 0xAAAAu, ihu = i ^ 0xBBBBu, itx = i ^ 0xCCCCu;
      if (hot(sv) && pressed && active_ == 0) active_ = isv;
      if (hot(hu.inset(-4)) && pressed && active_ == 0) active_ = ihu;
      bool ch = false;
      if (active_ == isv && down) {
        hsv_.s = saturate((mouse.x - sv.l) / sv.w());
        hsv_.v = 1.f - saturate((mouse.y - sv.t) / sv.h());
        ch = true;
      }
      if (active_ == ihu && down) {
        hsv_.h = clampf((mouse.x - hu.l) / hu.w(), 0.f, 0.9999f);
        ch = true;
      }
      Color full = fromHsv({hsv_.h, 1, 1});
      auto b1 = p->linear({sv.l, 0}, {sv.r, 0}, {{0, Color(1, 1, 1)}, {1, full}});
      p->rrect(sv, 6, b1.Get());
      auto b2 = p->linear({0, sv.t}, {0, sv.b}, {{0, Color(0, 0, 0, 0)}, {1, Color(0, 0, 0, 1)}});
      p->rrect(sv, 6, b2.Get());
      V2 k{lerp(sv.l, sv.r, hsv_.s), lerp(sv.b, sv.t, hsv_.v)};
      p->circleStroke(k, 7, Color(0, 0, 0, 0.5f), 3.5f);
      p->circleStroke(k, 7, Color(1, 1, 1), 2.f);
      std::vector<Stop> st;
      for (int q = 0; q <= 6; q++) st.push_back({q / 6.f, fromHsv({q / 6.f * 0.9999f, 1, 1})});
      auto b3 = p->linear({hu.l, 0}, {hu.r, 0}, st);
      p->rrect(hu, 8, b3.Get());
      V2 hk{lerp(hu.l + 4, hu.r - 4, hsv_.h), hu.cy()};
      p->circle(hk, 9, Color(1, 1, 1));
      p->circleStroke(hk, 9, Color(0, 0, 0, 0.25f), 1);
      p->circle(hk, 6, full);
      if (ch) hex_ = widen(fromHsv(hsv_).str());
      RectF tx = RectF::xywh(pop.l + 12, hu.b + 14, 108, 32);
      if (textBox(itx, tx, hex_, L"#RRGGBB")) {
        Color n;
        if (Color::parse(narrow(trim(hex_)), n)) {
          hsv_ = toHsv(n);
          ch = true;
        }
      }
      p->rrect(RectF::xywh(tx.r + 10, tx.t, 106, 32), 6, fromHsv(hsv_));
      p->rrectStroke(RectF::xywh(tx.r + 10, tx.t, 106, 32).inset(0.5f), 6, pal.strokeStrong, 1);
      if (ch) {
        Color n = fromHsv(hsv_);
        store(i + 1) = n.r;
        store(i + 2) = n.g;
        store(i + 3) = n.b;
        store(i + 4) = 1;
        animating = true;
      }
    });
  }
  float a = anim(i, h || popupId_ == i ? 1.f : 0.f);
  p->rrect(r, 7, c.withA(1));
  p->rrectStroke(r.inset(0.5f), 7, pal.strokeStrong, 1);
  if (a > 0.01f) p->rrectStroke(r.inset(-2.5f), 9.5f, pal.accent.alpha(a), 2.f);
  return changed;
}

// ---------------------------------------------------------------- popup and scroll
void Ui::openPopup(uint32_t i, RectF r, std::function<void()> draw) {
  popupId_ = i;
  popup_ = r;
  popupDraw_ = std::move(draw);
  popupFresh_ = true;  // first drawn next frame, so the opening click cannot land inside it
  animating = true;
}

float Ui::beginScroll(uint32_t i, RectF r, float contentH) {
  float& tgt = store(i);
  float maxOff = std::max(0.f, contentH - r.h());
  if (hot(r) && wheel != 0) {
    tgt -= wheel * 96.f;
    wheel = 0;
  }
  // scrollbar drag
  uint32_t bar = i ^ 0xBA5u;
  if (maxOff > 0) {
    RectF track(r.r - 12, r.t, r.r, r.b);
    float th = std::max(34.f, r.h() * r.h() / contentH);
    if (hot(track) && pressed && active_ == 0) {
      active_ = bar;
      float ty = r.t + (r.h() - th) * (tgt / maxOff);
      store(bar) = (mouse.y >= ty && mouse.y <= ty + th) ? mouse.y - ty : th * 0.5f;
    }
    if (active_ == bar && down) tgt = (mouse.y - store(bar) - r.t) / std::max(1.f, r.h() - th) * maxOff;
  }
  tgt = clampf(tgt, 0.f, maxOff);
  float off = active_ == bar ? (anims_[i ^ 0x5C4011u] = tgt) : anim(i ^ 0x5C4011u, tgt, 18.f);
  scrolls_.push_back({i, r, contentH, off});
  pushClip(r);
  return -roundf(off);
}

void Ui::endScroll() {
  popClip();
  if (scrolls_.empty()) return;
  Scroll s = scrolls_.back();
  scrolls_.pop_back();
  float maxOff = std::max(0.f, s.contentH - s.r.h());
  if (maxOff <= 0.5f) return;
  RectF track(s.r.r - 12, s.r.t, s.r.r, s.r.b);
  bool h = hot(track) || active_ == (s.id ^ 0xBA5u);
  float a = anim(s.id ^ 0x8A8u, h ? 1.f : 0.f);
  float th = std::max(34.f, s.r.h() * s.r.h() / s.contentH), ty = s.r.t + (s.r.h() - th) * saturate(s.off / maxOff);
  float w = lerp(2.5f, 6.f, a);
  p->rrect(RectF::xywh(s.r.r - 3.f - w, ty + 2, w, th - 4), w * 0.5f, pal.text.withA(lerp(0.28f, 0.45f, a)));
}

// ================================================================ dropping
namespace {
constexpr UINT kDropMsg = WM_APP + 71;

std::string readMedium(STGMEDIUM& m, size_t limit) {
  std::string s;
  if (m.tymed == TYMED_HGLOBAL && m.hGlobal) {
    size_t n = GlobalSize(m.hGlobal);
    if (n <= limit)
      if (auto* p = (const char*)GlobalLock(m.hGlobal)) {
        s.assign(p, n);
        GlobalUnlock(m.hGlobal);
      }
  } else if (m.tymed == TYMED_ISTREAM && m.pstm) {
    char buf[16384];
    ULONG got = 0;
    LARGE_INTEGER zero{};
    m.pstm->Seek(zero, STREAM_SEEK_SET, nullptr);
    while (s.size() < limit && SUCCEEDED(m.pstm->Read(buf, sizeof buf, &got)) && got) s.append(buf, got);
    if (s.size() >= limit) s.clear();
  }
  return s;
}

std::string readFormat(IDataObject* o, UINT format, LONG index = -1, size_t limit = 24 * 1024 * 1024) {
  FORMATETC f{(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, index, TYMED_HGLOBAL | TYMED_ISTREAM};
  STGMEDIUM m{};
  if (FAILED(o->GetData(&f, &m))) return {};
  std::string s = readMedium(m, limit);
  ReleaseStgMedium(&m);
  return s;
}

std::wstring wideText(const std::string& raw) {
  std::wstring t((const wchar_t*)raw.data(), raw.size() / sizeof(wchar_t));
  size_t z = t.find(L'\0');
  if (z != std::wstring::npos) t.resize(z);
  return trim(t);
}

bool webLink(const std::wstring& t) {
  std::wstring low = toLower(t.substr(0, 8));
  return t.size() < 2000 && (low.rfind(L"http://", 0) == 0 || low.rfind(L"https://", 0) == 0) && t.find_first_of(L" \t\r\n\"<>") == std::wstring::npos;
}

DropData readDrop(IDataObject* o) {
  DropData d;
  FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM m{};
  if (SUCCEEDED(o->GetData(&f, &m))) {
    auto h = (HDROP)m.hGlobal;
    UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; i++) {
      UINT len = DragQueryFileW(h, i, nullptr, 0);
      std::wstring s(len + 1, 0);
      DragQueryFileW(h, i, s.data(), len + 1);
      s.resize(len);
      d.files.push_back(s);
    }
    ReleaseStgMedium(&m);
    if (!d.files.empty()) return d;
  }
  // A browser hands over a picture as a file that does not exist on disk, and a link as a little .url file.
  std::string desc = readFormat(o, RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW), -1, 1024 * 1024);
  if (desc.size() >= sizeof(FILEGROUPDESCRIPTORW)) {
    FILEGROUPDESCRIPTORW g;
    memcpy(&g, desc.data(), sizeof g);
    g.fgd[0].cFileName[MAX_PATH - 1] = 0;
    std::wstring name = g.cItems ? fileName(g.fgd[0].cFileName) : L"", ext = fileExt(name);
    if (ext == L".url") {
      d.title = fileStem(name);
    } else if (g.cItems) {
      d.picture = readFormat(o, RegisterClipboardFormatW(CFSTR_FILECONTENTS), 0);
      d.pictureName = fileStem(name);
    }
  }
  std::wstring u = wideText(readFormat(o, RegisterClipboardFormatW(CFSTR_INETURLW), -1, 65536));
  if (!webLink(u)) u = wideText(readFormat(o, CF_UNICODETEXT, -1, 65536));
  if (webLink(u)) d.url = u;
  return d;
}

class DropTarget final : public IDropTarget {
  LONG ref_ = 1;
  UiWindow& win_;
  bool ok_ = false;

 public:
  explicit DropTarget(UiWindow& w) : win_(w) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (id == __uuidof(IUnknown) || id == __uuidof(IDropTarget)) {
      *out = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG n = InterlockedDecrement(&ref_);
    if (!n) delete this;
    return (ULONG)n;
  }
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* o, DWORD, POINTL p, DWORD* effect) override {
    ok_ = false;
    const UINT fs[] = {CF_HDROP, RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW), RegisterClipboardFormatW(CFSTR_INETURLW), CF_UNICODETEXT};
    for (UINT f : fs) {
      FORMATETC e{(CLIPFORMAT)f, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL | TYMED_ISTREAM};
      if (o && o->QueryGetData(&e) == S_OK) ok_ = true;
    }
    return DragOver(0, p, effect);
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL p, DWORD* effect) override {
    *effect = !ok_ ? DROPEFFECT_NONE : (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : (*effect & DROPEFFECT_LINK) ? DROPEFFECT_LINK : DROPEFFECT_NONE;
    win_.dragTo(ok_, POINT{p.x, p.y});
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override {
    win_.dragTo(false, POINT{});
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* o, DWORD, POINTL p, DWORD* effect) override {
    DWORD can = *effect;
    *effect = DROPEFFECT_NONE;
    win_.dragTo(false, POINT{});
    DropData d;
    if (o) d = readDrop(o);
    if (!d.empty()) {
      *effect = (can & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_LINK;
      win_.dropped(d, POINT{p.x, p.y});
    }
    return S_OK;
  }
};
}  // namespace

void UiWindow::dragTo(bool over, POINT screen) {
  if (over) {
    ScreenToClient(hwnd_, &screen);
    dragAt_ = {screen.x / scale_, screen.y / scale_};
  }
  dragOver_ = over;
  dirty_ = true;
}

void UiWindow::dropped(const DropData& d, POINT screen) {
  // The program the drag came from waits until this returns, so the work happens afterwards.
  ScreenToClient(hwnd_, &screen);
  drops_.emplace_back(d, V2{screen.x / scale_, screen.y / scale_});
  PostMessageW(hwnd_, kDropMsg, 0, 0);
}

// ================================================================ window
UiWindow::~UiWindow() {
  dropDevice();
  if (hwnd_) {
    RevokeDragDrop(hwnd_);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
  }
}

bool UiWindow::createWindow(HINSTANCE inst, const wchar_t* cls, const wchar_t* title, int w, int h, int minW, int minH, bool resizable) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  if (!GetClassInfoExW(inst, cls, &wc)) {
    wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.lpszClassName = cls;
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
  }
  minW_ = minW;
  minH_ = minH;
  POINT cur;
  GetCursorPos(&cur);
  HMONITOR hm = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(hm, &mi);
  scale_ = dpiScaleFor(hm);
  DWORD style = resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
  RECT r{0, 0, (LONG)(w * scale_), (LONG)(h * scale_)};
  AdjustWindowRectExForDpi(&r, style, FALSE, 0, (UINT)(96 * scale_));
  int ww = std::min<int>(r.right - r.left, mi.rcWork.right - mi.rcWork.left), hh = std::min<int>(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
  int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2, y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - hh) / 2;
  hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, cls, title, style, x, y, ww, hh, nullptr, nullptr, inst, this);
  if (!hwnd_) return false;
  ui.hwnd = hwnd_;
  scale_ = dpiScaleFor(hwnd_);
  applyTheme();
  auto* drop = new DropTarget(*this);
  RegisterDragDrop(hwnd_, drop);
  drop->Release();
  return true;
}

bool UiWindow::createPopup(HINSTANCE inst, const wchar_t* cls, RECT at, DWORD exStyle, int backdrop) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  if (!GetClassInfoExW(inst, cls, &wc)) {
    wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
  }
  popup_ = true;
  backdrop_ = backdrop;
  hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | exStyle, cls, L"", WS_POPUP, at.left, at.top, at.right - at.left,
                          at.bottom - at.top, nullptr, nullptr, inst, this);
  if (!hwnd_) return false;
  ui.hwnd = hwnd_;
  scale_ = dpiScaleFor(hwnd_);
  applyTheme();
  return true;
}

void UiWindow::applyTheme() {
  bool dark = !systemUsesLightTheme();
  ui.pal = makePalette(dark, systemAccent());
  if (!hwnd_) return;
  BOOL d = dark;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof d);
  int backdrop = popup_ ? backdrop_ : 2;  // Mica for windows; flyouts choose (3 = Acrylic)
  mica_ = backdrop != 0 && SUCCEEDED(DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof backdrop));
  if (popup_) {
    int round = 2;  // DWMWA_WINDOW_CORNER_PREFERENCE (33) = DWMWCP_ROUND; not in every MinGW header
    DwmSetWindowAttribute(hwnd_, 33, &round, sizeof round);
  }
  MARGINS m{-1, -1, -1, -1};
  DwmExtendFrameIntoClientArea(hwnd_, &m);
}

void UiWindow::themeChanged() {
  applyTheme();
  dirty_ = true;
}

void UiWindow::dropDevice() {
  back_.Reset();
  swap_.Reset();
  visual_.Reset();
  target_.Reset();
  if (wait_) CloseHandle(wait_);
  wait_ = nullptr;
  gen_ = 0;
  dirty_ = true;
}

bool UiWindow::ensureSurface() {
  auto& g = gfx();
  if (!g.ensure()) return false;
  if (gen_ != g.generation) {
    dropDevice();
    gen_ = g.generation;
  }
  RECT r;
  GetClientRect(hwnd_, &r);
  int w = std::max<int>(1, r.right), h = std::max<int>(1, r.bottom);
  if (swap_ && (w != pw_ || h != ph_)) {
    back_.Reset();
    g.dc->SetTarget(nullptr);
    if (FAILED(swap_->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) {
      dropDevice();
      gen_ = g.generation;
    }
  }
  pw_ = w;
  ph_ = h;
  if (!swap_) {
    swap_ = g.compositionSwapChain(w, h);
    if (!swap_) return false;
    ComPtr<IDXGISwapChain2> s2;
    if (SUCCEEDED(swap_.As(&s2))) {
      s2->SetMaximumFrameLatency(1);
      wait_ = s2->GetFrameLatencyWaitableObject();
    }
    if (FAILED(g.dcomp->CreateTargetForHwnd(hwnd_, TRUE, &target_)) || FAILED(g.dcomp->CreateVisual(&visual_))) return false;
    visual_->SetContent(swap_.Get());
    target_->SetRoot(visual_.Get());
    g.dcomp->Commit();
  }
  if (!back_) back_ = g.backBuffer(swap_.Get(), 96.f * scale_);
  return back_ != nullptr;
}

void UiWindow::frame() {
  if (painting_ || !hwnd_ || !ensureSurface()) return;
  painting_ = true;
  double now = nowSec();
  float dt = clampf((float)(now - last_), 0.001f, 0.05f);
  last_ = now;
  ID2D1DeviceContext* dc = gfx().dc.Get();
  dc->SetTarget(back_.Get());
  dc->SetDpi(96.f * scale_, 96.f * scale_);
  dc->BeginDraw();
  dc->SetTransform(D2D1::Matrix3x2F::Identity());
  Color bg = mica_ ? ui.pal.bg.withA(0.0f) : ui.pal.bg;
  dc->Clear(D2D1::ColorF(bg.r * bg.a, bg.g * bg.a, bg.b * bg.a, bg.a));
  dirty_ = false;
  ui.begin(dc, RectF(0, 0, pw_ / scale_, ph_ / scale_), dt);
  paint();
  ui.end();
  HRESULT hr = dc->EndDraw();
  dc->SetTarget(nullptr);
  dc->SetDpi(96.f, 96.f);
  if (SUCCEEDED(hr)) hr = swap_->Present(1, 0);
  ui.endInput();
  busy_ = ui.animating;
  painting_ = false;
  if (FAILED(hr)) {
    logf("ui: frame failed %08lx", hr);
    dropDevice();
    gfx().release();
  }
  while (!later_.empty()) {
    auto run = std::move(later_.front());
    later_.erase(later_.begin());
    run();
    dirty_ = true;
  }
}

void UiWindow::after(std::function<void()> f) {
  if (painting_) {
    later_.push_back(std::move(f));
  } else {
    f();
    dirty_ = true;
  }
}

bool UiWindow::renderTo(ID2D1Bitmap1* target, float w, float h, int frames, bool dark) {
  headless_ = true;
  headlessTarget_ = target;
  mica_ = false;
  scale_ = target->GetPixelSize().width / w;
  ui.pal = makePalette(dark, Color::hex(0x0067C0));
  ID2D1DeviceContext* dc = gfx().dc.Get();
  HRESULT hr = S_OK;
  for (int i = 0; i < frames && SUCCEEDED(hr); i++) {
    dc->SetTarget(target);
    dc->SetDpi(96.f * scale_, 96.f * scale_);
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->Clear(ui.pal.bg.f());
    ui.begin(dc, RectF(0, 0, w, h), 1.f / 60.f);
    paint();
    ui.end();
    hr = dc->EndDraw();
    dc->SetTarget(nullptr);
    dc->SetDpi(96.f, 96.f);
    ui.endInput();
  }
  return SUCCEEDED(hr);
}

LRESULT CALLBACK UiWindow::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
  auto* self = (UiWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (self) {
    self->hwnd_ = h;
    return self->handle(m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

LRESULT UiWindow::handle(UINT m, WPARAM w, LPARAM l) {
  bool done = false;
  LRESULT r = onMessage(m, w, l, done);
  if (done) return r;
  auto at = [&] { return V2{GET_X_LPARAM(l) / scale_, GET_Y_LPARAM(l) / scale_}; };
  switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd_, &ps);
      EndPaint(hwnd_, &ps);
      frame();
      return 0;
    }
    case WM_SIZE:
      if (w != SIZE_MINIMIZED) {
        dirty_ = true;
        if (IsWindowVisible(hwnd_)) frame();
      }
      return 0;
    case WM_ENTERSIZEMOVE: SetTimer(hwnd_, 0xF00, 16, nullptr); return 0;
    case WM_EXITSIZEMOVE: KillTimer(hwnd_, 0xF00); return 0;
    case WM_TIMER:
      if (w == 0xF00 && (dirty_ || busy_)) frame();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = (MINMAXINFO*)l;
      RECT q{0, 0, (LONG)(minW_ * scale_), (LONG)(minH_ * scale_)};
      AdjustWindowRectExForDpi(&q, (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE), FALSE, 0, (UINT)(96 * scale_));
      mm->ptMinTrackSize = {q.right - q.left, q.bottom - q.top};
      return 0;
    }
    case WM_DPICHANGED: {
      scale_ = LOWORD(w) / 96.f;
      back_.Reset();
      auto* q = (RECT*)l;
      SetWindowPos(hwnd_, nullptr, q->left, q->top, q->right - q->left, q->bottom - q->top, SWP_NOZORDER | SWP_NOACTIVATE);
      dirty_ = true;
      return 0;
    }
    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&t);
      ui.mouse = at();
      dirty_ = true;
      return 0;
    }
    case WM_MOUSELEAVE:
      if (!ui.down) ui.mouse = {-1000, -1000};
      dirty_ = true;
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      SetCapture(hwnd_);
      ui.mouse = at();
      ui.down = ui.pressed = true;
      ui.dbl = m == WM_LBUTTONDBLCLK;
      frame();  // handle each click at the position it happened
      return 0;
    case WM_LBUTTONUP:
      ui.mouse = at();
      ui.down = false;
      ui.released = true;
      ReleaseCapture();
      frame();
      return 0;
    case WM_RBUTTONUP:
      ui.mouse = at();
      ui.rightPressed = true;
      frame();
      return 0;
    case WM_CAPTURECHANGED:
      if (ui.down) {
        ui.down = false;
        ui.released = true;
        dirty_ = true;
      }
      return 0;
    case WM_MOUSEWHEEL: {
      POINT q{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(hwnd_, &q);
      ui.mouse = {q.x / scale_, q.y / scale_};
      ui.wheel += GET_WHEEL_DELTA_WPARAM(w) / (float)WHEEL_DELTA;
      dirty_ = true;
      return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (m == WM_SYSKEYDOWN) break;
      ui.keys.push_back({(UINT)w, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0});
      dirty_ = true;
      return 0;
    case WM_CHAR:
      if (!(GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) ui.chars += (wchar_t)w;
      dirty_ = true;
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT) {
        SetCursor(ui.cursor ? ui.cursor : LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
      }
      break;
    case kDropMsg:
      while (!drops_.empty()) {
        auto one = std::move(drops_.front());
        drops_.erase(drops_.begin());
        after([this, one] { onDrop(one.first, one.second); });
      }
      dirty_ = true;
      return 0;
    case WM_ACTIVATE:
      dirty_ = true;
      if (LOWORD(w) == WA_INACTIVE) onDeactivate();
      break;
    case WM_CLOSE:
      if (onClose()) {
        ShowWindow(hwnd_, SW_HIDE);
      }
      return 0;
  }
  return DefWindowProcW(hwnd_, m, w, l);
}

}  // namespace rt
