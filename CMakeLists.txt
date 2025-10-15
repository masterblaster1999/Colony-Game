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
include(CGCoreTargets)    # colony_core & colony_platform_win (if sources exist)

# Your real game target is typically defined in src/
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

include(CGGameTarget)     # link libs, attach shaders, per-target PCH/defs
include(CGStageInstall)   # stage_win target + installer rules
include(CGTests)          # tests if available
include(CGSummary)        # final pretty summary
