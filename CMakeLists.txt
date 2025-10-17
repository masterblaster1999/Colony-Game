cmake_minimum_required(VERSION 3.27)

# Make project-local modules discoverable
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Policies & vcpkg toolchain must be configured before project()/language enable
include(CGPolicies)
include(CGToolchain)

project(ColonyGame LANGUAGES CXX)

# ------------------------------------------------------------------------------
# Back-compat & preset hygiene
# - Force C++23 to match project policy and avoid mismatches from older presets.
# - Accept older presets that used CG_ENABLE_* and map them to the real options.
#   (Safe to remove this block once all presets are updated.)
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Older presets used CG_ENABLE_PCH / CG_ENABLE_UNITY; real options are
# COLONY_USE_PCH / COLONY_UNITY_BUILD. Map them if present.
if(DEFINED CG_ENABLE_PCH AND NOT DEFINED COLONY_USE_PCH)
  set(COLONY_USE_PCH "${CG_ENABLE_PCH}" CACHE BOOL "Enable precompiled headers" FORCE)
endif()
if(DEFINED CG_ENABLE_UNITY AND NOT DEFINED COLONY_UNITY_BUILD)
  set(COLONY_UNITY_BUILD "${CG_ENABLE_UNITY}" CACHE BOOL "Enable unity (jumbo) builds" FORCE)
endif()

include(CGOptions)        # options, MSVC defaults, helpers
include(CGThirdParty)     # SDL2, ImGui, Tracy, DXC (optional)
include(CGShaders)        # unified HLSL pipeline (VS/MSBuild, FXC, DXC)

# NOTE: Stop defining/creating core/platform targets at the top level.
# Those targets must be defined once inside subdirectories to avoid CMP0002 collisions.
# (Removed: include(CGCoreTargets))

# Your real game target (and libraries) are defined under src/
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

include(CGGameTarget)     # link libs, attach shaders, per-target PCH/defs
include(CGStageInstall)   # stage_win target + installer rules
include(CGTests)          # tests if available
include(CGSummary)        # final pretty summary
