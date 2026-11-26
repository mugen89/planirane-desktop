// GPU device, text and drawing helpers shared by the overlay, settings and setup.
#pragma once
#include "common.h"

namespace rt {

// Built-in effect property indices that the MinGW headers leave out (values are part of the Direct2D ABI).
namespace fx {
constexpr UINT32 kScale = 0, kScaleInterpolation = 2, kScaleBorder = 3;
constexpr UINT32 kScaleLinear = 1;
constexpr UINT32 kSaturation = 0;
constexpr UINT32 kBlurBalanced = 1;
}  // namespace fx

struct Stop { float at; Color c; };

enum class Align { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct TextStyle {
  std::wstring family = L"Segoe UI Variable Text";
  float size = 14.f;
  int weight = 400;
  Align align = Align::Left;
  VAlign valign = VAlign::Middle;
  bool wrap = false;
  bool ellipsis = true;
  float tracking = 0.f;
};

class Gfx {
 public:
  ComPtr<ID2D1Factory1> d2d;      // lives for the whole process
  ComPtr<IDWriteFactory> dwrite;  // lives for the whole process
  ComPtr<IWICImagingFactory> wic;

  ComPtr<ID3D11Device> d3d;
  ComPtr<IDXGIDevice1> dxgi;
  ComPtr<IDXGIFactory2> factory;
  ComPtr<ID2D1Device> dev;
  ComPtr<ID2D1DeviceContext> dc;     // main drawing context
  ComPtr<ID2D1DeviceContext> aux;    // records command lists / offscreen work
  ComPtr<IDCompositionDevice> dcomp;
  uint32_t generation = 0;           // changes whenever the device is rebuilt
  std::vector<std::function<void()>> onRelease;  // owners of GPU objects let go here, or the device would live on
  bool warp = false;

  bool ensureFactories();
  bool ensure();                     // creates the device if needed
  bool ready() const { return dc != nullptr; }
  void release();                    // frees every GPU object
  void shutdown();                   // also frees the factories; call before COM goes away
  void trim();
  bool isDeviceError(HRESULT hr) const;
  void prewarm();                    // compiles the effects used on first open

  ComPtr<ID2D1Bitmap1> bitmap(const void* bgra, int w, int h, int stride, bool ignoreAlpha = false);
  ComPtr<ID2D1Bitmap1> target(int w, int h);
  ComPtr<ID2D1Bitmap1> readable(int w, int h);
  ComPtr<IDXGISwapChain1> compositionSwapChain(int w, int h);
  ComPtr<ID2D1Bitmap1> backBuffer(IDXGISwapChain1* sc, float dpi = 96.f);

  bool hasFont(const std::wstring& family);
  std::wstring pickFont(const std::wstring& wanted);  // falls back to Segoe UI
  ComPtr<IDWriteTextFormat> format(const TextStyle& ts);
  ComPtr<IDWriteTextLayout> layout(std::wstring_view text, const TextStyle& ts, float w, float h);
  V2 measure(std::wstring_view text, const TextStyle& ts, float maxW = 10000.f);

 private:
  std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> formats_;
  std::unordered_map<std::wstring, bool> fonts_;
};
Gfx& gfx();

// ---------------------------------------------------------------- shapes
using Geo = ComPtr<ID2D1Geometry>;
Geo wedgeGeo(V2 c, float rIn, float rOut, float a0, float a1, float gap, float corner);
Geo bladeGeo(V2 c, float rIn, float rOut, float a0, float a1, float gap, float cut, float skew);
Geo petalGeo(V2 c, float rIn, float rOut, float mid, float halfWidth);
Geo hexGeo(V2 c, float radius, float rot, float corner);
Geo circleGeo(V2 c, float r);
Geo ringGeo(V2 c, float rIn, float rOut);
Geo rrectGeo(RectF r, float corner);
Geo cutRectGeo(RectF r, float cut);
Geo arcGeo(V2 c, float r, float a0, float a1);          // open arc, for stroking
Geo polyGeo(const std::vector<V2>& pts, bool closed, bool smooth);

// ---------------------------------------------------------------- painter
class Painter {
 public:
  ID2D1DeviceContext* dc = nullptr;

  void attach(ID2D1DeviceContext* ctx);
  ID2D1SolidColorBrush* solid(Color c);
  ComPtr<ID2D1Brush> linear(V2 a, V2 b, const std::vector<Stop>& stops);
  ComPtr<ID2D1Brush> radial(V2 c, float rx, float ry, const std::vector<Stop>& stops, V2 focus = {});
  ComPtr<ID2D1Brush> image(ID2D1Bitmap1* bmp, V2 origin, float scale, float opacity = 1.f);

  void fill(ID2D1Geometry* g, Color c);
  void fill(ID2D1Geometry* g, ID2D1Brush* b);
  void stroke(ID2D1Geometry* g, Color c, float w, bool roundCaps = false);
  void stroke(ID2D1Geometry* g, ID2D1Brush* b, float w, bool roundCaps = false);
  void circle(V2 c, float r, Color col);
  void circleStroke(V2 c, float r, Color col, float w);
  void rrect(RectF r, float corner, Color c);
  void rrect(RectF r, float corner, ID2D1Brush* b);
  void rrectStroke(RectF r, float corner, Color c, float w);
  void line(V2 a, V2 b, Color c, float w, bool roundCaps = true);
  void bitmap(ID2D1Bitmap1* b, RectF dst, float opacity = 1.f, const D2D1_RECT_F* src = nullptr);

  void text(std::wstring_view s, const TextStyle& ts, RectF r, Color c);
  void text(std::wstring_view s, const TextStyle& ts, RectF r, ID2D1Brush* b);
  // Text with a soft blurred halo so it stays readable over any wallpaper.
  void textHalo(std::wstring_view s, const TextStyle& ts, RectF r, Color c, Color halo, float sigma);

  // Blurred stroke/fill of a shape, drawn additively for light or normally for shadow.
  void glow(ID2D1Geometry* g, Color c, float sigma, float strokeW, bool fillShape = false, bool additive = true);

  ID2D1StrokeStyle* roundCapStyle();
  void drop();  // lets go of cached brushes and effects, which would otherwise keep the device alive

 private:
  ComPtr<ID2D1SolidColorBrush> solid_;
  ComPtr<ID2D1Effect> blur_;
  ComPtr<ID2D1StrokeStyle> round_;
  uint32_t gen_ = 0;
  void sync();
};
Painter& painter();

// Saves a D2D bitmap (GPU) as a PNG file; used by the headless render checks.
bool savePng(ID2D1Bitmap1* src, const std::wstring& path);

}  // namespace rt
