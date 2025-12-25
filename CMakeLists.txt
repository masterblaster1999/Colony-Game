cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

# ==================================================================================================
# Colony-Game (Windows)
# --------------------------------------------------------------------------------------------------
# This is the *top-level* CMakeLists.txt.
#
# Notes:
#   - The repo is currently Windows-only.
#   - Subprojects expect helper functions (colony_*), provided by cmake/ColonyBuildHelpers.cmake.
#   - The main game executable target is: ColonyGame
# ==================================================================================================

if(NOT WIN32)
  message(FATAL_ERROR "Colony-Game is currently Windows-only. Please configure on Windows.")
endif()

# Make our cmake/ folder discoverable for include(<ModuleName>)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Policies must be set *before* the first project()/enable_language() to affect MSVC runtime abstraction.
include(CGPolicies)

project(ColonyGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options / install layout / IDE defaults used across subprojects
include(CGOptions)
include(CGInstallLayout)
include(CGProjectDefaults)
cg_project_defaults_init(PREDEFINED_TARGETS_FOLDER "Colony")

# Helper commands expected by various subdirectories (colony_apply_msvc_runtime, colony_enable_pch, ...)
include(EnableWarningsMSVC)
include(ColonyBuildHelpers)

# Per-target debug runtime checks helper (RTC, etc.)
include(WindowsFlags)

# --------------------------------------------------------------------------------------
# Targets (kept intentionally small and Windows-focused)
# --------------------------------------------------------------------------------------

add_subdirectory(src/core)          # colony::core
add_subdirectory(src/platform/win)  # colony::platform_win
add_subdirectory(pathfinding)       # colony::nav / colony::path (optional, but used by tests)

add_subdirectory(src)               # ColonyGame (+ optional ColonyLauncher)

# Tests
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Staging / packaging helper (copies ColonyGame + assets into <build>/stage)
include(StageWin)

# Optional: print a small config summary at the end of configure
include(CGSummary)
if (COMMAND cg_print_config_summary)
    cg_print_config_summary()
endif()
