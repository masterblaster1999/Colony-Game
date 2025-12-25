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

# Some helper functions (e.g. colony_enable_warnings()) historically referenced
# COLONY_WARNINGS_AS_ERRORS. Provide a dedicated toggle so the executable and
# other non-core targets can opt into /WX without forcing it on colony_core.
#
# Default: follow COLONY_WERROR if it was enabled.
set(_cg_default_werror_global OFF)
if(DEFINED COLONY_WERROR AND COLONY_WERROR)
  set(_cg_default_werror_global ON)
endif()
option(COLONY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for non-core targets" ${_cg_default_werror_global})
unset(_cg_default_werror_global)

# Optional IPO/LTO (Release + RelWithDebInfo). Off by default because it can
# noticeably increase link times.
option(COLONY_LTO "Enable link-time optimization (IPO/LTO) for Release builds" OFF)
if(COLONY_LTO)
  # CMake uses these variables to default the IPO property by config.
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

# Sanitizer toggles (wired up by toolchain helpers where used). These are kept
# here so CI/devs can pass -DCOLONY_ENABLE_ASAN=ON without having to know which
# helper module owns the option.
option(COLONY_ENABLE_ASAN  "Enable AddressSanitizer (toolchain dependent)" OFF)
option(COLONY_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (Clang only)" OFF)
option(COLONY_ENABLE_TSAN  "Enable ThreadSanitizer (Clang only)" OFF)

# Renderer selection (future-facing; the current prototype build is D3D11).
set(COLONY_RENDERER "d3d11" CACHE STRING "Renderer backend (d3d11 or d3d12)")
set_property(CACHE COLONY_RENDERER PROPERTY STRINGS d3d11 d3d12)

set(COLONY_PCH_HEADER "" CACHE STRING "Path to a shared PCH header (relative to repo root)")

# ------------------------------ Windows-only defaults / guards ------------------------------

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
  set(ENABLE_IMGUI OFF CACHE BOOL "Enable Dear ImGui UI (Win32 + D3D11 backends)" FORCE)
  set(ENABLE_TRACY OFF CACHE BOOL "Enable Tracy instrumentation client" FORCE)
  set(COLONY_BUILD_LAUNCHER OFF CACHE BOOL "Build native Windows launcher (separate EXE)" FORCE)
endif()
