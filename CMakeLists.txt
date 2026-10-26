cmake_minimum_required(VERSION 3.20)
# The collection's version (the launcher and the three games are released together): the VERSION file, which
# tools/make_release.ps1 checks against the release it makes.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WHOPPING_RELEASE_VERSION LIMIT_COUNT 1)
project(whopping_launcher VERSION ${WHOPPING_RELEASE_VERSION} LANGUAGES C CXX)

# The Whopping Recompilation Collection: one launcher for the three Burger King
# Xbox 360 recompilations (Sneak King, Big Bumpin', PocketBike Racer).
#
# Built on the TRG Launcher (third_party/trg-launcher): its settings stores,
# Xbox 360 disc installs, downloads and updater. The collection draws its own
# console style dashboard (src/dash.h) instead of the TRG launcher's pages.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The C runtime linked in, so whopping_launcher.exe runs on its own (the release offers the exe by itself too).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(TRG_LAUNCHER_STANDALONE ON CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/trg-launcher)

# stb_image: PNG and JPEG decoding for the game art.
include(FetchContent)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG f58f558c120e9b32c217290b80bad1a0729fbb2c
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(stb)

add_executable(whopping_launcher WIN32
  src/main.cpp
  src/achievements.cpp
  src/app.cpp
  src/art.cpp
  src/art_source.cpp
  src/dash.cpp
  src/disc_finder.cpp
  src/games.cpp
  src/game_settings.cpp
  src/home.cpp
  src/hub.cpp
  src/install.cpp
  src/install_screen.cpp
  src/saves.cpp
  src/projects.cpp
  src/projects_screen.cpp
  src/settings_screen.cpp
  src/sounds.cpp
  src/steam_button.cpp
  src/steam_shortcut.cpp
  src/whopping.rc)
target_include_directories(whopping_launcher PRIVATE src ${stb_SOURCE_DIR})
target_link_libraries(whopping_launcher PRIVATE trg::launcher_standalone)
if(MSVC)
  target_compile_options(whopping_launcher PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(whopping_launcher PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_options(whopping_launcher PRIVATE /MAP /ENTRY:mainCRTStartup)
endif()
# Also the exe's file version (whopping.rc).
target_compile_definitions(whopping_launcher PRIVATE WHOPPING_VERSION="${PROJECT_VERSION}"
  WHOPPING_VERSION_COMMAS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
