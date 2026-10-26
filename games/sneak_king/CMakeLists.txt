# sneak_king - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(sneak_king LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Code the three games share (games/shared/src): achievement notifications, the way back to the launcher, the
# render quality modes, the game window, the FPE guard, the achievement list export.

# Sources
set(SNEAKKING_SOURCES
    src/main.cpp
    ../shared/src/fpe_guard.cpp
    src/port_defaults.cpp
    ../shared/src/achievements_export.cpp
    src/input_options.cpp
    src/frame_stats.cpp
    ../shared/src/achievement_toast.cpp
    ../shared/src/return_to_launcher.cpp
    ../shared/src/render_quality.cpp
    ../shared/src/game_window.cpp
    src/projection_fix.cpp
)

if(WIN32)
    add_executable(sneak_king WIN32 ${SNEAKKING_SOURCES})
else()
    add_executable(sneak_king ${SNEAKKING_SOURCES})
endif()

rexglue_setup_target(sneak_king GPU_PLUGINS xenos)

if(WIN32)
    # UTF-8 as the program's code page, and a linker map for decoding crash reports.
    target_link_options(sneak_king PRIVATE "LINKER:/MANIFEST:EMBED" "LINKER:/MANIFESTUAC:NO"
                        "LINKER:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/sneak_king.manifest"
                        "LINKER:/MAP")
endif()
