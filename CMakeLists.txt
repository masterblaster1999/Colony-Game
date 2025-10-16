cmake_minimum_required(VERSION 3.27)

# Make project-local modules discoverable
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Policies & vcpkg toolchain must be configured before project()/language enable
include(CGPolicies)
include(CGToolchain)

project(ColonyGame LANGUAGES CXX)

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
