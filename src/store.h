// What survives a restart: %APPDATA%\KanbanDesktop\config.json (plain settings) and session.dat
// (the session cookie, encrypted for this Windows user with DPAPI). The password is never stored —
// the same rule as the browser's „Запомни ме": the server issues a 90-day sliding session instead.
#pragma once
#include "common.h"

namespace kd {

struct Config {
  std::wstring server;            // "http://host:8106"
  std::wstring username;          // prefilled at the next sign-in
  bool pill = true;               // the taskbar button
  std::string updateTried;        // the last version an automatic update was attempted for…
  double updateTriedAt = 0;       // …and when (unix seconds): one attempt a day, so a broken release cannot loop

  void load();
  bool save() const;
};

bool loadSession(std::string& cookie);
bool saveSession(const std::string& cookie);
void clearSession();

}  // namespace kd
