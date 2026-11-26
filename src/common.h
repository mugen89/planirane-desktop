// Shared foundation: Windows headers, small math types, colours, springs, strings.
#pragma once
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <dwrite_2.h>
#include <wincodec.h>
#include <dcomp.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace rt {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

// ---------------------------------------------------------------- math
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.f, 1.f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01(float t) { t = saturate(t); return t * t * (3.f - 2.f * t); }
inline float easeOutCubic(float t) { t = 1.f - saturate(t); return 1.f - t * t * t; }
inline float easeOutQuint(float t) { t = 1.f - saturate(t); return 1.f - t * t * t * t * t; }
// Wrap an angle to (-pi, pi].
inline float wrapAngle(float a) {
  a = fmodf(a + kPi, kTau);
  if (a <= 0) a += kTau;
  return a - kPi;
}

struct V2 {
  float x = 0, y = 0;
  V2() = default;
  V2(float x_, float y_) : x(x_), y(y_) {}
  V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
  V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
  V2 operator*(float k) const { return {x * k, y * k}; }
  V2& operator+=(V2 o) { x += o.x; y += o.y; return *this; }
  float len() const { return sqrtf(x * x + y * y); }
  D2D1_POINT_2F p() const { return D2D1::Point2F(x, y); }
};
inline V2 lerp(V2 a, V2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }

// Wheel angles: 0 is straight up, positive is clockwise.
inline V2 polar(V2 c, float angle, float r) { return {c.x + sinf(angle) * r, c.y - cosf(angle) * r}; }
inline float angleOf(V2 d) { return atan2f(d.x, -d.y); }

// ---------------------------------------------------------------- colour
struct Color {
  float r = 0, g = 0, b = 0, a = 1;
  constexpr Color() = default;
  constexpr Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
  static constexpr Color hex(uint32_t rgb, float a = 1) {
    return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a};
  }
  static bool parse(std::string_view s, Color& out);
  std::string str() const;
  Color alpha(float k) const { return {r, g, b, a * k}; }
  Color withA(float v) const { return {r, g, b, v}; }
  float luma() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
  D2D1_COLOR_F f() const { return D2D1::ColorF(r, g, b, a); }
  bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};
inline Color mix(Color a, Color b, float t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}
struct Hsv { float h = 0, s = 0, v = 0; };
Hsv toHsv(Color c);
Color fromHsv(Hsv h, float a = 1);

// ---------------------------------------------------------------- springs
// Closed-form damped spring: stable for any dt, so dropped frames never explode.
struct Spring {
  float x = 0, v = 0, target = 0;
  Spring() = default;
  explicit Spring(float start) : x(start), target(start) {}
  void snap(float value) { x = target = value; v = 0; }
  void step(float dt, float freqHz, float damping);
  bool settled(float eps = 0.0015f) const { return fabsf(x - target) < eps && fabsf(v) < eps * 14.f; }
};

// ---------------------------------------------------------------- strings / system
std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view s);
std::wstring toLower(std::wstring s);
std::wstring trim(std::wstring s);
std::wstring fileName(const std::wstring& path);
std::wstring fileStem(const std::wstring& path);
std::wstring fileExt(const std::wstring& path);  // lower case, with dot
std::wstring dirName(const std::wstring& path);
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
std::wstring expandEnv(const std::wstring& s);
bool fileExists(const std::wstring& p);
bool dirExists(const std::wstring& p);
std::wstring modulePath();
std::wstring dataDir();   // %APPDATA%\RadialTaskbar (created on demand)
std::wstring cacheDir();  // %LOCALAPPDATA%\RadialTaskbar, safe to delete at any time
void sweepCache(bool all);  // requests to helpers that were cut short leave small files behind
std::string newId();
double nowSec();
bool readFile(const std::wstring& path, std::string& out, DWORD* error = nullptr);
// Writes beside the target and swaps it in. Retries for about a second, because scanners and our own other
// process hold files open for a moment. keepBackup leaves the previous version as "<path>.bak".
bool writeFileAtomic(const std::wstring& path, const std::string& data, bool keepBackup = false);
HANDLE startHelper(const std::wstring& args, bool quick = false);  // a copy of this program that cannot outlive us
std::string base64(const std::string& bytes);
bool unbase64(const std::string& text, std::string& out);
void logf(const char* fmt, ...);

bool systemUsesLightTheme();
Color systemAccent();
bool systemAnimationsEnabled();
float dpiScaleFor(HWND hwnd);
float dpiScaleFor(HMONITOR mon);

struct RectF {
  float l = 0, t = 0, r = 0, b = 0;
  RectF() = default;
  RectF(float l_, float t_, float r_, float b_) : l(l_), t(t_), r(r_), b(b_) {}
  static RectF xywh(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
  float w() const { return r - l; }
  float h() const { return b - t; }
  float cx() const { return (l + r) * 0.5f; }
  float cy() const { return (t + b) * 0.5f; }
  V2 center() const { return {cx(), cy()}; }
  bool contains(V2 p) const { return p.x >= l && p.x < r && p.y >= t && p.y < b; }
  RectF inset(float d) const { return {l + d, t + d, r - d, b - d}; }
  RectF inset(float dx, float dy) const { return {l + dx, t + dy, r - dx, b - dy}; }
  RectF offset(float dx, float dy) const { return {l + dx, t + dy, r + dx, b + dy}; }
  D2D1_RECT_F f() const { return D2D1::RectF(l, t, r, b); }
};

}  // namespace rt
