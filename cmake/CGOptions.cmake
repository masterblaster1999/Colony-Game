# cmake/CGOptions.cmake
#
# Centralizes user-facing cache variables / options for the project.
# This file is Windows-aware, but safe to include on other platforms.
#
# Uses include_guard() so multiple includes do not re-run option definitions. :contentReference[oaicite:3]{index=3}

include_guard(GLOBAL)

# VS folder organization (keeps Solution Explorer tidy)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------ User-facing options ------------------------------

# Frontend selector (matches existing behavior: '' (native Win32) or 'sdl')
set(FRONTEND "" CACHE STRING "Frontend: '' (Win32 native) or 'sdl'")
set_property(CACHE FRONTEND PROPERTY STRINGS "" sdl)

# Optional UI / instrumentation
option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" ON)   # :contentReference[oaicite:4]{index=4}
option(ENABLE_TRACY "Enable Tracy instrumentation client" ON)

# Build toggles
option(SHOW_CONSOLE        "Show console for the Win32 executable(s)" OFF)
option(COLONY_USE_PCH      "Enable precompiled headers" ON)
option(COLONY_UNITY_BUILD  "Enable CMake Unity builds (jumbo)" OFF)

# Gate compute shaders to avoid FXC X3501 until all *_cs.hlsl have CSMain
option(COLONY_ENABLE_COMPUTE_SHADERS
  "Compile compute shaders (*_cs.hlsl) if they define CSMain"
  OFF
)

# Separate Windows launcher EXE that spawns the game
option(COLONY_BUILD_LAUNCHER "Build native Windows launcher (separate EXE)" ON)

# Optional: compile warnings-as-errors for colony_core (EXE link /WX is handled elsewhere)
option(COLONY_WERROR "Treat compiler warnings as errors when compiling colony_core" OFF)

# Optional override for shared PCH header (relative to repo root, unless absolute path)
set(COLONY_PCH_HEADER "" CACHE STRING "Path to a shared PCH header (relative to repo root)")

# ------------------------------ Windows-only defaults / guards ------------------------------

if(WIN32)
  # Global compile defines used throughout the project
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
  # If somebody configures outside Windows, force Windows-only options off to avoid surprises.
  set(ENABLE_IMGUI OFF CACHE BOOL "Enable Dear ImGui UI (Win32 + D3D11 backends)" FORCE)
  set(ENABLE_TRACY OFF CACHE BOOL "Enable Tracy instrumentation client" FORCE)
  set(COLONY_BUILD_LAUNCHER OFF CACHE BOOL "Build native Windows launcher (separate EXE)" FORCE)
endif()
