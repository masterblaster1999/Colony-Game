cmake_minimum_required(VERSION 3.26)

# ---- Policies / UX ----
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)  # honor CMAKE_MSVC_RUNTIME_LIBRARY
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- vcpkg toolchain (auto) ----
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
  elseif(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
  endif()
endif()

project(ColonyGame LANGUAGES CXX)

# ---- Language standard ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Options (Windows-first) ----
option(ENABLE_IMGUI       "Enable Dear ImGui overlay"          ON)
option(ENABLE_TRACY       "Enable Tracy profiler"              ON)
option(TRACY_FETCH        "Fetch Tracy via FetchContent"       ON)
option(SHOW_CONSOLE       "Show console for WIN32 exe"         OFF)
option(BUILD_TESTING      "Enable tests"                       ON)
set(FRONTEND "win32" CACHE STRING "Frontend to build: win32 or sdl")
set_property(CACHE FRONTEND PROPERTY STRINGS win32 sdl)

# Choose CRT: default dynamic (/MD,/MDd) for parity with vcpkg bin artifacts
option(MSVC_STATIC_RUNTIME "Use static MSVC runtime (/MT,/MTd)" OFF)

# Optional IPO/LTO (use -DCOLONY_LTO=ON)
option(COLONY_LTO "Enable IPO/LTO for Release" OFF)
if(COLONY_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(WARNING "IPO/LTO not supported here: ${_ipo_msg}")
  endif()
endif()

# ---- Build info (optional) ----
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

if(EXISTS "${CMAKE_SOURCE_DIR}/src/build_info.h.in")
  configure_file("${CMAKE_SOURCE_DIR}/src/build_info.h.in"
                 "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Version.rc.in")
  configure_file("${CMAKE_SOURCE_DIR}/platform/win/Version.rc.in"
                 "${CMAKE_BINARY_DIR}/generated/Version.rc" @ONLY)
endif()

# ---- Compiler / linker defaults ----
if(MSVC)
  if(MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)   # /MT or /MTd
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE) # /MD or /MDd
  endif()

  # UTF-8, high warnings, modern conformance.  No /MP here (avoid PCH oddities).
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /Zc:throwingNew /EHsc)
  add_link_options(/DEBUG:FULL)
endif()

# ---- Global compile definitions (Windows-first) ----
#   - XAUDIO2_HELPER_FUNCTIONS + _USE_MATH_DEFINES make XAudio2 helpers & M_PI available project-wide.
#   - NOMINMAX & WIN32_LEAN_AND_MEAN reduce Windows header noise.
#   - _CRT_SECURE_NO_WARNINGS quiets MSVC getenv/_wgetenv warnings (code still may use _dupenv_s at your discretion).
if(WIN32)
  add_compile_definitions(
    _USE_MATH_DEFINES
    XAUDIO2_HELPER_FUNCTIONS=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
  )
endif()

# ---- GLOBAL FLAG SANITIZER (keeps the $<1:/O2 bug away) ----
function(_cg_sanitize VAR)
  if(DEFINED ${VAR})
    set(_v "${${VAR}}")
    string(REGEX REPLACE "\\$<[^>]*>" "" _v "${_v}")  # strip any genex that leaked into flags
    foreach(tok /O2 "/O2" /MP "/MP")
      string(REPLACE "${tok}" "" _v "${_v}")
    endforeach()
    string(REGEX REPLACE "[\"']" "" _v "${_v}")       # drop stray quotes
    string(REGEX REPLACE "[ \t]+" " " _v "${_v}")
    string(STRIP "${_v}" _v)
    if(NOT "${_v}" STREQUAL "${${VAR}}")
      message(WARNING "Sanitized ${VAR}: '${${VAR}}' -> '${_v}'")
      set(${VAR} "${_v}" CACHE STRING "Sanitized by top-level CMakeLists" FORCE)
    endif()
  endif()
endfunction()

foreach(v
  CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
  CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
  _cg_sanitize(${v})
endforeach()

# ====================================================================================
# Third-party deps (vcpkg-friendly): SDL2 (optional), ImGui (optional), Tracy headers
# ====================================================================================

# We do discovery in the top-level so subdirs can rely on imported targets.
# We'll link everything to the main app target after add_subdirectory(src).

set(COLONY_THIRDPARTY_LIBS         "" CACHE INTERNAL "")
set(COLONY_THIRDPARTY_INCLUDE_DIRS "" CACHE INTERNAL "")

# --- SDL2 (only if FRONTEND is sdl) ---
if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)  # vcpkg: provides SDL2::SDL2 and optionally SDL2::SDL2main
  add_compile_definitions(COLONY_WITH_SDL2=1 SDL_MAIN_HANDLED=1)
  list(APPEND COLONY_THIRDPARTY_LIBS
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
  )
endif()

# --- ImGui (optional overlay) ---
if(ENABLE_IMGUI)
  # 1) Prefer a proper package (vcpkg) providing imgui::imgui.
  find_package(imgui CONFIG QUIET)
  if(TARGET imgui::imgui)
    add_compile_definitions(COLONY_WITH_IMGUI=1)
    list(APPEND COLONY_THIRDPARTY_LIBS imgui::imgui)

  # 2) Fallback: build from vendored submodule with Win32 + D3D11 backends.
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/externals/imgui/imgui.cpp")
    add_library(imgui STATIC
      externals/imgui/imgui.cpp
      externals/imgui/imgui_draw.cpp
      externals/imgui/imgui_tables.cpp
      externals/imgui/imgui_widgets.cpp
      # backends for Win32 + D3D11
      externals/imgui/backends/imgui_impl_win32.cpp
      externals/imgui/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC externals/imgui)
    add_compile_definitions(COLONY_WITH_IMGUI=1)
    list(APPEND COLONY_THIRDPARTY_LIBS imgui)
  else()
    message(WARNING "ENABLE_IMGUI=ON but neither imgui::imgui (vcpkg) nor externals/imgui found. UI overlay will be disabled.")
  endif()
endif()

# --- Tracy (headers for profiler UI + optional client lib) ---
if(ENABLE_TRACY)
  add_compile_definitions(TRACY_ENABLE=1)

  # 1) Prefer vcpkg-provided imported target (Tracy::TracyClient).
  find_package(Tracy CONFIG QUIET)
  if(TARGET Tracy::TracyClient)
    list(APPEND COLONY_THIRDPARTY_LIBS Tracy::TracyClient)

  # 2) Fallback: Fetch Tracy sources and build a local client lib.
  elseif(TRACY_FETCH)
    include(FetchContent)
    FetchContent_Declare(tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG v0.11
      GIT_SHALLOW TRUE
    )
    # Only populate (don't add_subdirectory) to avoid pulling in extras we don't need.
    FetchContent_GetProperties(tracy)
    if(NOT tracy_POPULATED)
      FetchContent_Populate(tracy)
    endif()

    # Build the client library from the canonical source file.
    add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
    target_include_directories(tracy_client PUBLIC
      "${tracy_SOURCE_DIR}/public"         # for <tracy/Tracy.hpp>
      "${tracy_SOURCE_DIR}/public/tracy"   # for <Tracy.hpp>
    )
    target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE=1)
    list(APPEND COLONY_THIRDPARTY_LIBS tracy_client)

    # Also expose include dirs globally so any TU can #include <Tracy.hpp>.
    list(APPEND COLONY_THIRDPARTY_INCLUDE_DIRS
      "${tracy_SOURCE_DIR}/public" "${tracy_SOURCE_DIR}/public/tracy")
  else()
    message(WARNING "ENABLE_TRACY=ON but neither Tracy::TracyClient nor TRACY_FETCH available. Profiling will be disabled at link time.")
  endif()
endif()

# --- Propagate include dirs globally if we accumulated any ---
if(COLONY_THIRDPARTY_INCLUDE_DIRS)
  include_directories(${COLONY_THIRDPARTY_INCLUDE_DIRS})
endif()

# ---- HLSL shaders helper (DXC/FXC) ----
include(cmake/shaders.cmake OPTIONAL)

# ===========================
#   Main game sources / target
# ===========================
# Only add the game's 'src' dir if it has its own CMakeLists.txt
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

# Link third-party libs to the main app if the target exists
if(TARGET ColonyGame)
  if(COLONY_THIRDPARTY_LIBS)
    target_link_libraries(ColonyGame PRIVATE ${COLONY_THIRDPARTY_LIBS})
  endif()

  # Frontend toggles
  if(FRONTEND STREQUAL "win32")
    target_compile_definitions(ColonyGame PRIVATE CG_FRONTEND_WIN32=1)
  elseif(FRONTEND STREQUAL "sdl")
    target_compile_definitions(ColonyGame PRIVATE CG_FRONTEND_SDL=1)
  endif()

  # Prefer GUI subsystem on Windows unless SHOW_CONSOLE=ON
  if(WIN32 AND NOT SHOW_CONSOLE)
    set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE ON)
  endif()
endif()

# ===========================
#  Compute-shader erosion demo
# ===========================
option(BUILD_COLONY_COMPUTE_EROSION "Build the compute-shader erosion demo tool" ON)

if(BUILD_COLONY_COMPUTE_EROSION)
  # Try a few common layouts for the demo sources:
  set(_ED_BASE "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/samples/compute_erosion/src/main.cpp")
    set(_ED_BASE "${CMAKE_SOURCE_DIR}/samples/compute_erosion")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/tools/erosion_demo/src/main.cpp")
    set(_ED_BASE "${CMAKE_SOURCE_DIR}/tools/erosion_demo")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/main.cpp"
      AND EXISTS "${CMAKE_SOURCE_DIR}/src/D3DUtils.cpp"
      AND EXISTS "${CMAKE_SOURCE_DIR}/src/PoissonDisk.cpp")
    # Fallback to top-level src/ (use with caution if your game also lives in src/)
    set(_ED_BASE "${CMAKE_SOURCE_DIR}")
  endif()

  if(_ED_BASE STREQUAL "")
    message(STATUS "ColonyComputeErosion sources not found; skipping demo target.")
  else()
    add_executable(ColonyComputeErosion
      "${_ED_BASE}/src/main.cpp"
      "${_ED_BASE}/src/D3DUtils.cpp"
      "${_ED_BASE}/src/PoissonDisk.cpp"
    )
    target_include_directories(ColonyComputeErosion PRIVATE "${_ED_BASE}/src")

    if(MSVC)
      target_link_libraries(ColonyComputeErosion PRIVATE d3d11 d3dcompiler)
    else()
      message(FATAL_ERROR "ColonyComputeErosion requires Windows/MSVC (D3D11).")
    endif()

    # Copy shaders and README next to the binary after build (if present)
    if(EXISTS "${_ED_BASE}/shaders")
      add_custom_command(TARGET ColonyComputeErosion POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyComputeErosion>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_ED_BASE}/shaders"
                "$<TARGET_FILE_DIR:ColonyComputeErosion>/shaders"
      )
    endif()
    if(EXISTS "${_ED_BASE}/README.md")
      add_custom_command(TARGET ColonyComputeErosion POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${_ED_BASE}/README.md"
                "$<TARGET_FILE_DIR:ColonyComputeErosion>/README.md"
      )
    endif()
  endif()
endif()

# ===========================
#        Tests
# ===========================
include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
