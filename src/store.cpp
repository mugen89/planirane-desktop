#include "store.h"

#include <wincrypt.h>

#include "json.h"

namespace kd {
using namespace rt;

namespace {
std::wstring configPath() { return joinPath(dataDir(), L"config.json"); }
std::wstring sessionPath() { return joinPath(dataDir(), L"session.dat"); }
const char* kEntropy = "KanbanDesktop.session.v1";
}  // namespace

void Config::load() {
  std::string text;
  if (!readFile(configPath(), text)) return;
  Json j;
  if (!Json::parse(text, j) || !j.isObj()) return;
  server = widen(j.str("server"));
  username = widen(j.str("username"));
  pill = j.flag("pill", pill);
  winq = j.flag("winq", winq);
  calDue = j.flag("calDue", calDue);
  calStart = j.flag("calStart", calStart);
  calPeriod = j.flag("calPeriod", calPeriod);
  calBands = j.flag("calBands", calBands);
  updateTried = j.str("updateTried");
  updateTriedAt = j.num("updateTriedAt", 0);
}

bool Config::save() const {
  Json j = Json::object();
  j.set("server", narrow(server));
  j.set("username", narrow(username));
  j.set("pill", pill);
  j.set("winq", winq);
  j.set("calDue", calDue);
  j.set("calStart", calStart);
  j.set("calPeriod", calPeriod);
  j.set("calBands", calBands);
  j.set("updateTried", updateTried);
  j.set("updateTriedAt", updateTriedAt);
  return writeFileAtomic(configPath(), j.dump(), true);
}

bool loadSession(std::string& cookie) {
  std::string blob;
  if (!readFile(sessionPath(), blob) || blob.empty()) return false;
  DATA_BLOB in{(DWORD)blob.size(), (BYTE*)blob.data()}, out{}, ent{(DWORD)strlen(kEntropy), (BYTE*)kEntropy};
  if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
  cookie.assign((const char*)out.pbData, out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return !cookie.empty();
}

bool saveSession(const std::string& cookie) {
  DATA_BLOB in{(DWORD)cookie.size(), (BYTE*)cookie.data()}, out{}, ent{(DWORD)strlen(kEntropy), (BYTE*)kEntropy};
  if (!CryptProtectData(&in, L"KanbanDesktop session", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
  std::string blob((const char*)out.pbData, out.cbData);
  LocalFree(out.pbData);
  return writeFileAtomic(sessionPath(), blob, false);
}

void clearSession() { DeleteFileW(sessionPath().c_str()); }

}  // namespace kd
