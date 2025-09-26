cmake_minimum_required(VERSION 3.26)

# ---------------- Policies & developer UX ----------------
# Honor CMAKE_MSVC_RUNTIME_LIBRARY for MSVC (runtime selection via CMake).  (CMP0091)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# Generate compile_commands.json for better tooling/IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- vcpkg auto-wire (before project()) ----------------
# If no toolchain is explicitly provided, pick up vcpkg from VCPKG_ROOT.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
    message(STATUS "Using vcpkg toolchain at: ${CMAKE_TOOLCHAIN_FILE}")
  endif()
endif()

project(ColonyGame LANGUAGES CXX)

# --- Language standard (project-wide)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------- Options (Windows-first)
option(ENABLE_IMGUI         "Enable Dear ImGui overlay"          ON)
option(ENABLE_TRACY         "Enable Tracy profiler"              ON)
option(TRACY_FETCH          "FetchContent Tracy"                 ON)
option(SHOW_CONSOLE         "Show Console for WIN32 exe"         OFF)
option(BUILD_BOOTSTRAPPER   "Build WinLauncher bootstrapper"     ON)
set(FRONTEND "win32" CACHE STRING "Frontend to build: win32 or sdl")
set_property(CACHE FRONTEND PROPERTY STRINGS win32 sdl)

# Prefer static MSVC runtime with a single CMake knob (default OFF = dynamic CRT)
option(MSVC_STATIC_RUNTIME "Link /MT or /MTd" OFF)

# Optional Link-Time Optimization via CMake IPO (no manual /GL or /LTCG flags)
# Use: -DCOLONY_LTO=ON
option(COLONY_LTO "Enable Link-Time Optimization (IPO) for Release" OFF)
if(COLONY_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(WARNING "IPO/LTO not supported here: ${_ipo_msg}")
  endif()
endif()

# ---------------- Build info (Git hash / timestamp / version) ----------------
# Capture short Git hash; fall back to "unknown" if not a git workspace.
execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE CG_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(CG_GIT_HASH STREQUAL "")
  set(CG_GIT_HASH "unknown")
endif()

string(TIMESTAMP CG_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# Semantic version (bump as needed)
set(CG_VERSION_MAJOR 0)
set(CG_VERSION_MINOR 1)
set(CG_VERSION_PATCH 0)
set(CG_VERSION_STR "${CG_VERSION_MAJOR}.${CG_VERSION_MINOR}.${CG_VERSION_PATCH}")

# Configure generated headers/resources used by targets (if templates exist)
configure_file("${CMAKE_SOURCE_DIR}/src/build_info.h.in"
               "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/platform/win/Version.rc.in"
               "${CMAKE_BINARY_DIR}/generated/Version.rc" @ONLY)

# ------------- Toolchain & compiler flags

# If you keep extra settings in cmake/msvc_flags.cmake they'll still run.
# The block below provides hardened defaults & one-switch CRT selection.
# It runs BEFORE targets are created so the runtime setting propagates.
include(cmake/msvc_flags.cmake OPTIONAL)

if(MSVC)
  # Choose CRT via single toggle; defaults to dynamic CRT (/MD, /MDd)
  if(MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime" FORCE)   # /MT or /MTd
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CACHE STRING "MSVC runtime" FORCE)   # /MD or /MDd
  endif()

  # Warnings & conformance; use multi-proc; correct __cplusplus; safe new
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:throwingNew /EHsc /MP)

  # Full PDBs for better diagnostics instead of FASTLINK
  add_link_options(/DEBUG:FULL)

  # NOTE: We do NOT add manual /GL or /LTCG flags here.
  #       Release LTO is controlled by COLONY_LTO + IPO above.
endif()

# --- Sanitize any leaked generator expressions in global flags
#     (Prevents build tools from treating '$<...>' text like filenames,
#      e.g., '$<1:/O2>' causing C1083.)
foreach(v
  CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
  CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
  if(DEFINED ${v} AND "${${v}}" MATCHES "\\$<")
    string(REGEX REPLACE "\\$<[^>]*>" "" _SANITIZED "${${v}}")
    message(WARNING "${v} contained generator expressions; sanitizing to: '${_SANITIZED}'")
    set(${v} "${_SANITIZED}" CACHE STRING "Sanitized by top-level CMakeLists" FORCE)
  endif()
endforeach()

# ------------- Dependencies (vcpkg-friendly)
include(cmake/deps.cmake)           # SDL2 (if FRONTEND=sdl), imgui wiring, Tracy, etc.

# ------------- Shaders (DXC/FXC fallback)
include(cmake/shaders.cmake)

# ------------- Sources & targets
add_subdirectory(src)

# ------------- Install/Stage
# Prefer your existing staging if present; otherwise add safe defaults here.
include(cmake/install_stage.cmake OPTIONAL)

# Allow disabling these defaults from cache or from your included script.
set(CG_ADD_DEFAULT_INSTALL_RULES ON CACHE BOOL "Add default install/copy rules if not provided elsewhere")

if(CG_ADD_DEFAULT_INSTALL_RULES)
  # If your main executable target is named differently, adjust below.
  if(TARGET ColonyGame)
    # NOTE: The POST_BUILD copy commands were intentionally removed from the
    # top-level file. Define them inside src/CMakeLists.txt where ColonyGame
    # is created, per CMake's requirement for add_custom_command(TARGET ...).

    # Install layout for packaging/zip
    install(TARGETS ColonyGame RUNTIME DESTINATION .)
    if(EXISTS "${CMAKE_SOURCE_DIR}/res")
      install(DIRECTORY res/     DESTINATION res)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
      install(DIRECTORY shaders/ DESTINATION shaders)
    endif()
  else()
    message(STATUS "Target 'ColonyGame' not found at top level; default install rules only.")
  endif()
endif()

# ------------- Testing (robust when tests/ isn't present)
include(CTest)
if(BUILD_TESTING)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
  else()
    message(STATUS "BUILD_TESTING=ON but no 'tests/' directory. Skipping tests.")
  endif()
endif()

# ------------- Packaging (ZIP + MSVC runtime redist)
# Includes VC++ redistributable DLLs when needed and creates a ZIP package target.
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME "ColonyGame")
set(CPACK_PACKAGE_VERSION "${CG_VERSION_STR}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "ColonyGame-${CPACK_PACKAGE_VERSION}-win64")
include(CPack)

# ------------- Helpful tips (fixed bracket block)
include(cmake/tips.cmake OPTIONAL)
