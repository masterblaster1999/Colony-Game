# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.20) # genex in OUTPUT requires 3.20+; VS HLSL props are 3.1+/3.2+

# -------------------------------------------------------------------------------------------------
# Global conveniences
# -------------------------------------------------------------------------------------------------
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(FetchContent)

# Expose FRONTEND selector ("", "sdl")
set(FRONTEND "" CACHE STRING "Frontend: '' (Win32 native) or 'sdl'")

# Windows-only feature toggles (can be overridden by parent/toolchain)
if(WIN32)
  option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" ON)
  option(ENABLE_TRACY "Enable Tracy instrumentation client" ON)
else()
  option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" OFF)
  option(ENABLE_TRACY "Enable Tracy instrumentation client" OFF)
endif()

option(SHOW_CONSOLE "Show console for the Win32 executable" OFF)
option(COLONY_USE_PCH "Enable precompiled headers" ON)
option(COLONY_UNITY_BUILD "Enable CMake Unity builds (jumbo)" OFF)

# Allow specifying a custom PCH header (relative to source root)
set(COLONY_PCH_HEADER "" CACHE STRING "Path to the shared PCH header (relative to source root)")

# -------------------------------------------------------------------------------------------------
# Windows-only global defines (apply to all targets in this dir tree)
# NOTE: We moved WIN32_LEAN_AND_MEAN and NOMINMAX to *per-target* definitions below
#       to avoid accidental redefinitions and improve scoping.
# -------------------------------------------------------------------------------------------------
if(WIN32)
  add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
  )
endif()

# -------------------------------------------------------------------------------------------------
# Third-party via package manager (vcpkg)
# - We try vcpkg/CONFIG packages first. If not present, we add a clean FetchContent fallback below.
# -------------------------------------------------------------------------------------------------
# NOTE (Patch A): imgui/tracy discovery is centralized at the top-level; don't re-find here.
find_package(taskflow CONFIG QUIET)

# -------------------------------------------------------------------------------------------------
# Source discovery (exclude vendored imgui if present)
# -------------------------------------------------------------------------------------------------
file(GLOB_RECURSE SRC_ALL
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c"
  "${CMAKE_SOURCE_DIR}/src/*.h"   "${CMAKE_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_SOURCE_DIR}/platform/win/*.h"
)
list(FILTER SRC_ALL EXCLUDE REGEX ".*/third_party/imgui/.*")

# Remove legacy/duplicate entry points
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/main\\.cpp$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/app/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/render/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/terrain/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/core/.*\\.(c|cc|cxx|cpp)$")

# --- Crash-dump TU: Option 2 (no /EHa) ---
# We keep the normal /EHsc model; __try blocks are isolated into tiny leaf helpers in code.
# Still exclude any stub implementation from the build on Windows.
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/CrashDumpStub\\.cpp$")
  # NOTE: Intentionally no per-file /EHa here (Option 2).
endif()

# --- Frontend gating: native Win32 (default) or SDL ---
if(FRONTEND STREQUAL "sdl")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win32_main\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/launcher/LauncherMain\\.cpp$")
else()
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/Launcher_SDL\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/SDL.*\\.(c|cpp)$")
endif()

# -------------------------------------------------------------------------------------------------
# Library: colony_core (engine + game code)
# -------------------------------------------------------------------------------------------------
add_library(colony_core STATIC ${SRC_ALL})

# >>> PATCH: Scope WIN32_LEAN_AND_MEAN and NOMINMAX to this target <<<
if(WIN32)
  target_compile_definitions(colony_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Use C++23 when available; fall back to C++20 for older CMake toolchains
if(CMAKE_VERSION VERSION_LESS 3.21)
  target_compile_features(colony_core PUBLIC cxx_std_20)
else()
  target_compile_features(colony_core PUBLIC cxx_std_23)
endif()

target_include_directories(colony_core
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"    # expose public headers (e.g., include/rendering/…)
    "${CMAKE_SOURCE_DIR}"            # allow includes like "platform/win/WinApp.h"
    "${CMAKE_BINARY_DIR}/generated"
)

# ---------- Optional PCH ----------
set(_PCH_HEADER "")
if(DEFINED COLONY_PCH_HEADER AND EXISTS "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
  set(_PCH_HEADER "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  set(_PCH_HEADER "${CMAKE_SOURCE_DIR}/src/pch.h")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.hpp")
  set(_PCH_HEADER "${CMAKE_SOURCE_DIR}/src/pch.hpp")
endif()

if(COLONY_USE_PCH AND _PCH_HEADER)
  target_precompile_headers(colony_core PRIVATE "${_PCH_HEADER}")
endif()

# Propagate feature toggles to consumers (exe + tests)
target_compile_definitions(colony_core
  PUBLIC
    $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
    $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
    $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
)

if(MSVC)
  # Stricter conformance and UTF-8 for headers/sources
  target_compile_options(colony_core PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
endif()

# Windows system libs needed by path helpers, COM/KnownFolders, etc. (PUBLIC for tests)
if(WIN32)
  target_link_libraries(colony_core PUBLIC shell32 ole32)
endif()

# -------------------------------------------------------------------------------------------------
# Third-party aggregation + build options
# -------------------------------------------------------------------------------------------------
# Create a harmless fallback interface target if the top-level didn't define one.
if(NOT TARGET colony_build_options)
  add_library(colony_build_options INTERFACE)
endif()

# Expect ${COLONY_THIRDPARTY_LIBS} to be assembled at top-level; ok if empty.
target_link_libraries(colony_core
  PUBLIC
    ${COLONY_THIRDPARTY_LIBS}
    colony_build_options
)

# -------------------------------------------------------------------------------------------------
# Dear ImGui (Win32 + D3D11 backends)
# - Strategy:
#     1) Prefer vcpkg-config targets if present.
#     2) Fallback: FetchContent + build minimal backends into a private static lib.
# -------------------------------------------------------------------------------------------------
if(WIN32 AND ENABLE_IMGUI)
  # If a vcpkg (or parent) package already provided imgui targets, use them.
  set(_imgui_ok FALSE)
  if(TARGET imgui::imgui)
    set(_imgui_ok TRUE)
  endif()

  # Fallback to FetchContent only if we didn't find a proper package
  if(NOT _imgui_ok)
    message(STATUS "[ImGui] vcpkg target not found; using FetchContent fallback.")
    FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.90.5
    )
    FetchContent_MakeAvailable(imgui)

    # Build core + Win32/DX11 backends into a small static lib
    set(IMGUI_SOURCES
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    add_library(imgui_backend STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui_backend PUBLIC
      ${imgui_SOURCE_DIR}
      ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(imgui_backend PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(imgui_backend PUBLIC d3d11 dxgi)
  endif()

  # Link whichever form is available
  if(TARGET imgui::imgui)
    target_link_libraries(colony_core PRIVATE imgui::imgui)
  endif()
  if(TARGET imgui::imgui-win32-binding)
    target_link_libraries(colony_core PRIVATE imgui::imgui-win32-binding)
  elseif(TARGET imgui_backend)
    target_link_libraries(colony_core PRIVATE imgui_backend)
  endif()
  if(TARGET imgui::imgui-dx11-binding)
    target_link_libraries(colony_core PRIVATE imgui::imgui-dx11-binding)
  endif()
endif()

# -------------------------------------------------------------------------------------------------
# Tracy (client)
# - Strategy:
#     1) Prefer an existing packaged target (Tracy::TracyClient).
#     2) Fallback: FetchContent and build public/TracyClient.cpp.
# -------------------------------------------------------------------------------------------------
if(WIN32 AND ENABLE_TRACY)
  if(NOT TARGET Tracy::TracyClient)
    message(STATUS "[Tracy] packaged target not found; using FetchContent fallback.")
    FetchContent_Declare(
      tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG v0.11.0
    )
    FetchContent_MakeAvailable(tracy)

    add_library(tracy_client STATIC
      ${tracy_SOURCE_DIR}/public/TracyClient.cpp
    )
    target_include_directories(tracy_client PUBLIC
      ${tracy_SOURCE_DIR}/public
      ${tracy_SOURCE_DIR}       # allows tracy/Tracy.hpp to include common/TracySystem.hpp
    )
    if(MSVC)
      target_link_libraries(tracy_client PUBLIC ws2_32 dbghelp)
    endif()
    add_library(Tracy::TracyClient ALIAS tracy_client)
  endif()

  target_link_libraries(colony_core PRIVATE Tracy::TracyClient)
endif()

# -------------------------------------------------------------------------------------------------
# Taskflow header-only include path fallback if CONFIG wasn't found
# -------------------------------------------------------------------------------------------------
if(NOT taskflow_FOUND)
  find_path(TASKFLOW_INCLUDE_DIR taskflow/taskflow.hpp)
  if(NOT TASKFLOW_INCLUDE_DIR)
    message(FATAL_ERROR "Taskflow not found. Install via vcpkg (taskflow) or set TASKFLOW_INCLUDE_DIR.")
  endif()
  target_include_directories(colony_core PUBLIC "${TASKFLOW_INCLUDE_DIR}")
endif()

# -------------------------------------------------------------------------------------------------
# Executable: keep the game thin; it links the core lib.
# (The actual entry point(s) are compiled into colony_core by design.)
# -------------------------------------------------------------------------------------------------
add_executable(ColonyGame)

# >>> PATCH: Scope WIN32_LEAN_AND_MEAN and NOMINMAX to this target as well <<<
if(WIN32)
  target_compile_definitions(ColonyGame PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  # Optional: if a separate launcher target exists in your tree.
  if(TARGET colony_launcher)
    target_compile_definitions(colony_launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()
endif()

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

# Use C++23 when available; fall back to C++20
if(CMAKE_VERSION VERSION_LESS 3.21)
  target_compile_features(ColonyGame PRIVATE cxx_std_20)
else()
  target_compile_features(ColonyGame PRIVATE cxx_std_23)
endif()

target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}/generated"
)

if(MSVC)
  target_compile_options(ColonyGame PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
endif()

# Link libs (propagate Windows flags/defs from interface target)
target_link_libraries(ColonyGame
  PRIVATE
    colony_core
    colony_build_options
)

# Clean, readable SDL link gating (Windows-only SDL frontend)
if(FRONTEND STREQUAL "sdl")
  if(TARGET SDL2::SDL2main)
    target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(ColonyGame PRIVATE SDL2::SDL2)
endif()

# Add Windows platform resources and helpers (manifest, version info, crash handler, GPU preference)
if(WIN32)
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()
  foreach(_f IN ITEMS
      "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp"
      "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp"
    )
    if(EXISTS "${_f}")
      target_sources(ColonyGame PRIVATE "${_f}")
    endif()
  endforeach()
  # dbghelp is exe-local; shell32/ole32 are already PUBLIC on colony_core for tests
  target_link_libraries(ColonyGame PRIVATE dbghelp)
endif()

# Make the game the startup project in Visual Studio
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)

# Output directories per configuration (for multi-config generators like VS)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  endforeach()
else()
  # Single-config generators (Ninja, Makefiles)
  set_target_properties(ColonyGame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# Reusable runtime dir (works for single- and multi-config generators)
set(_RUNTIME_DIR "$<TARGET_FILE_DIR:ColonyGame>")

# -------------------------------------------------------------------------------------------------
# POST-BUILD: stage runtime assets next to the ColonyGame.exe
# -------------------------------------------------------------------------------------------------
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${_RUNTIME_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${_RUNTIME_DIR}/res"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets" "${_RUNTIME_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/res"    "${_RUNTIME_DIR}/res"
  VERBATIM
)

# -------------------------------------------------------------------------------------------------
# SHADERS
# -------------------------------------------------------------------------------------------------
# Visual Studio generator path (MSBuild HLSL)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set(HLSL_DIR  "${CMAKE_SOURCE_DIR}/renderer/Shaders")
  set(HLSL_INC_PRIMARY  "${HLSL_DIR}/include")
  set(HLSL_INC_ALT      "${CMAKE_SOURCE_DIR}/shaders/include")
  set(_VS_OUTDIR "$(OutDir)res\\shaders")

  file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
    "${HLSL_DIR}/*_vs.hlsl"
    "${HLSL_DIR}/*_ps.hlsl"
    "${HLSL_DIR}/*_cs.hlsl"
  )

  file(GLOB_RECURSE HLSL_ALL_HLSL CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsl")
  set(HLSL_LIBRARY "")
  foreach(_f IN LISTS HLSL_ALL_HLSL)
    list(FIND HLSL_SOURCES "${_f}" _idx)
    if(_idx EQUAL -1)
      list(APPEND HLSL_LIBRARY "${_f}")
    endif()
  endforeach()

  file(GLOB_RECURSE HLSL_INCLUDES CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsli")

  if(HLSL_SOURCES)
    target_sources(ColonyGame PRIVATE ${HLSL_SOURCES})
  endif()
  if(HLSL_LIBRARY)
    target_sources(ColonyGame PRIVATE ${HLSL_LIBRARY})
    set_source_files_properties(${HLSL_LIBRARY} PROPERTIES HEADER_FILE_ONLY ON)
  endif()
  if(HLSL_INCLUDES)
    target_sources(ColonyGame PRIVATE ${HLSL_INCLUDES})
    set_source_files_properties(${HLSL_INCLUDES} PROPERTIES HEADER_FILE_ONLY ON)
  endif()

  source_group("Shaders\\Sources"  FILES ${HLSL_SOURCES})
  source_group("Shaders\\Library"  FILES ${HLSL_LIBRARY})
  source_group("Shaders\\Includes" FILES ${HLSL_INCLUDES})

  set(_HLSL_INC_FLAGS "")
  if(EXISTS "${HLSL_INC_PRIMARY}")
    string(APPEND _HLSL_INC_FLAGS " /I\"${HLSL_INC_PRIMARY}\"")
  endif()
  if(EXISTS "${HLSL_INC_ALT}")
    string(APPEND _HLSL_INC_FLAGS " /I\"${HLSL_INC_ALT}\"")
  endif()

  function(_configure_vs_shader _file)
    get_filename_component(_we "${_file}" NAME_WE)
    set(_type "")
    set(_entry "")
    if(_we MATCHES "_vs$")
      set(_type "Vertex")
      set(_entry "VSMain")
    elseif(_we MATCHES "_ps$")
      set(_type "Pixel")
      set(_entry "PSMain")
    elseif(_we MATCHES "_cs$")
      set(_type "Compute")
      set(_entry "CSMain")
    else()
      return()
    endif()
    set_source_files_properties("${_file}" PROPERTIES
      VS_SHADER_TYPE              "${_type}"
      VS_SHADER_MODEL             "5.0"
      VS_SHADER_ENTRYPOINT        "${_entry}"
      VS_SHADER_OBJECT_FILE_NAME  "${_VS_OUTDIR}\\%(Filename).cso"
      VS_SHADER_FLAGS             "$<$<CONFIG:Debug>:/Od;/Zi>${_HLSL_INC_FLAGS}"
    )
  endfunction()

  foreach(_sh IN LISTS HLSL_SOURCES)
    _configure_vs_shader("${_sh}")
  endforeach()

else()
  include(${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake OPTIONAL RESULT_VARIABLE _colony_shaders_included)

  set(_COLONY_MANIFEST "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
    set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
    set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
  endif()

  if(_COLONY_MANIFEST AND COMMAND colony_register_shaders)
    set(_COLONY_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
    colony_register_shaders(
      TARGET       ColonyGame
      MANIFEST     "${_COLONY_MANIFEST}"
      OUTPUT_DIR   "${_COLONY_SHADER_OUT}"
      INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/renderer/Shaders"
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/include"
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/include"
      DXC_ARGS     -nologo
    )

    if(COMMAND colony_install_shaders)
      colony_install_shaders(TARGET ColonyGame DESTINATION bin/shaders)
    endif()

    add_custom_command(TARGET ColonyGame POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>"
              "${_RUNTIME_DIR}/res/shaders"
      VERBATIM
    )
  elseif(_COLONY_MANIFEST)
    message(WARNING
      "Found '${_COLONY_MANIFEST}', but 'colony_register_shaders()' is not defined. "
      "Ensure cmake/ColonyShaders.cmake provides it, or build with a Visual Studio generator."
    )
  else()
    message(STATUS "No shaders.json manifest found; DXC shader registration skipped.")
  endif()
endif()

# ---------- Precompiled header for the EXE (same unified header as core) ----------
if(COLONY_USE_PCH AND _PCH_HEADER)
  target_precompile_headers(ColonyGame PRIVATE "${_PCH_HEADER}")
endif()

# Unity builds
if(COLONY_UNITY_BUILD)
  set_target_properties(colony_core PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
  set_target_properties(ColonyGame  PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
endif()

target_compile_definitions(ColonyGame PRIVATE
  $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
)

# Make sure a frequently-included HLSL header is copied alongside compiled shaders for runtime
if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include/common.hlsli")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res/shaders/include"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/renderer/Shaders/include/common.hlsli"
            "${_RUNTIME_DIR}/res/shaders/include/common.hlsli"
    VERBATIM
  )
endif()
