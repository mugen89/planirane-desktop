// Win+Q — the board over a dimmed screen (the user's original request, item 5). Windows keeps Win+Q for Search and does
// not give it to RegisterHotKey, so a low-level keyboard hook takes it over — the same way Radial Taskbar does.
#pragma once
#include <windows.h>

namespace kd {

// Starts the hook on a thread of its own (a busy window can never slow the person's typing). On a plain Win+Q (no Ctrl,
// Alt or Shift) it swallows the Q and posts `msg` to `notify`. False when Windows refused the hook.
bool winqStart(HWND notify, UINT msg);
void winqStop();
bool winqOn();

}  // namespace kd
