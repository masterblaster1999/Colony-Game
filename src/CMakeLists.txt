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

# ------------------------------ Third-party ------------------------------
find_package(taskflow   CONFIG QUIET)
find_package(directxtex CONFIG REQUIRED) # Microsoft::DirectXTex

# ------------------------------ Modules ------------------------------
include("${COLONY_ROOT_DIR}/cmake/CGSources.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGShadersPipeline.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGRuntimeLayout.cmake")
include("${COLONY_ROOT_DIR}/cmake/CGOptionalDeps.cmake")

# ------------------------------ Sources ------------------------------
cg_collect_core_sources(COLONY_CORE_SOURCES
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
)

# ------------------------------ Core library -----------------------------
if(NOT TARGET colony_core)
  add_library(colony_core STATIC)
endif()
target_sources(colony_core PRIVATE ${COLONY_CORE_SOURCES})

if(COLONY_UNITY_BUILD)
  set_target_properties(colony_core PROPERTIES UNITY_BUILD ON)
else()
  set_target_properties(colony_core PROPERTIES UNITY_BUILD OFF)
endif()

if(WIN32)
  target_compile_definitions(colony_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(CMAKE_VERSION VERSION_LESS 3.21)
  target_compile_features(colony_core PUBLIC cxx_std_20)
else()
  target_compile_features(colony_core PUBLIC cxx_std_23)
endif()

target_include_directories(colony_core
  PUBLIC
    "${COLONY_ROOT_DIR}/src"
    "${COLONY_ROOT_DIR}/include"
    "${COLONY_ROOT_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

# PCH
set(_PCH_HEADER "")
if(DEFINED COLONY_PCH_HEADER AND EXISTS "${COLONY_ROOT_DIR}/${COLONY_PCH_HEADER}")
  set(_PCH_HEADER "${COLONY_ROOT_DIR}/${COLONY_PCH_HEADER}")
elseif(EXISTS "${COLONY_ROOT_DIR}/src/pch.h")
  set(_PCH_HEADER "${COLONY_ROOT_DIR}/src/pch.h")
elseif(EXISTS "${COLONY_ROOT_DIR}/src/pch.hpp")
  set(_PCH_HEADER "${COLONY_ROOT_DIR}/src/pch.hpp")
endif()
if(COLONY_USE_PCH AND _PCH_HEADER)
  target_precompile_headers(colony_core PRIVATE "${_PCH_HEADER}")
endif()
unset(_PCH_HEADER)

# Public feature toggles (note: ImGui/Tracy defines are now set by CGOptionalDeps.cmake)
target_compile_definitions(colony_core
  PUBLIC
    $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
)

if(MSVC)
  target_compile_options(colony_core PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
  if(DEFINED COLONY_WERROR AND COLONY_WERROR)
    target_compile_options(colony_core PRIVATE /WX)
  endif()
endif()

if(WIN32)
  target_link_libraries(colony_core PUBLIC shell32 ole32)
endif()

# Link the aggregated third-party list if provided by your top-level build.
set(_TP_LIBS ${COLONY_THIRDPARTY_LIBS})
if(NOT FRONTEND STREQUAL "sdl")
  if(_TP_LIBS)
    list(FILTER _TP_LIBS EXCLUDE REGEX "SDL2main|SDL2::SDL2main")
  endif()
endif()

if(NOT TARGET colony_build_options)
  add_library(colony_build_options INTERFACE)
endif()
target_link_libraries(colony_core PUBLIC ${_TP_LIBS} colony_build_options Microsoft::DirectXTex)
unset(_TP_LIBS)

# --------------------- Optional deps (ONE LINER) ---------------------
cg_setup_optional_deps(
  CORE_TARGET colony_core
  ENABLE_IMGUI ${ENABLE_IMGUI}
  ENABLE_TRACY ${ENABLE_TRACY}
)

# ----------------------------- Taskflow fallback --------------------------
if(NOT taskflow_FOUND)
  find_path(TASKFLOW_INCLUDE_DIR taskflow/taskflow.hpp)
  if(NOT TASKFLOW_INCLUDE_DIR)
    message(FATAL_ERROR "Taskflow not found. Install via vcpkg (taskflow) or set TASKFLOW_INCLUDE_DIR.")
  endif()
  target_include_directories(colony_core PUBLIC "${TASKFLOW_INCLUDE_DIR}")
endif()

# =============================== Game EXE ===============================
if(NOT TARGET ColonyGame)
  add_executable(ColonyGame)
endif()

if(WIN32)
  target_compile_definitions(ColonyGame PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(NOT SHOW_CONSOLE)
    set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
  endif()
endif()

if(COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_UNIQUE_ID "COLONY")
else()
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD OFF)
endif()

if(CMAKE_VERSION VERSION_LESS 3.21)
  target_compile_features(ColonyGame PRIVATE cxx_std_20)
else()
  target_compile_features(ColonyGame PRIVATE cxx_std_23)
endif()

target_include_directories(ColonyGame PRIVATE
  "${COLONY_ROOT_DIR}/src" "${COLONY_ROOT_DIR}/include" "${COLONY_ROOT_DIR}" "${CMAKE_BINARY_DIR}/generated"
)

if(MSVC)
  target_compile_options(ColonyGame PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
  target_link_options(ColonyGame PRIVATE /WX)
endif()

target_link_libraries(ColonyGame PRIVATE colony_core colony_build_options)

if(TARGET Colony::PlatformWin)
  target_link_libraries(ColonyGame PRIVATE Colony::PlatformWin)
endif()
if(TARGET Colony::WinPlatform)
  target_link_libraries(ColonyGame PRIVATE Colony::WinPlatform)
endif()

cg_add_colonygame_sources(ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
)

if(WIN32)
  target_link_libraries(ColonyGame PRIVATE dbghelp)
endif()

# ============================== Launcher EXE ==============================
if(WIN32 AND COLONY_BUILD_LAUNCHER AND NOT (FRONTEND STREQUAL "sdl"))
  if(NOT TARGET ColonyLauncher)
    add_executable(ColonyLauncher)
  endif()

  target_compile_definitions(ColonyLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(NOT SHOW_CONSOLE)
    set_target_properties(ColonyLauncher PROPERTIES WIN32_EXECUTABLE YES)
  endif()
  if(CMAKE_VERSION VERSION_LESS 3.21)
    target_compile_features(ColonyLauncher PRIVATE cxx_std_20)
  else()
    target_compile_features(ColonyLauncher PRIVATE cxx_std_23)
  endif()
  target_include_directories(ColonyLauncher PRIVATE
    "${COLONY_ROOT_DIR}/src" "${COLONY_ROOT_DIR}/include" "${COLONY_ROOT_DIR}" "${CMAKE_BINARY_DIR}/generated"
  )
  if(MSVC)
    target_compile_options(ColonyLauncher PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
    target_link_options(ColonyLauncher PRIVATE /WX)
  endif()

  target_link_libraries(ColonyLauncher PRIVATE colony_core colony_build_options shell32 ole32)

  if(TARGET Colony::PlatformWin)
    target_link_libraries(ColonyLauncher PRIVATE Colony::PlatformWin)
  endif()
  if(TARGET Colony::WinPlatform)
    target_link_libraries(ColonyLauncher PRIVATE Colony::WinPlatform)
  endif()

  cg_add_colonylauncher_sources(ColonyLauncher
    ROOT_DIR "${COLONY_ROOT_DIR}"
    FRONTEND "${FRONTEND}"
  )

  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
else()
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
endif()

# --------------------- Runtime layout (ONE LINERS) ---------------------
cg_configure_runtime_output(
  TARGETS ColonyGame ColonyLauncher
  BASE_DIR "${CMAKE_BINARY_DIR}/bin"
)

# IMPORTANT: Call content-copy before shader pipeline so shader copies can overwrite res/shaders.
cg_add_post_build_content_copy(
  TARGET ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
)

# ------------------------------- SHADERS (one-liner) -------------------------------
cg_setup_shaders_pipeline(
  TARGET ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
  ENABLE_COMPUTE_SHADERS ${COLONY_ENABLE_COMPUTE_SHADERS}
)

# ------------------------------- Install ----------------------------------
install(TARGETS ColonyGame RUNTIME DESTINATION ".")
if(TARGET ColonyLauncher)
  install(TARGETS ColonyLauncher RUNTIME DESTINATION ".")
endif()

cg_install_runtime_content(ROOT_DIR "${COLONY_ROOT_DIR}")
