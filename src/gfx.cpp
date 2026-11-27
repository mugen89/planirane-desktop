#include "gfx.h"

namespace rt {

Gfx& gfx() {
  static Gfx g;
  return g;
}
Painter& painter() {
  static Painter p;
  return p;
}

// ---------------------------------------------------------------- device
bool Gfx::ensureFactories() {
  if (!d2d) {
    D2D1_FACTORY_OPTIONS opt{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opt, (void**)d2d.GetAddressOf()))) return false;
  }
  if (!dwrite) {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf()))) return false;
  }
  if (!wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (void**)wic.GetAddressOf());
  return true;
}

bool Gfx::ensure() {
  if (dc && d3d && FAILED(d3d->GetDeviceRemovedReason())) release();  // lost while we were idle
  if (dc) return true;
  if (!ensureFactories()) return false;
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
  warp = false;
  if (FAILED(hr)) {
    warp = true;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
  }
  if (FAILED(hr)) { logf("gfx: no D3D device %08lx", hr); return false; }
  hr = d3d.As(&dxgi);
  ComPtr<IDXGIAdapter> ad;
  if (SUCCEEDED(hr)) hr = dxgi->GetAdapter(&ad);
  if (SUCCEEDED(hr)) hr = ad->GetParent(__uuidof(IDXGIFactory2), (void**)factory.GetAddressOf());
  if (SUCCEEDED(hr)) hr = d2d->CreateDevice(dxgi.Get(), &dev);
  if (SUCCEEDED(hr)) hr = dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
  if (SUCCEEDED(hr)) hr = dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &aux);
  if (SUCCEEDED(hr)) hr = DCompositionCreateDevice(dxgi.Get(), __uuidof(IDCompositionDevice), (void**)dcomp.GetAddressOf());
  if (FAILED(hr)) {
    logf("gfx: device setup failed %08lx", hr);
    release();
    return false;
  }
  dxgi->SetMaximumFrameLatency(1);
  dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  aux->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  generation++;
  return true;
}

void Gfx::shutdown() {
  release();
  formats_.clear();
  wic.Reset();
  dwrite.Reset();
  d2d.Reset();
}

void Gfx::release() {
  painter().drop();
  for (auto& f : onRelease) f();
  if (dc) dc->SetTarget(nullptr);
  if (aux) aux->SetTarget(nullptr);
  dcomp.Reset();
  aux.Reset();
  dc.Reset();
  dev.Reset();
  factory.Reset();
  if (d3d) {
    ComPtr<ID3D11DeviceContext> ctx;
    d3d->GetImmediateContext(&ctx);
    if (ctx) { ctx->ClearState(); ctx->Flush(); }
  }
  dxgi.Reset();
  d3d.Reset();
  generation++;
}

void Gfx::trim() {
  if (!dxgi) return;
  ComPtr<IDXGIDevice3> d3;
  if (SUCCEEDED(dxgi.As(&d3))) d3->Trim();
}

bool Gfx::isDeviceError(HRESULT hr) const {
  return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
         hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

void Gfx::prewarm() {
  if (!ensure()) return;
  auto t = target(64, 64);
  auto s = target(64, 64);
  if (!t || !s) return;
  ComPtr<ID2D1Effect> blur, sat;
  aux->CreateEffect(CLSID_D2D1GaussianBlur, &blur);
  aux->CreateEffect(CLSID_D2D1Saturation, &sat);
  if (!blur || !sat) return;
  blur->SetInput(0, s.Get());
  blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 8.f);
  blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
  sat->SetInputEffect(0, blur.Get());
  aux->SetTarget(t.Get());
  aux->BeginDraw();
  aux->DrawImage(sat.Get());
  aux->EndDraw();
  aux->SetTarget(nullptr);
  Geo g = wedgeGeo({32, 32}, 8, 28, 0.f, 1.f, 2.f, 3.f);
  aux->SetTarget(t.Get());
  aux->BeginDraw();
  Painter p;
  p.attach(aux.Get());
  p.fill(g.Get(), Color(1, 1, 1, 1));
  p.stroke(g.Get(), Color(1, 1, 1, 1), 1.f);
  auto b = p.linear({0, 0}, {64, 64}, {{0, Color(1, 1, 1, 1)}, {1, Color(0, 0, 0, 0)}});
  p.fill(g.Get(), b.Get());
  auto rb = p.radial({32, 32}, 30, 30, {{0, Color(1, 1, 1, 1)}, {1, Color(0, 0, 0, 0)}});
  p.fill(g.Get(), rb.Get());
  TextStyle ts;
  p.text(L"Aa", ts, RectF(0, 0, 64, 64), Color(1, 1, 1, 1));
  aux->EndDraw();
  aux->SetTarget(nullptr);
}

// ---------------------------------------------------------------- surfaces
ComPtr<ID2D1Bitmap1> Gfx::bitmap(const void* bgra, int w, int h, int stride, bool ignoreAlpha) {
  ComPtr<ID2D1Bitmap1> b;
  if (!dc || w <= 0 || h <= 0) return b;
  auto p = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, ignoreAlpha ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED));
  dc->CreateBitmap(D2D1::SizeU((UINT)w, (UINT)h), bgra, (UINT)stride, p, &b);
  return b;
}
ComPtr<ID2D1Bitmap1> Gfx::target(int w, int h) {
  ComPtr<ID2D1Bitmap1> b;
  if (!dc || w <= 0 || h <= 0) return b;
  auto p = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  dc->CreateBitmap(D2D1::SizeU((UINT)w, (UINT)h), nullptr, 0, p, &b);
  return b;
}
ComPtr<ID2D1Bitmap1> Gfx::readable(int w, int h) {
  ComPtr<ID2D1Bitmap1> b;
  if (!dc || w <= 0 || h <= 0) return b;
  auto p = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  dc->CreateBitmap(D2D1::SizeU((UINT)w, (UINT)h), nullptr, 0, p, &b);
  return b;
}
ComPtr<IDXGISwapChain1> Gfx::compositionSwapChain(int w, int h) {
  ComPtr<IDXGISwapChain1> sc;
  if (!factory) return sc;
  DXGI_SWAP_CHAIN_DESC1 d{};
  d.Width = (UINT)std::max(1, w);
  d.Height = (UINT)std::max(1, h);
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  d.BufferCount = 2;
  d.Scaling = DXGI_SCALING_STRETCH;
  d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  HRESULT hr = factory->CreateSwapChainForComposition(d3d.Get(), &d, nullptr, &sc);
  if (FAILED(hr)) logf("gfx: swap chain failed %08lx", hr);
  return sc;
}
ComPtr<ID2D1Bitmap1> Gfx::backBuffer(IDXGISwapChain1* sc, float dpi) {
  ComPtr<ID2D1Bitmap1> b;
  ComPtr<IDXGISurface> s;
  if (!sc || FAILED(sc->GetBuffer(0, __uuidof(IDXGISurface), (void**)s.GetAddressOf()))) return b;
  auto p = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
  dc->CreateBitmapFromDxgiSurface(s.Get(), &p, &b);
  return b;
}

// ---------------------------------------------------------------- text
bool Gfx::hasFont(const std::wstring& family) {
  if (!ensureFactories()) return false;
  auto it = fonts_.find(family);
  if (it != fonts_.end()) return it->second;
  ComPtr<IDWriteFontCollection> col;
  BOOL exists = FALSE;
  UINT32 idx = 0;
  if (SUCCEEDED(dwrite->GetSystemFontCollection(&col, FALSE))) col->FindFamilyName(family.c_str(), &idx, &exists);
  fonts_[family] = exists != FALSE;
  return exists != FALSE;
}
std::wstring Gfx::pickFont(const std::wstring& wanted) {
  if (hasFont(wanted)) return wanted;
  if (wanted.rfind(L"Segoe Fluent", 0) == 0) return L"Segoe MDL2 Assets";
  return L"Segoe UI";
}
ComPtr<IDWriteTextFormat> Gfx::format(const TextStyle& ts) {
  if (!ensureFactories()) return {};
  wchar_t key[160];
  swprintf(key, 160, L"%ls|%.2f|%d|%d|%d|%d|%d", ts.family.c_str(), ts.size, ts.weight, (int)ts.align, (int)ts.valign, ts.wrap, ts.ellipsis);
  auto it = formats_.find(key);
  if (it != formats_.end()) return it->second;
  if (formats_.size() > 300) formats_.clear();
  ComPtr<IDWriteTextFormat> f;
  std::wstring fam = pickFont(ts.family);
  if (FAILED(dwrite->CreateTextFormat(fam.c_str(), nullptr, (DWRITE_FONT_WEIGHT)ts.weight, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, std::max(1.f, ts.size), L"", &f)))
    return {};
  f->SetTextAlignment(ts.align == Align::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                      : ts.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
  f->SetParagraphAlignment(ts.valign == VAlign::Top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                           : ts.valign == VAlign::Middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
  f->SetWordWrapping(ts.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  if (ts.ellipsis) {
    DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> sign;
    dwrite->CreateEllipsisTrimmingSign(f.Get(), &sign);
    f->SetTrimming(&tr, sign.Get());
  }
  formats_[key] = f;
  return f;
}
ComPtr<IDWriteTextLayout> Gfx::layout(std::wstring_view text, const TextStyle& ts, float w, float h) {
  ComPtr<IDWriteTextLayout> l;
  auto f = format(ts);
  if (!f) return l;
  dwrite->CreateTextLayout(text.data(), (UINT32)text.size(), f.Get(), std::max(0.f, w), std::max(0.f, h), &l);
  if (l && ts.tracking != 0.f) {
    ComPtr<IDWriteTextLayout1> l1;
    if (SUCCEEDED(l.As(&l1))) l1->SetCharacterSpacing(0, ts.tracking, 0, DWRITE_TEXT_RANGE{0, (UINT32)text.size()});
  }
  return l;
}
V2 Gfx::measure(std::wstring_view text, const TextStyle& ts, float maxW) {
  TextStyle t = ts;
  t.align = Align::Left;
  t.valign = VAlign::Top;
  auto l = layout(text, t, maxW, 10000.f);
  DWRITE_TEXT_METRICS m{};
  if (l) l->GetMetrics(&m);
  return {m.widthIncludingTrailingWhitespace, m.height};
}

// ---------------------------------------------------------------- shapes
namespace {
struct PathBuilder {
  ComPtr<ID2D1PathGeometry> g;
  ComPtr<ID2D1GeometrySink> s;
  explicit PathBuilder(V2 start, bool filled = true) {
    gfx().ensureFactories();
    gfx().d2d->CreatePathGeometry(g.GetAddressOf());
    if (g) g->Open(&s);
    if (s) {
      s->SetFillMode(D2D1_FILL_MODE_WINDING);
      s->BeginFigure(start.p(), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    }
  }
  void line(V2 p) { if (s) s->AddLine(p.p()); }
  void arc(V2 p, float r, bool cw, bool large = false) {
    if (!s) return;
    if (r < 0.01f) { s->AddLine(p.p()); return; }
    s->AddArc(D2D1::ArcSegment(p.p(), D2D1::SizeF(r, r), 0.f, cw ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                               large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
  }
  void cubic(V2 a, V2 b, V2 c) { if (s) s->AddBezier(D2D1::BezierSegment(a.p(), b.p(), c.p())); }
  Geo done(bool closed = true) {
    if (s) {
      s->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
      s->Close();
    }
    Geo out;
    if (g) g.As(&out);
    return out;
  }
};
float safeAsin(float v) { return asinf(clampf(v, -1.f, 1.f)); }
}  // namespace

Geo wedgeGeo(V2 c, float rIn, float rOut, float a0, float a1, float gap, float corner) {
  rIn = std::max(0.5f, rIn);
  rOut = std::max(rIn + 1.f, rOut);
  float span = a1 - a0;
  if (span >= kTau - 0.001f) return ringGeo(c, rIn, rOut);
  float hg = gap * 0.5f, half = span * 0.5f;
  float co = std::max(0.f, std::min(corner, (rOut - rIn) * 0.48f));
  float ci = co;
  // Shrink corners until they fit the angular room available at each radius.
  for (int i = 0; i < 24 && safeAsin((hg + co) / std::max(1.f, rOut - co)) > half * 0.96f; i++) co *= 0.8f;
  for (int i = 0; i < 24 && safeAsin((hg + ci) / (rIn + ci)) > half * 0.96f; i++) ci *= 0.8f;
  float to = std::min(safeAsin((hg + co) / std::max(1.f, rOut - co)), half * 0.98f);
  float ti = std::min(safeAsin((hg + ci) / (rIn + ci)), half * 0.98f);
  float Lo = (rOut - co) * cosf(to), Li = (rIn + ci) * cosf(ti);
  V2 u0{sinf(a0), -cosf(a0)}, p0{cosf(a0), sinf(a0)};
  V2 u1{sinf(a1), -cosf(a1)}, p1{cosf(a1), sinf(a1)};

  PathBuilder b(polar(c, a0 + to, rOut));
  b.arc(polar(c, a1 - to, rOut), rOut, true, span - 2 * to > kPi);
  b.arc(c + u1 * Lo - p1 * hg, co, true);
  b.line(c + u1 * Li - p1 * hg);
  b.arc(polar(c, a1 - ti, rIn), ci, true);
  b.arc(polar(c, a0 + ti, rIn), rIn, false, span - 2 * ti > kPi);
  b.arc(c + u0 * Li + p0 * hg, ci, true);
  b.line(c + u0 * Lo + p0 * hg);
  b.arc(polar(c, a0 + to, rOut), co, true);
  return b.done();
}

Geo bladeGeo(V2 c, float rIn, float rOut, float a0, float a1, float gap, float cut, float skew) {
  rIn = std::max(0.5f, rIn);
  rOut = std::max(rIn + 1.f, rOut);
  float span = a1 - a0;
  if (span >= kTau - 0.001f) return ringGeo(c, rIn, rOut);
  float hg = gap * 0.5f, half = span * 0.5f;
  float go = std::min(safeAsin(hg / rOut), half * 0.9f), gi = std::min(safeAsin(hg / rIn), half * 0.9f);
  float th = rOut - rIn;
  cut = std::max(0.f, std::min({cut, th * 0.45f, rOut * (half - go) * 0.8f}));
  float inCut = std::max(0.f, std::min(cut * 0.55f, rIn * (half - gi) * 0.8f));
  float ca = cut / rOut, cia = inCut / rIn;
  V2 oL = polar(c, a0 + go + skew, rOut), oR = polar(c, a1 - go + skew, rOut);
  V2 iL = polar(c, a0 + gi, rIn), iR = polar(c, a1 - gi, rIn);
  float eL = std::max(1.f, (oL - iL).len()), eR = std::max(1.f, (oR - iR).len());

  PathBuilder b(polar(c, a0 + go + skew + ca, rOut));
  b.arc(polar(c, a1 - go + skew - ca, rOut), rOut, true, span - 2 * go - 2 * ca > kPi);
  b.line(lerp(oR, iR, cut / eR));
  b.line(lerp(iR, oR, inCut / eR));
  b.line(polar(c, a1 - gi - cia, rIn));
  b.arc(polar(c, a0 + gi + cia, rIn), rIn, false, span - 2 * gi - 2 * cia > kPi);
  b.line(lerp(iL, oL, inCut / eL));
  b.line(lerp(oL, iL, cut / eL));
  return b.done();
}

Geo petalGeo(V2 c, float rIn, float rOut, float mid, float w) {
  float h = rOut - rIn;
  V2 u{sinf(mid), -cosf(mid)}, p{cosf(mid), sinf(mid)};
  auto at = [&](float r, float s) { return c + u * r + p * s; };
  // Lotus petal: rounded foot near the hub, widest past the middle, soft point at the rim.
  float foot = std::max(3.f, w * 0.34f);
  PathBuilder b(at(rIn, foot));
  b.cubic(at(rIn + h * 0.16f, w * 0.80f), at(rIn + h * 0.30f, w), at(rIn + h * 0.50f, w));
  b.cubic(at(rIn + h * 0.76f, w), at(rIn + h * 0.92f, w * 0.34f), at(rOut, w * 0.07f));
  b.cubic(at(rOut + h * 0.012f, w * 0.03f), at(rOut + h * 0.012f, -w * 0.03f), at(rOut, -w * 0.07f));
  b.cubic(at(rIn + h * 0.92f, -w * 0.34f), at(rIn + h * 0.76f, -w), at(rIn + h * 0.50f, -w));
  b.cubic(at(rIn + h * 0.30f, -w), at(rIn + h * 0.16f, -w * 0.80f), at(rIn, -foot));
  b.cubic(at(rIn - foot * 0.35f, -foot * 0.45f), at(rIn - foot * 0.35f, foot * 0.45f), at(rIn, foot));
  return b.done();
}

Geo hexGeo(V2 c, float R, float rot, float corner) {
  float edge = R;  // a regular hexagon's side equals its circumradius
  float d = std::min(corner * 0.57735f, edge * 0.45f);
  float rad = d / 0.57735f;
  V2 v[6];
  for (int i = 0; i < 6; i++) v[i] = polar(c, rot + i * kTau / 6.f, R);
  auto toward = [&](V2 a, V2 b) { V2 dd = b - a; float l = std::max(0.001f, dd.len()); return a + dd * (d / l); };
  PathBuilder b(toward(v[0], v[1]));
  for (int i = 1; i <= 6; i++) {
    V2 cur = v[i % 6], prev = v[(i - 1) % 6], next = v[(i + 1) % 6];
    b.line(toward(cur, prev));
    b.arc(toward(cur, next), rad, true);
  }
  return b.done();
}

Geo circleGeo(V2 c, float r) {
  gfx().ensureFactories();
  ComPtr<ID2D1EllipseGeometry> e;
  gfx().d2d->CreateEllipseGeometry(D2D1::Ellipse(c.p(), std::max(0.1f, r), std::max(0.1f, r)), &e);
  Geo o;
  if (e) e.As(&o);
  return o;
}

Geo ringGeo(V2 c, float rIn, float rOut) {
  gfx().ensureFactories();
  ComPtr<ID2D1PathGeometry> g;
  gfx().d2d->CreatePathGeometry(g.GetAddressOf());
  if (!g) return {};
  ComPtr<ID2D1GeometrySink> s;
  g->Open(&s);
  s->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
  for (float r : {rOut, rIn}) {
    s->BeginFigure(D2D1::Point2F(c.x, c.y - r), D2D1_FIGURE_BEGIN_FILLED);
    s->AddArc(D2D1::ArcSegment(D2D1::Point2F(c.x, c.y + r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    s->AddArc(D2D1::ArcSegment(D2D1::Point2F(c.x, c.y - r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
  }
  s->Close();
  Geo o;
  g.As(&o);
  return o;
}

Geo rrectGeo(RectF r, float corner) {
  gfx().ensureFactories();
  ComPtr<ID2D1RoundedRectangleGeometry> g;
  corner = std::max(0.f, std::min({corner, r.w() * 0.5f, r.h() * 0.5f}));
  gfx().d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r.f(), corner, corner), &g);
  Geo o;
  if (g) g.As(&o);
  return o;
}

Geo cutRectGeo(RectF r, float cut) {
  cut = std::max(0.f, std::min({cut, r.w() * 0.45f, r.h() * 0.45f}));
  PathBuilder b({r.l + cut, r.t});
  b.line({r.r - cut * 0.4f, r.t});
  b.line({r.r, r.t + cut * 0.4f});
  b.line({r.r, r.b - cut});
  b.line({r.r - cut, r.b});
  b.line({r.l + cut * 0.4f, r.b});
  b.line({r.l, r.b - cut * 0.4f});
  b.line({r.l, r.t + cut});
  return b.done();
}

Geo arcGeo(V2 c, float r, float a0, float a1) {
  if (a1 - a0 >= kTau - 0.001f) return circleGeo(c, r);
  PathBuilder b(polar(c, a0, r), false);
  b.arc(polar(c, a1, r), r, true, (a1 - a0) > kPi);
  return b.done(false);
}

Geo polyGeo(const std::vector<V2>& pts, bool closed, bool smooth) {
  if (pts.size() < 2) return {};
  PathBuilder b(pts[0], closed);
  size_t n = pts.size();
  if (!smooth) {
    for (size_t i = 1; i < n; i++) b.line(pts[i]);
  } else {
    // Catmull-Rom through the points, expressed as cubic Beziers.
    size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; i++) {
      V2 p0 = closed ? pts[(i + n - 1) % n] : pts[i ? i - 1 : 0];
      V2 p1 = pts[i % n], p2 = pts[(i + 1) % n];
      V2 p3 = closed ? pts[(i + 2) % n] : pts[std::min(i + 2, n - 1)];
      b.cubic(p1 + (p2 - p0) * (1.f / 6.f), p2 - (p3 - p1) * (1.f / 6.f), p2);
    }
  }
  return b.done(closed);
}

// ---------------------------------------------------------------- painter
void Painter::attach(ID2D1DeviceContext* ctx) {
  dc = ctx;
  sync();
}
void Painter::drop() {
  solid_.Reset();
  blur_.Reset();
  round_.Reset();
  dc = nullptr;
}
void Painter::sync() {
  if (gen_ != gfx().generation) {
    solid_.Reset();
    blur_.Reset();
    gen_ = gfx().generation;
  }
}
ID2D1SolidColorBrush* Painter::solid(Color c) {
  sync();
  if (!solid_) dc->CreateSolidColorBrush(c.f(), &solid_);
  if (solid_) solid_->SetColor(c.f());
  return solid_.Get();
}
ComPtr<ID2D1Brush> Painter::linear(V2 a, V2 b, const std::vector<Stop>& stops) {
  D2D1_GRADIENT_STOP gs[16];
  UINT n = (UINT)std::min<size_t>(stops.size(), 16);
  for (UINT i = 0; i < n; i++) gs[i] = {saturate(stops[i].at), stops[i].c.f()};
  ComPtr<ID2D1GradientStopCollection> col;
  dc->CreateGradientStopCollection(gs, n, &col);
  ComPtr<ID2D1LinearGradientBrush> br;
  if (col) dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a.p(), b.p()), col.Get(), &br);
  ComPtr<ID2D1Brush> o;
  if (br) br.As(&o);
  return o;
}
ComPtr<ID2D1Brush> Painter::radial(V2 c, float rx, float ry, const std::vector<Stop>& stops, V2 focus) {
  D2D1_GRADIENT_STOP gs[16];
  UINT n = (UINT)std::min<size_t>(stops.size(), 16);
  for (UINT i = 0; i < n; i++) gs[i] = {saturate(stops[i].at), stops[i].c.f()};
  ComPtr<ID2D1GradientStopCollection> col;
  dc->CreateGradientStopCollection(gs, n, &col);
  ComPtr<ID2D1RadialGradientBrush> br;
  if (col)
    dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(c.p(), focus.p(), std::max(0.1f, rx), std::max(0.1f, ry)), col.Get(), &br);
  ComPtr<ID2D1Brush> o;
  if (br) br.As(&o);
  return o;
}
ComPtr<ID2D1Brush> Painter::image(ID2D1Bitmap1* bmp, V2 origin, float scale, float opacity) {
  ComPtr<ID2D1BitmapBrush1> br;
  if (bmp) {
    dc->CreateBitmapBrush(bmp,
                          D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_INTERPOLATION_MODE_LINEAR),
                          D2D1::BrushProperties(opacity, D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(origin.x, origin.y)), &br);
  }
  ComPtr<ID2D1Brush> o;
  if (br) br.As(&o);
  return o;
}

ID2D1StrokeStyle* Painter::roundCapStyle() {
  if (!round_) {
    gfx().ensureFactories();
    gfx().d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                 nullptr, 0, round_.GetAddressOf());
  }
  return round_.Get();
}

void Painter::fill(ID2D1Geometry* g, Color c) { if (g && c.a > 0.001f) dc->FillGeometry(g, solid(c)); }
void Painter::fill(ID2D1Geometry* g, ID2D1Brush* b) { if (g && b) dc->FillGeometry(g, b); }
void Painter::stroke(ID2D1Geometry* g, Color c, float w, bool rc) {
  if (g && c.a > 0.001f && w > 0.01f) dc->DrawGeometry(g, solid(c), w, rc ? roundCapStyle() : nullptr);
}
void Painter::stroke(ID2D1Geometry* g, ID2D1Brush* b, float w, bool rc) {
  if (g && b && w > 0.01f) dc->DrawGeometry(g, b, w, rc ? roundCapStyle() : nullptr);
}
void Painter::circle(V2 c, float r, Color col) {
  if (r > 0.01f && col.a > 0.001f) dc->FillEllipse(D2D1::Ellipse(c.p(), r, r), solid(col));
}
void Painter::circleStroke(V2 c, float r, Color col, float w) {
  if (r > 0.01f && col.a > 0.001f) dc->DrawEllipse(D2D1::Ellipse(c.p(), r, r), solid(col), w);
}
void Painter::rrect(RectF r, float corner, Color c) {
  if (c.a > 0.001f) dc->FillRoundedRectangle(D2D1::RoundedRect(r.f(), corner, corner), solid(c));
}
void Painter::rrect(RectF r, float corner, ID2D1Brush* b) {
  if (b) dc->FillRoundedRectangle(D2D1::RoundedRect(r.f(), corner, corner), b);
}
void Painter::rrectStroke(RectF r, float corner, Color c, float w) {
  if (c.a > 0.001f) dc->DrawRoundedRectangle(D2D1::RoundedRect(r.f(), corner, corner), solid(c), w);
}
void Painter::line(V2 a, V2 b, Color c, float w, bool rc) {
  if (c.a > 0.001f) dc->DrawLine(a.p(), b.p(), solid(c), w, rc ? roundCapStyle() : nullptr);
}
void Painter::bitmap(ID2D1Bitmap1* b, RectF dst, float opacity, const D2D1_RECT_F* src) {
  if (!b || opacity <= 0.002f) return;
  D2D1_RECT_F d = dst.f();
  dc->DrawBitmap(b, &d, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, src, nullptr);
}

void Painter::text(std::wstring_view s, const TextStyle& ts, RectF r, Color c) {
  if (c.a > 0.001f) text(s, ts, r, (ID2D1Brush*)solid(c));
}
void Painter::text(std::wstring_view s, const TextStyle& ts, RectF r, ID2D1Brush* b) {
  if (s.empty() || !b) return;
  auto l = gfx().layout(s, ts, r.w(), r.h());
  if (l) dc->DrawTextLayout(D2D1::Point2F(r.l, r.t), l.Get(), b, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void Painter::textHalo(std::wstring_view s, const TextStyle& ts, RectF r, Color c, Color halo, float sigma) {
  if (s.empty() || c.a <= 0.001f) return;
  auto l = gfx().layout(s, ts, r.w(), r.h());
  if (!l) return;
  if (halo.a > 0.001f && sigma > 0.1f) {
    sync();
    ID2D1DeviceContext* aux = gfx().aux.Get();
    ComPtr<ID2D1CommandList> cl;
    if (aux && SUCCEEDED(aux->CreateCommandList(&cl))) {
      aux->SetTarget(cl.Get());
      aux->SetTransform(D2D1::Matrix3x2F::Identity());
      aux->BeginDraw();
      ComPtr<ID2D1SolidColorBrush> br;
      aux->CreateSolidColorBrush(halo.f(), &br);
      if (br) {
        aux->DrawTextLayout(D2D1::Point2F(r.l, r.t), l.Get(), br.Get());
        aux->DrawTextLayout(D2D1::Point2F(r.l, r.t), l.Get(), br.Get());
      }
      aux->EndDraw();
      aux->SetTarget(nullptr);
      cl->Close();
      if (!blur_) dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur_);
      if (blur_) {
        blur_->SetInput(0, cl.Get());
        blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
        blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, fx::kBlurBalanced);
        dc->DrawImage(blur_.Get());
        blur_->SetInput(0, nullptr);
      }
    }
  }
  dc->DrawTextLayout(D2D1::Point2F(r.l, r.t), l.Get(), solid(c), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void Painter::glow(ID2D1Geometry* g, Color c, float sigma, float strokeW, bool fillShape, bool additive) {
  if (!g || c.a <= 0.002f || sigma < 0.2f) return;
  sync();
  ID2D1DeviceContext* aux = gfx().aux.Get();
  if (!aux || aux == dc) return;
  ComPtr<ID2D1CommandList> cl;
  if (FAILED(aux->CreateCommandList(&cl))) return;
  aux->SetTarget(cl.Get());
  aux->SetTransform(D2D1::Matrix3x2F::Identity());
  aux->BeginDraw();
  ComPtr<ID2D1SolidColorBrush> br;
  aux->CreateSolidColorBrush(c.f(), &br);
  if (br) {
    if (fillShape) aux->FillGeometry(g, br.Get());
    if (strokeW > 0.01f) aux->DrawGeometry(g, br.Get(), strokeW, roundCapStyle());
  }
  aux->EndDraw();
  aux->SetTarget(nullptr);
  cl->Close();
  if (!blur_) dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur_);
  if (!blur_) return;
  blur_->SetInput(0, cl.Get());
  blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
  blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, fx::kBlurBalanced);
  dc->DrawImage(blur_.Get(), D2D1_INTERPOLATION_MODE_LINEAR, additive ? D2D1_COMPOSITE_MODE_PLUS : D2D1_COMPOSITE_MODE_SOURCE_OVER);
  blur_->SetInput(0, nullptr);
}

// ---------------------------------------------------------------- png
// The pixels are premultiplied BGRA (Direct2D's and the layered windows' format); a PNG wants them straight.
bool savePngPixels(const uint8_t* bits, int w, int h, int pitch, const std::wstring& path) {
  auto& g = gfx();
  if (!bits || !g.wic || w <= 0 || h <= 0) return false;
  ComPtr<IWICStream> st;
  ComPtr<IWICBitmapEncoder> enc;
  ComPtr<IWICBitmapFrameEncode> fr;
  HRESULT hr = g.wic->CreateStream(&st);
  if (SUCCEEDED(hr)) hr = st->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (SUCCEEDED(hr)) hr = g.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
  if (SUCCEEDED(hr)) hr = enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&fr, nullptr);
  if (SUCCEEDED(hr)) hr = fr->Initialize(nullptr);
  if (SUCCEEDED(hr)) hr = fr->SetSize((UINT)w, (UINT)h);
  WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = fr->SetPixelFormat(&pf);
  if (SUCCEEDED(hr)) {
    std::vector<uint8_t> row((size_t)w * 4 * h);
    for (int y = 0; y < h; y++) {
      const uint8_t* s = bits + (size_t)y * pitch;
      uint8_t* d = row.data() + (size_t)y * w * 4;
      for (int x = 0; x < w; x++, s += 4, d += 4) {
        unsigned a = s[3];
        for (int k = 0; k < 3; k++) d[k] = a ? (uint8_t)std::min(255u, (s[k] * 255u + a / 2) / a) : 0;
        d[3] = (uint8_t)a;
      }
    }
    hr = fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)row.size(), row.data());
  }
  if (SUCCEEDED(hr)) hr = fr->Commit();
  if (SUCCEEDED(hr)) hr = enc->Commit();
  return SUCCEEDED(hr);
}

bool savePng(ID2D1Bitmap1* src, const std::wstring& path) {
  auto& g = gfx();
  if (!src || !g.wic) return false;
  D2D1_SIZE_U sz = src->GetPixelSize();
  auto cpu = g.readable((int)sz.width, (int)sz.height);
  if (!cpu) return false;
  D2D1_POINT_2U dp{0, 0};
  D2D1_RECT_U sr{0, 0, sz.width, sz.height};
  if (FAILED(cpu->CopyFromBitmap(&dp, src, &sr))) return false;
  D2D1_MAPPED_RECT m{};
  if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return false;
  bool ok = savePngPixels(m.bits, (int)sz.width, (int)sz.height, (int)m.pitch, path);
  cpu->Unmap();
  return ok;
}

}  // namespace rt
