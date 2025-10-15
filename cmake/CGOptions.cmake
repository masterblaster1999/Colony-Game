# cmake/CGOptions.cmake
include_guard(GLOBAL)

# Windows-only build guard (after project() so WIN32 is known)
if(NOT WIN32)
  message(FATAL_ERROR "This project is configured for Windows/MSVC only.")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "32-bit builds are not supported. Please build x64.")
endif()

# Language standard (global)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Feature toggles
option(ENABLE_IMGUI        "Enable Dear ImGui overlay"                  ON)
option(ENABLE_TRACY        "Enable Tracy profiler"                      ON)
option(TRACY_FETCH         "Fetch Tracy via FetchContent"               ON)
option(SHOW_CONSOLE        "Show console for WIN32 exe"                 OFF)
option(BUILD_TESTING       "Enable tests"                               ON)
set(FRONTEND "win32" CACHE STRING "Frontend to build: win32 or sdl")
set_property(CACHE FRONTEND PROPERTY STRINGS win32 sdl)

set(COLONY_RENDERER "d3d11" CACHE STRING "Renderer backend: d3d11 or d3d12")
set_property(CACHE COLONY_RENDERER PROPERTY STRINGS d3d11 d3d12)

# HLSL knobs (reporting & defaults)
set(COLONY_HLSL_MODEL "5.0" CACHE STRING "Default HLSL shader model (e.g. 5.0, 5.1, 6.6, 6.7)")
set_property(CACHE COLONY_HLSL_MODEL PROPERTY STRINGS "5.0;5.1;6.0;6.6;6.7")
set(COLONY_HLSL_COMPILER "AUTO" CACHE STRING "HLSL compiler: AUTO | FXC | DXC")
set_property(CACHE COLONY_HLSL_COMPILER PROPERTY STRINGS "AUTO;FXC;DXC")

# Optional build toggles
option(COLONY_USE_PCH      "Enable precompiled headers if a PCH header exists" ON)
option(COLONY_UNITY_BUILD  "Enable Unity (jumbo) builds"                        OFF)
option(COLONY_LTO          "Enable IPO/LTO for Release"                         OFF)
option(COLONY_ASAN         "Enable AddressSanitizer (MSVC, Debug only)"         OFF)
option(COLONY_WARNINGS_AS_ERRORS "Treat warnings as errors"                     OFF)

# CRT selection (CMP0091)
option(COLONY_STATIC_CRT   "Link MSVC runtime statically (/MT,/MTd)" OFF)
if(MSVC)
  if(COLONY_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")          # /MT, /MTd
    set(_crt_kind "static (/MT,/MTd)")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")       # /MD, /MDd
    set(_crt_kind "dynamic (/MD,/MDd)")
  endif()
  message(STATUS "MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}  [${_crt_kind}]")
endif()

# IPO/LTO
if(COLONY_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(WARNING "IPO/LTO not supported here: ${_ipo_msg}")
  endif()
endif()

# MSVC defaults
if(MSVC)
  if(MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "Visual Studio 2022 (v19.30+) required.")
  endif()
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /Zc:throwingNew /Zc:preprocessor /Zc:inline /EHsc)
  add_compile_options(/external:anglebrackets /external:W3)
  if(COLONY_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
  endif()
  add_link_options(/DEBUG:FULL)  # PDBs; exact format driven by CMP0141
endif()

# Small interface lib for ubiquitous Windows macros
add_library(colony_build_options INTERFACE)
target_compile_definitions(colony_build_options INTERFACE
  _USE_MATH_DEFINES XAUDIO2_HELPER_FUNCTIONS=1
  NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
  UNICODE _UNICODE
)

# Flag sanitizer helper (keeps odd genex/flags from leaking)
function(cg_sanitize_flag VAR)
  if(DEFINED ${VAR})
    set(_v "${${VAR}}")
    string(REGEX REPLACE "\\$<[^>]*>" "" _v "${_v}")
    foreach(tok /O2 "/O2" /MP "/MP")
      string(REPLACE "${tok}" "" _v "${_v}")
    endforeach()
    string(REGEX REPLACE "[\"']" "" _v "${_v}")
    string(REGEX REPLACE "[ \t]+" " " _v "${_v}")
    string(STRIP "${_v}" _v)
    if(NOT "${_v}" STREQUAL "${${VAR}}")
      message(STATUS "Sanitized ${VAR}: '${${VAR}}' -> '${_v}'")
      set(${VAR} "${_v}" CACHE STRING "Sanitized by CGOptions" FORCE)
    endif()
  endif()
endfunction()

foreach(v
  CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
  CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
  cg_sanitize_flag(${v})
endforeach()
