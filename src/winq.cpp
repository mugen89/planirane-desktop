#include "winq.h"

#include <process.h>

#include <atomic>

#include "common.h"

namespace kd {
using namespace rt;  // logf

namespace {

// Our own injected key (see maskWinKey) carries this, and the hook lets it pass.
constexpr ULONG_PTR kMagic = 0x4B445157;  // "KDQW"

struct Hook {
  HWND notify = nullptr;
  UINT msg = 0;
  HANDLE thread = nullptr;
  unsigned threadId = 0;
  std::atomic<bool> on{false};
  std::atomic<bool> down[256] = {};  // as the hook sees the keys: a press vs its auto-repeat
  bool eatUp[256] = {};              // a swallowed press: its release is swallowed too
} g;

bool held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Without this, Windows opens Start when Win is released after a key it never saw: an unassigned key (0xE8) in between
// tells it Win was part of a combination. The well-known trick; the hook lets this key through (kMagic).
void maskWinKey() {
  INPUT in[2]{};
  in[0].type = in[1].type = INPUT_KEYBOARD;
  in[0].ki.wVk = in[1].ki.wVk = 0xE8;
  in[0].ki.dwExtraInfo = in[1].ki.dwExtraInfo = kMagic;
  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

LRESULT CALLBACK hookProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    auto* k = (KBDLLHOOKSTRUCT*)lp;
    if (k->dwExtraInfo != kMagic) {
      bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
      UINT vk = k->vkCode & 0xFF;
      bool first = down && !g.down[vk];
      g.down[vk] = down;
      if (!down && g.eatUp[vk]) {
        g.eatUp[vk] = false;
        return 1;
      }
      bool win = held(VK_LWIN) || held(VK_RWIN);
      if (down && vk == 'Q' && win && !held(VK_CONTROL) && !held(VK_MENU) && !held(VK_SHIFT)) {
        if (first) {  // its auto-repeat opens nothing more
          PostMessageW(g.notify, g.msg, 0, 0);
          maskWinKey();
        }
        g.eatUp[vk] = true;
        return 1;
      }
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

unsigned __stdcall hookThread(void* ready) {
  MSG m;
  PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // the thread's queue exists before the hook
  HHOOK h = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
  if (!h) logf("winq: keyboard hook refused (%lu)", GetLastError());
  else logf("winq: keyboard hook on");
  g.on = h != nullptr;
  SetEvent((HANDLE)ready);
  if (h) {
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    }
    UnhookWindowsHookEx(h);
  }
  g.on = false;
  return 0;
}

}  // namespace

bool winqStart(HWND notify, UINT msg) {
  if (g.thread) return g.on;
  g.notify = notify;
  g.msg = msg;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  g.thread = (HANDLE)_beginthreadex(nullptr, 0, hookThread, ready, 0, &g.threadId);
  if (g.thread) WaitForSingleObject(ready, 3000);
  CloseHandle(ready);
  return g.on;
}

void winqStop() {
  if (!g.thread) return;
  PostThreadMessageW(g.threadId, WM_QUIT, 0, 0);
  WaitForSingleObject(g.thread, 3000);
  CloseHandle(g.thread);
  g.thread = nullptr;
  g.threadId = 0;
  logf("winq: keyboard hook off");
}

bool winqOn() { return g.on; }

}  // namespace kd
