# src/CMakeLists.txt

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)
  project(ColonyGame LANGUAGES CXX)
endif()

get_filename_component(COLONY_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# -------------------------------- Options --------------------------------
set(FRONTEND "" CACHE STRING "Frontend: '' (Win32 native) or 'sdl'")

if(WIN32)
  option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" ON)
  option(ENABLE_TRACY "Enable Tracy instrumentation client" ON)
else()
  option(ENABLE_IMGUI OFF)
  option(ENABLE_TRACY OFF)
endif()

option(SHOW_CONSOLE        "Show console for the Win32 executable(s)" OFF)
option(COLONY_USE_PCH      "Enable precompiled headers" ON)
option(COLONY_UNITY_BUILD  "Enable CMake Unity builds (jumbo)" OFF)

option(COLONY_ENABLE_COMPUTE_SHADERS "Compile compute shaders (*_cs.hlsl) if they define CSMain" OFF)
option(COLONY_BUILD_LAUNCHER "Build native Windows launcher (separate EXE)" ON)

set(COLONY_PCH_HEADER "" CACHE STRING "Path to a shared PCH header (relative to source root)")

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()

# ------------------------------ Modules ------------------------------
include("${COLONY_ROOT_DIR}/cmake/CGSources.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGCoreTarget.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGExecutables.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGShadersPipeline.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGRuntimeLayout.cmake")

# ------------------------------ Core sources (ownership rules) ------------------------------
cg_collect_core_sources(COLONY_CORE_SOURCES
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
)

# ------------------------------ Core target (one-liner) ------------------------------
cg_setup_core_target(
  TARGET colony_core
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
  SOURCES ${COLONY_CORE_SOURCES}
  UNITY_BUILD ${COLONY_UNITY_BUILD}
  USE_PCH ${COLONY_USE_PCH}
  PCH_HEADER "${COLONY_PCH_HEADER}"
  ENABLE_IMGUI ${ENABLE_IMGUI}
  ENABLE_TRACY ${ENABLE_TRACY}
)

# ------------------------------ Executables (one-liner) ------------------------------
cg_setup_executables(
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
  SHOW_CONSOLE ${SHOW_CONSOLE}
  UNITY_BUILD ${COLONY_UNITY_BUILD}
  BUILD_LAUNCHER ${COLONY_BUILD_LAUNCHER}
  CORE_TARGET colony_core
  BUILD_OPTIONS_TARGET colony_build_options
  OUT_TARGETS COLONY_EXECUTABLE_TARGETS
)

# ------------------------------ Runtime layout (one-liners) ------------------------------
cg_configure_runtime_output(
  TARGETS ${COLONY_EXECUTABLE_TARGETS}
  BASE_DIR "${CMAKE_BINARY_DIR}/bin"
)

cg_add_post_build_content_copy(
  TARGET ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
)

# ------------------------------ Shaders (one-liner) ------------------------------
cg_setup_shaders_pipeline(
  TARGET ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
  ENABLE_COMPUTE_SHADERS ${COLONY_ENABLE_COMPUTE_SHADERS}
)

# ------------------------------ Install ------------------------------
install(TARGETS ColonyGame RUNTIME DESTINATION ".")
if(TARGET ColonyLauncher)
  install(TARGETS ColonyLauncher RUNTIME DESTINATION ".")
endif()

cg_install_runtime_content(ROOT_DIR "${COLONY_ROOT_DIR}")
