cmake_minimum_required(VERSION 3.20)
# The version of a release. The GitHub workflow refuses a tag that does not match it (vX.Y.Z).
project(KanbanDesktop VERSION 0.2.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# windres cannot cope with spaces in -I paths or quoted defines, so these are C++ only.
foreach(def UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00
        NTDDI_VERSION=0x0A00000C KD_VERSION_STR="${PROJECT_VERSION}")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-D${def}>)
endforeach()

# Lean, self-contained binaries: no runtime DLLs to ship, dead code stripped (same recipe as Radial Taskbar).
foreach(opt -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
        -Wno-cast-function-type -Wno-extra -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${opt}>)
endforeach()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(KD_LINK_FLAGS -static -municode -mwindows -Wl,--gc-sections
  -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)

# GCC for Windows links its own default manifest. An empty stand-in found first keeps ours the only one.
set(KD_STUB "${CMAKE_BINARY_DIR}/nomanifest")
if(NOT EXISTS "${KD_STUB}/default-manifest.o")
  file(MAKE_DIRECTORY "${KD_STUB}")
  file(WRITE "${KD_STUB}/empty.cpp" "")
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -c empty.cpp -o default-manifest.o WORKING_DIRECTORY "${KD_STUB}")
endif()
list(APPEND KD_LINK_FLAGS "-Bnomanifest/")

set(KD_LIBS d3d11 dxgi d2d1 dwrite dcomp dwmapi windowscodecs shell32 shlwapi
  propsys ole32 oleaut32 uuid dxguid gdi32 user32 advapi32 winmm comctl32
  comdlg32 shcore uxtheme version psapi winhttp crypt32 wtsapi32 bcrypt)

# Resource scripts are generated with this version and absolute paths (windres cannot take -I with spaces).
set(KD_RES_DIR "${CMAKE_SOURCE_DIR}/res")
set(KD_APP_EXE "${CMAKE_BINARY_DIR}/KanbanDesktop.exe")
string(REPLACE "." "," KD_VER_COMMA "${PROJECT_VERSION}.0")
configure_file(res/app.rc.in "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
configure_file(setup/setup.rc.in "${CMAKE_BINARY_DIR}/setup.rc" @ONLY)

file(GLOB KD_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(KanbanDesktop ${KD_SOURCES} "${CMAKE_BINARY_DIR}/app.rc")
set_source_files_properties("${CMAKE_BINARY_DIR}/app.rc" PROPERTIES OBJECT_DEPENDS "${KD_RES_DIR}/app.ico;${KD_RES_DIR}/app.manifest")
target_link_options(KanbanDesktop PRIVATE ${KD_LINK_FLAGS} $<$<CONFIG:Release>:-s>)
target_link_libraries(KanbanDesktop PRIVATE ${KD_LIBS})

# The setup embeds the finished program, so it is built after it. Installed, it is also Uninstall.exe.
add_executable(KanbanDesktopSetup setup/setup_main.cpp src/common.cpp src/gfx.cpp src/ui.cpp src/install.cpp "${CMAKE_BINARY_DIR}/setup.rc")
set_source_files_properties("${CMAKE_BINARY_DIR}/setup.rc" PROPERTIES OBJECT_DEPENDS "${KD_APP_EXE};${KD_RES_DIR}/app.ico;${KD_RES_DIR}/app.manifest")
add_dependencies(KanbanDesktopSetup KanbanDesktop)
target_include_directories(KanbanDesktopSetup PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src>)
target_link_options(KanbanDesktopSetup PRIVATE ${KD_LINK_FLAGS} $<$<CONFIG:Release>:-s>)
target_link_libraries(KanbanDesktopSetup PRIVATE ${KD_LIBS})
set_target_properties(KanbanDesktopSetup PROPERTIES OUTPUT_NAME "KanbanDesktop-Setup")

# End-to-end API test (development servers only): cmake -S . -B build -DKD_TESTS=ON
option(KD_TESTS "Build the API test" OFF)
if(KD_TESTS)
  add_executable(test_api tests/test_api.cpp src/common.cpp src/json.cpp src/net.cpp src/api.cpp)
  target_include_directories(test_api PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src>)
  target_link_options(test_api PRIVATE -static)
  target_link_libraries(test_api PRIVATE ${KD_LIBS})
endif()
