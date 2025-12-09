# cmake/CGOptions.cmake
include_guard(GLOBAL)

# Windows-only build guard
if(NOT WIN32)
  message(FATAL_ERROR "This project is configured for Windows/MSVC only.")
endif()

# Disallow 32-bit (make the message single-command and unambiguous)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "32-bit builds are not supported.\nPlease build x64.")
endif()

# Language standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Feature toggles (keep your names)
option(ENABLE_IMGUI "Enable Dear ImGui overlay" ON)
option(ENABLE_TRACY "Enable Tracy profiler" ON)
option(TRACY_FETCH "Fetch Tracy via FetchContent" ON)
option(SHOW_CONSOLE "Show console for WIN32 exe" OFF)
option(BUILD_TESTING "Enable tests" ON)

# Frontend & renderer knobs you already expose
set(FRONTEND "win32" CACHE STRING "Frontend to build: win32 or sdl")
set_property(CACHE FRONTEND PROPERTY STRINGS win32 sdl)
set(COLONY_RENDERER "d3d11" CACHE STRING "Renderer backend: d3d11 or d3d12")
set_property(CACHE COLONY_RENDERER PROPERTY STRINGS d3d11 d3d12)

# HLSL defaults (unchanged)
set(COLONY_HLSL_MODEL "5.0" CACHE STRING "Default HLSL shader model (e.g. 5.0, 5.1, 6.6, 6.7)")
set_property(CACHE COLONY_HLSL_MODEL PROPERTY STRINGS "5.0;5.1;6.0;6.6;6.7")
set(COLONY_HLSL_COMPILER "AUTO" CACHE STRING "HLSL compiler: AUTO | FXC | DXC")
set_property(CACHE COLONY_HLSL_COMPILER PROPERTY STRINGS "AUTO;FXC;DXC")

# Optional build knobs
option(COLONY_USE_PCH "Enable precompiled headers if a PCH header exists" ON)
option(COLONY_UNITY_BUILD "Enable Unity (jumbo) builds" OFF)
option(COLONY_LTO "Enable IPO/LTO for Release" OFF)
option(COLONY_ASAN "Enable AddressSanitizer (MSVC, Debug only)" OFF)
option(COLONY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# CRT selection (CMP0091 must be NEW; see CGPolicies.cmake)
option(COLONY_STATIC_CRT "Link MSVC runtime statically (/MT,/MTd)" OFF)
if(MSVC)
  if(COLONY_STATIC_CRT)
    # /MT (Release) and /MTd (Debug)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    # /MD (Release) and /MDd (Debug)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
  message(STATUS "MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

# IPO/LTO block (unchanged)
# [keep your existing check_ipo_supported / message(...) / endif() nesting here]
