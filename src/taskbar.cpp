#include "taskbar.h"

#include <uiautomation.h>

namespace kd {
using namespace rt;

namespace {
constexpr const wchar_t* kAdvanced = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

bool validInside(const RECT& r, const RECT& outer) {
  return r.right > r.left && r.bottom > r.top && r.left >= outer.left - 2 && r.right <= outer.right + 2;
}
}  // namespace

TaskbarInfo queryTaskbar() {
  TaskbarInfo t;
  t.hwnd = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!t.hwnd || !GetWindowRect(t.hwnd, &t.bar)) return t;
  t.mon = MonitorFromWindow(t.hwnd, MONITOR_DEFAULTTOPRIMARY);
  t.scale = dpiScaleFor(t.mon);

  APPBARDATA abd{};
  abd.cbSize = sizeof abd;
  abd.hWnd = t.hwnd;
  if (SHAppBarMessage(ABM_GETTASKBARPOS, &abd)) t.atBottom = abd.uEdge == ABE_BOTTOM;

  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(t.mon, &mi);
  RECT vis{};
  IntersectRect(&vis, &t.bar, &mi.rcMonitor);
  t.slidAway = (vis.bottom - vis.top) < (t.bar.bottom - t.bar.top) / 2;

  HWND tray = FindWindowExW(t.hwnd, nullptr, L"TrayNotifyWnd", nullptr);
  if (tray && GetWindowRect(tray, &t.tray) && validInside(t.tray, t.bar)) {
    // ok
  } else {
    // Unknown layout: assume the usual width of the clock and a few icons.
    t.tray = t.bar;
    t.tray.left = t.bar.right - (LONG)(260 * t.scale);
  }
  HWND rebar = FindWindowExW(t.hwnd, nullptr, L"ReBarWindow32", nullptr);
  if (!rebar || !GetWindowRect(rebar, &t.tasks) || !validInside(t.tasks, t.bar)) {
    t.tasks = t.bar;
    t.tasks.right = t.tasks.left;
  }
  t.ok = true;
  return t;
}

bool taskbarIsLight() {
  DWORD v = 0, size = sizeof v;
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
                   RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
    return false;  // Windows 11's default taskbar is dark
  return v != 0;
}

bool fullscreenAppActive() {
  // Windows' own answer — the one that holds back notifications: a full-screen game, video or presentation,
  // or nobody there (locked, screen saver). A home-made "a window covers the monitor" test hid the button
  // at moments Windows did not count as full-screen (seen 2026-11-26), so only Windows' answer is used.
  QUERY_USER_NOTIFICATION_STATE st{};
  if (FAILED(SHQueryUserNotificationState(&st))) return false;
  return st == QUNS_NOT_PRESENT || st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE;
}

bool widgetsButtonShown() {
  DWORD v = 1, size = sizeof v;
  if (RegGetValueW(HKEY_CURRENT_USER, kAdvanced, L"TaskbarDa", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return true;
  return v != 0;
}

namespace {
// One automation client for the life of the process.
IUIAutomation* automation() {
  static ComPtr<IUIAutomation> uia;
  static const GUID kClsid = {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};  // CUIAutomation
  static const GUID kIid = {0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};    // IUIAutomation
  if (!uia) CoCreateInstance(kClsid, nullptr, CLSCTX_INPROC_SERVER, kIid, (void**)uia.GetAddressOf());
  return uia.Get();
}
ComPtr<IUIAutomationCondition> stringCondition(PROPERTYID prop, const wchar_t* value) {
  ComPtr<IUIAutomationCondition> c;
  if (!automation()) return c;
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_BSTR;
  v.bstrVal = SysAllocString(value);
  automation()->CreatePropertyCondition(prop, v, &c);
  VariantClear(&v);
  return c;
}
ComPtr<IUIAutomationElement> taskbarElement() {
  ComPtr<IUIAutomationElement> root;
  HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (tb && automation()) automation()->ElementFromHandle(tb, &root);
  return root;
}
}  // namespace

bool widgetsButtonRect(RECT& out) {
  static ComPtr<IUIAutomationCondition> byId = stringCondition(UIA_AutomationIdPropertyId, L"WidgetsButton");
  ComPtr<IUIAutomationElement> root = taskbarElement(), button;
  if (!root || !byId || FAILED(root->FindFirst(TreeScope_Descendants, byId.Get(), &button)) || !button) return false;
  RECT r{};
  if (FAILED(button->get_CurrentBoundingRectangle(&r)) || r.right <= r.left) return false;
  out = r;
  return true;
}

LONG appButtonsRight() {
  static ComPtr<IUIAutomationCondition> byClass = stringCondition(UIA_ClassNamePropertyId, L"Taskbar.TaskListButtonAutomationPeer");
  ComPtr<IUIAutomationElement> root = taskbarElement();
  ComPtr<IUIAutomationElementArray> all;
  if (!root || !byClass || FAILED(root->FindAll(TreeScope_Descendants, byClass.Get(), &all)) || !all) return 0;
  int n = 0;
  all->get_Length(&n);
  LONG right = 0;
  for (int i = 0; i < n; i++) {
    ComPtr<IUIAutomationElement> e;
    RECT r{};
    if (SUCCEEDED(all->GetElement(i, &e)) && e && SUCCEEDED(e->get_CurrentBoundingRectangle(&r))) right = std::max(right, r.right);
  }
  return right;
}

}  // namespace kd
