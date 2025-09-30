cmake_minimum_required(VERSION 3.26)

# ---- Policies / UX ----
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)  # honor CMAKE_MSVC_RUNTIME_LIBRARY
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /Zc:throwingNew /Zc:preprocessor /EHsc)
  add_link_options(/DEBUG:FULL)
endif()

# ---- Build options interface (centralized Windows macros) ----
#   - Defines are provided via an INTERFACE target and linked to all binaries,
#     avoiding per-file #define spam and macro redefinitions.
if(WIN32)
  add_library(colony_build_options INTERFACE)
  target_compile_definitions(colony_build_options INTERFACE
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
  # Define both macros for compatibility with existing source guards.
  add_compile_definitions(COLONY_WITH_SDL2=1 COLONY_WITH_SDL=1 SDL_MAIN_HANDLED=1)
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
    if(NOT TARGET tracy_client)
      add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
      target_include_directories(tracy_client PUBLIC
        "${tracy_SOURCE_DIR}/public"         # for <tracy/Tracy.hpp>
        "${tracy_SOURCE_DIR}/public/tracy"   # for <Tracy.hpp>
      )
      target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE=1)
    endif()

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
# Keep existing optional include (project-local override)
include(cmake/shaders.cmake OPTIONAL)

# ---- Built-in DXC-based HLSL compilation (SM6+) ----
# If the project didn't define a 'shaders' target in cmake/shaders.cmake, build one here.
option(BUILD_SHADERS "Compile HLSL shaders with DXC" ON)
set(DXC_DEFAULT_SM "6_7" CACHE STRING "Default Shader Model version (e.g. 6_6, 6_7)")
set_property(CACHE DXC_DEFAULT_SM PROPERTY STRINGS 6_6 6_7)

if(BUILD_SHADERS AND NOT TARGET shaders)
  # Locate dxc.exe (can also set -DDXC_EXE=C:/path/to/dxc.exe)
  find_program(DXC_EXE NAMES dxc HINTS
    "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
    "$ENV{WindowsSdkDir}/bin/x64"
    "$ENV{WindowsSdkDir}/bin"
  )
  if(NOT DXC_EXE)
    message(WARNING "DXC not found on PATH or SDK; set DXC_EXE or install the DirectX Shader Compiler. Shaders will not be compiled.")
  endif()

  if(DXC_EXE AND (EXISTS "${CMAKE_SOURCE_DIR}/shaders" OR EXISTS "${CMAKE_SOURCE_DIR}/src/pcg/shaders"))
    # Compile from both the root shaders/ and the PCG shaders/ folders
    file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
      "${CMAKE_SOURCE_DIR}/src/pcg/shaders/*.hlsl"
    )
    set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

    # Helper to infer stage/profile/entry from filename suffix.
    function(_cg_infer_profile_and_entry SRC OUT_PROFILE OUT_ENTRY)
      get_filename_component(_name_we "${SRC}" NAME_WE)
      string(TOLOWER "${_name_we}" _lower)
      # default entry names per stage
      set(_prof "")
      set(_entry "")
      if(_lower MATCHES "(_|\\.)vs$|^vs_")
        set(_prof "vs_${DXC_DEFAULT_SM}")
        set(_entry "VSMain")
      elseif(_lower MATCHES "(_|\\.)ps$|^ps_")
        set(_prof "ps_${DXC_DEFAULT_SM}")
        set(_entry "PSMain")
      elseif(_lower MATCHES "(_|\\.)cs$|^cs_")
        set(_prof "cs_${DXC_DEFAULT_SM}")
        set(_entry "CSMain")
      elseif(_lower MATCHES "(_|\\.)gs$|^gs_")
        set(_prof "gs_${DXC_DEFAULT_SM}")
        set(_entry "GSMain")
      elseif(_lower MATCHES "(_|\\.)hs$|^hs_")
        set(_prof "hs_${DXC_DEFAULT_SM}")
        set(_entry "HSMain")
      elseif(_lower MATCHES "(_|\\.)ds$|^ds_")
        set(_prof "ds_${DXC_DEFAULT_SM}")
        set(_entry "DSMain")
      elseif(_lower MATCHES "(_|\\.)ms$|^ms_")
        set(_prof "ms_${DXC_DEFAULT_SM}")
        set(_entry "MSMain")
      elseif(_lower MATCHES "(_|\\.)as$|^as_")
        set(_prof "as_${DXC_DEFAULT_SM}")
        set(_entry "ASMain")
      endif()
      set(${OUT_PROFILE} "${_prof}" PARENT_SCOPE)
      set(${OUT_ENTRY}   "${_entry}" PARENT_SCOPE)
    endfunction()

    set(_BUILT_CSOS "")
    foreach(SRC IN LISTS HLSL_SOURCES)
      # Determine profile/entry from filename; skip if ambiguous.
      _cg_infer_profile_and_entry("${SRC}" PROFILE ENTRY)
      if(NOT PROFILE)
        message(STATUS "Skipping shader (stage not inferred): ${SRC}  [Use suffix _vs/_ps/_cs/_gs/_hs/_ds/_ms/_as]")
        continue()
      endif()

      get_filename_component(NAME_WE "${SRC}" NAME_WE)

      # Per-file overrides (PCG compute shader uses 'main' entry)
      if("${NAME_WE}" STREQUAL "noise_fbm_cs")
        set(ENTRY "main")
      endif()

      set(OUTFILE "${SHADER_BIN_DIR}/${NAME_WE}.cso")

      # Build command:
      #   dxc -T <profile> -E <entry> -Fo <out> [-Zi -Qembed_debug | -Qstrip_debug] -I shaders <src>
      add_custom_command(
        OUTPUT "${OUTFILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BIN_DIR}"
        COMMAND "${DXC_EXE}" -nologo
                -T ${PROFILE} -E ${ENTRY}
                $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug>
                $<$<NOT:$<CONFIG:Debug>>:-Qstrip_debug>
                -Fo "${OUTFILE}"
                -I "${CMAKE_SOURCE_DIR}/shaders"
                -I "${CMAKE_SOURCE_DIR}/src/pcg/shaders"
                "${SRC}"
        DEPENDS "${SRC}"
        COMMENT "DXC ${PROFILE} ${SRC} -> ${OUTFILE}"
        VERBATIM
      )
      list(APPEND _BUILT_CSOS "${OUTFILE}")
    endforeach()

    if(_BUILT_CSOS)
      add_custom_target(shaders ALL DEPENDS ${_BUILT_CSOS})
      set_property(TARGET shaders PROPERTY FOLDER "assets")
      # Make compiled shaders easy to find from the debugger and install step
    else()
      message(STATUS "No HLSL shaders compiled (no matching *.hlsl with stage suffix).")
    endif()
  endif()
endif()

# ===========================
#   PCG library & demo (NEW)
# ===========================
option(PCG_USE_YAML   "Enable YAML storylets in the pcg library" OFF)
option(BUILD_PCG_DEMO "Build the PCG demo tool"                  ON)

# ---- PCG static library ----
set(PCG_SRC
    src/pcg/SeededRng.cpp
    src/pcg/Noise.cpp
    src/pcg/TerrainGen.cpp
    src/pcg/Rivers.cpp
    src/pcg/Biomes.cpp
    src/pcg/WfcLayout.cpp
    src/pcg/Weather.cpp
    src/pcg/NoiseCS.cpp
)
add_library(pcg STATIC ${PCG_SRC})
target_include_directories(pcg PUBLIC ${CMAKE_SOURCE_DIR}/src)
set_property(TARGET pcg PROPERTY FOLDER "libs")

if(PCG_USE_YAML)
  find_package(yaml-cpp CONFIG REQUIRED)  # vcpkg: yaml-cpp
  target_compile_definitions(pcg PUBLIC PCG_USE_YAML)
  target_link_libraries(pcg PUBLIC yaml-cpp)
  target_sources(pcg PRIVATE src/pcg/Storylets.cpp)
endif()

# ---- PCG demo tool ----
if(BUILD_PCG_DEMO AND EXISTS "${CMAKE_SOURCE_DIR}/src/tools/main_pcg_demo.cpp")
  add_executable(pcg_demo src/tools/main_pcg_demo.cpp)
  target_link_libraries(pcg_demo PRIVATE pcg)
  target_include_directories(pcg_demo PRIVATE ${CMAKE_SOURCE_DIR}/src)
  if(TARGET colony_build_options)
    target_link_libraries(pcg_demo PRIVATE colony_build_options)
  endif()
  if(TARGET shaders)
    add_dependencies(pcg_demo shaders)
  endif()
  # Let the demo find compiled shaders next to the exe (same convention as main app)
  target_compile_definitions(pcg_demo PRIVATE SHADER_DIR=L\"shaders/\")
  if(MSVC)
    set_target_properties(pcg_demo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
  set_property(TARGET pcg_demo PROPERTY FOLDER "tools")
endif()

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

  # Link in the PCG library
  target_link_libraries(ColonyGame PRIVATE pcg)

  # Apply centralized Windows compile definitions to the main target
  if(TARGET colony_build_options)
    target_link_libraries(ColonyGame PRIVATE colony_build_options)
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

  # ---- Integrations and quality-of-life ----
  # Precompiled headers, if present
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/common/pch.hpp")
    target_precompile_headers(ColonyGame PRIVATE src/common/pch.hpp)
  endif()

  # Expose generated build info include dir and version resource if they exist
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/build_info.h")
    target_include_directories(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated")
  endif()
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()

  # Let the game know where to look for runtime-compiled / precompiled shaders
  target_compile_definitions(ColonyGame PRIVATE SHADER_DIR=L\"shaders/\")

  # If a 'shaders' build target exists, build it before the game
  if(TARGET shaders)
    add_dependencies(ColonyGame shaders)
  endif()

  # Make VS debugger start in the source root so relative paths (res/, shaders/) resolve
  if(MSVC)
    set_target_properties(ColonyGame PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endif()

# ===========================
#       Stage (Windows)
# ===========================
# Produces a self-contained staging folder under ${CMAKE_BINARY_DIR}/stage
# with the built game, resources, shaders, and (when available) vcpkg runtime DLLs.
if(WIN32 AND TARGET ColonyGame)
  set(STAGE_DIR "${CMAKE_BINARY_DIR}/stage")

  # Optional resource/shader copy commands (only if source dirs exist)
  set(_STAGE_COPY_RES_CMD "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/res")
    set(_STAGE_COPY_RES_CMD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/res" "${STAGE_DIR}/res")
  endif()

  # Copy precompiled shaders if present; otherwise copy source shaders.
  set(_STAGE_COPY_SHADERS_CMD "")
  if(EXISTS "${CMAKE_BINARY_DIR}/shaders")
    set(_STAGE_COPY_SHADERS_CMD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/shaders" "${STAGE_DIR}/shaders")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
    set(_STAGE_COPY_SHADERS_CMD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/shaders" "${STAGE_DIR}/shaders")
  endif()

  # Try to copy vcpkg runtime DLLs (Release + Debug) if the common locations exist.
  set(_STAGE_COPY_VCPKG_RELEASE_CMD "")
  set(_STAGE_COPY_VCPKG_DEBUG_CMD "")
  if(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
    if(EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/bin")
      set(_STAGE_COPY_VCPKG_RELEASE_CMD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/bin" "${STAGE_DIR}/bin")
    endif()
    if(EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/debug/bin")
      set(_STAGE_COPY_VCPKG_DEBUG_CMD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/debug/bin" "${STAGE_DIR}/bin")
    endif()
  elseif(DEFINED ENV{VCPKG_ROOT})
    if(EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows/bin")
      set(_STAGE_COPY_VCPKG_RELEASE_CMD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "$ENV{VCPKG_ROOT}/installed/x64-windows/bin" "${STAGE_DIR}/bin")
    endif()
    if(EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin")
      set(_STAGE_COPY_VCPKG_DEBUG_CMD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin" "${STAGE_DIR}/bin")
    endif()
  endif()

  add_custom_target(stage_win
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/shaders"
    ${_STAGE_COPY_RES_CMD}
    ${_STAGE_COPY_SHADERS_CMD}
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ColonyGame>" "${STAGE_DIR}/bin/"
    ${_STAGE_COPY_VCPKG_RELEASE_CMD}
    ${_STAGE_COPY_VCPKG_DEBUG_CMD}
    VERBATIM
  )
  add_dependencies(stage_win ColonyGame)
endif()

# ---- Install & CPack (portable ZIP) ----
if(WIN32 AND TARGET ColonyGame)
  install(TARGETS ColonyGame RUNTIME DESTINATION .)
  if(EXISTS "${CMAKE_SOURCE_DIR}/res")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/res" DESTINATION .)
  endif()
  if(EXISTS "${CMAKE_BINARY_DIR}/shaders")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders" DESTINATION .)
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders" DESTINATION .)
  endif()
  set(CPACK_GENERATOR "ZIP")
  include(CPack)
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

    # Apply centralized Windows compile definitions to the demo target
    if(TARGET colony_build_options)
      target_link_libraries(ColonyComputeErosion PRIVATE colony_build_options)
    endif()

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
