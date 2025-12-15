# src/CMakeLists.txt

# When this directory is configured as a *standalone* project, set the CMake policy
# version here. When included from the repository root (add_subdirectory(src)),
# do NOT call cmake_minimum_required() to avoid resetting policy behavior inside
# this subdirectory.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)
  project(ColonyGame LANGUAGES CXX)
endif()

# Repo root (works both from root-add_subdirectory and standalone -S src)
get_filename_component(COLONY_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(FetchContent)

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

# Gate compute shaders to avoid FXC X3501 until all *_cs.hlsl have CSMain
option(COLONY_ENABLE_COMPUTE_SHADERS "Compile compute shaders (*_cs.hlsl) if they define CSMain" OFF)

# Option B: build a separate launcher EXE that can spawn the game EXE
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

# Explicitly control unity (jumbo) builds to avoid accidental ODR/duplicate merges.
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

# Public feature toggles for consumers
target_compile_definitions(colony_core
  PUBLIC
    $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
    $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
    $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
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

# ----------------------- Patch C: filter SDL2main when not using SDL -----------------------
set(_TP_LIBS ${COLONY_THIRDPARTY_LIBS})
if(NOT FRONTEND STREQUAL "sdl")
  if(_TP_LIBS)
    list(FILTER _TP_LIBS EXCLUDE REGEX "SDL2main|SDL2::SDL2main")
  endif()
endif()

# Optional aggregate build options + DirectXTex
if(NOT TARGET colony_build_options)
  add_library(colony_build_options INTERFACE)
endif()
target_link_libraries(colony_core PUBLIC ${_TP_LIBS} colony_build_options Microsoft::DirectXTex)
unset(_TP_LIBS)

# ------------------------------- ImGui (opt) ------------------------------
if(WIN32 AND ENABLE_IMGUI)
  set(_imgui_ok FALSE)
  if(TARGET imgui::imgui)
    set(_imgui_ok TRUE)
  endif()
  if(NOT _imgui_ok)
    message(STATUS "[ImGui] vcpkg target not found; using FetchContent fallback.")
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.90.5)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui_backend STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui_backend PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui_backend PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(imgui_backend PUBLIC d3d11 dxgi)
  endif()
  if(TARGET imgui::imgui)
    target_link_libraries(colony_core PRIVATE imgui::imgui)
  elseif(TARGET imgui_backend)
    target_link_libraries(colony_core PRIVATE imgui_backend)
  endif()
  unset(_imgui_ok)
endif()

# ------------------------------- Tracy (opt) ------------------------------
if(WIN32 AND ENABLE_TRACY)
  if(NOT TARGET Tracy::TracyClient)
    message(STATUS "[Tracy] packaged target not found; using FetchContent fallback.")
    FetchContent_Declare(tracy GIT_REPOSITORY https://github.com/wolfpld/tracy.git GIT_TAG v0.11.0)
    FetchContent_MakeAvailable(tracy)
    add_library(tracy_client STATIC ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
    target_include_directories(tracy_client PUBLIC ${tracy_SOURCE_DIR}/public ${tracy_SOURCE_DIR})
    if(MSVC)
      target_link_libraries(tracy_client PUBLIC ws2_32 dbghelp)
    endif()
    add_library(Tracy::TracyClient ALIAS tracy_client)
  endif()
  target_link_libraries(colony_core PRIVATE Tracy::TracyClient)
endif()

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

# Link single-owner Windows platform libs only at the EXE (if present)
if(TARGET Colony::PlatformWin)
  target_link_libraries(ColonyGame PRIVATE Colony::PlatformWin)
endif()
if(TARGET Colony::WinPlatform)
  target_link_libraries(ColonyGame PRIVATE Colony::WinPlatform)
endif()

# Add "EXE-owned" sources (entrypoints, crash, gpu exports, bootstrap, etc.)
cg_add_colonygame_sources(ColonyGame
  ROOT_DIR "${COLONY_ROOT_DIR}"
  FRONTEND "${FRONTEND}"
)

# Link DbgHelp for MiniDumpWriteDump (in EXE only)
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

# Output (multi/single config)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
    if(TARGET ColonyLauncher)
      set_target_properties(ColonyLauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
    endif()
  endforeach()
else()
  set_target_properties(ColonyGame PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  if(TARGET ColonyLauncher)
    set_target_properties(ColonyLauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  endif()
endif()

# Runtime dir variable
set(_RUNTIME_DIR "$<TARGET_FILE_DIR:ColonyGame>")

# ------------------------------ Post-build copy ---------------------------
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${COLONY_ROOT_DIR}/assets" "${_RUNTIME_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${COLONY_ROOT_DIR}/res"    "${_RUNTIME_DIR}/res"
  VERBATIM
)

set(CONTENT_DIR "${COLONY_ROOT_DIR}/Content")
if(EXISTS "${CONTENT_DIR}")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/Content"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CONTENT_DIR}" "$<TARGET_FILE_DIR:ColonyGame>/Content"
    COMMENT "Copy Content/ next to the EXE for streaming"
    VERBATIM
  )
endif()

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
install(DIRECTORY "${COLONY_ROOT_DIR}/assets/"  DESTINATION "assets")
install(DIRECTORY "${COLONY_ROOT_DIR}/res/"     DESTINATION "res")
install(DIRECTORY "${COLONY_ROOT_DIR}/Content/" DESTINATION "Content")
