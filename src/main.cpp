#include "app.h"
#include "gfx.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  // Only ever load system DLLs from System32, never from the folder we are run from.
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
  OleInitialize(nullptr);
  int rc = 0;
  {
    kd::App app;
    rc = app.run(inst);
  }
  rt::gfx().shutdown();
  OleUninitialize();
  return rc;
}
