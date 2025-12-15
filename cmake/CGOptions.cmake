# cmake/CGOptions.cmake
#
# Centralizes user-facing cache variables / options for the project.
# (Project defaults like USE_FOLDERS / startup project moved to CGProjectDefaults.cmake)

include_guard(GLOBAL)

# ------------------------------ User-facing options ------------------------------

set(FRONTEND "" CACHE STRING "Frontend: '' (Win32 native) or 'sdl'")
set_property(CACHE FRONTEND PROPERTY STRINGS "" sdl)

option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" ON)
option(ENABLE_TRACY "Enable Tracy instrumentation client" ON)

option(SHOW_CONSOLE        "Show console for the Win32 executable(s)" OFF)
option(COLONY_USE_PCH      "Enable precompiled headers" ON)
option(COLONY_UNITY_BUILD  "Enable CMake Unity builds (jumbo)" OFF)

option(COLONY_ENABLE_COMPUTE_SHADERS
  "Compile compute shaders (*_cs.hlsl) if they define CSMain"
  OFF
)

option(COLONY_BUILD_LAUNCHER "Build native Windows launcher (separate EXE)" ON)

option(COLONY_WERROR "Treat compiler warnings as errors when compiling colony_core" OFF)

set(COLONY_PCH_HEADER "" CACHE STRING "Path to a shared PCH header (relative to repo root)")

# ------------------------------ Windows-only defaults / guards ------------------------------

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
  set(ENABLE_IMGUI OFF CACHE BOOL "Enable Dear ImGui UI (Win32 + D3D11 backends)" FORCE)
  set(ENABLE_TRACY OFF CACHE BOOL "Enable Tracy instrumentation client" FORCE)
  set(COLONY_BUILD_LAUNCHER OFF CACHE BOOL "Build native Windows launcher (separate EXE)" FORCE)
endif()
