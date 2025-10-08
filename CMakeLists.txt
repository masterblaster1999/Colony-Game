cmake_minimum_required(VERSION 3.27)

# ---- Policies / UX ----
# Honor MSVC runtime selection and modern option() behavior,
# and make MSVC debug-info format controllable via CMAKE_MSVC_DEBUG_INFORMATION_FORMAT.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)  # honor CMAKE_MSVC_RUNTIME_LIBRARY
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)  # option() honors normal variables
endif()
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)  # MSVC debug info format via property
  # Default to PDBs for Debug/RelWithDebInfo (nice for VS + crash dumps)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

# Prefer config packages (matches vcpkg behavior)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Developer quality-of-life
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_cmake")

# ---- Global feature toggles (opt-in) ----
option(COLONY_USE_PCH     "Enable precompiled headers when a PCH header is present" ON)
option(COLONY_UNITY_BUILD "Enable Unity (jumbo) builds for faster compiles"         OFF)

# ---- vcpkg toolchain (auto) ----
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND EXISTS "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
  elseif(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
  endif()
endif()

# ---- Project (Windows-only configuration) ----
project(ColonyGame LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "This build is configured for Windows/MSVC only.")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "32-bit builds are not supported. Please build x64.")
endif()

# ====================================================================================
# 3.1 — Enforce C++23 globally (applies to ALL targets: app, libs, tests)
# ====================================================================================
set(CMAKE_CXX_STANDARD 23)
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

# ---- Renderer backend (Windows-only) ----
set(COLONY_RENDERER "d3d11" CACHE STRING "Renderer backend: d3d11 or d3d12")
set_property(CACHE COLONY_RENDERER PROPERTY STRINGS d3d11 d3d12)

# -------------------- HLSL reporting knobs (Windows only) --------------------
if(WIN32)
  # Default to D3D11-friendly Shader Model and an automatic compiler choice:
  set(COLONY_HLSL_MODEL "5.0" CACHE STRING "Default HLSL shader model (e.g. 5.0, 5.1, 6.7)")
  set_property(CACHE COLONY_HLSL_MODEL PROPERTY STRINGS "5.0;5.1;6.0;6.6;6.7")

  set(COLONY_HLSL_COMPILER "AUTO" CACHE STRING "HLSL compiler on Windows: AUTO | FXC | DXC")
  set_property(CACHE COLONY_HLSL_COMPILER PROPERTY STRINGS "AUTO;FXC;DXC")

  # Decide the effective path the build will take (for display only).
  set(COLONY_HLSL_SUMMARY "")
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    # Visual Studio generator uses native HLSL integration (VS_SHADER_*).
    set(COLONY_HLSL_SUMMARY "VS native HLSL")
  else()
    # Non‑VS generators: choose between FXC (DXBC) and DXC (DXIL)
    string(TOUPPER "${COLONY_HLSL_COMPILER}" _COLONY_HLSL_COMPILER_UP)
    if(_COLONY_HLSL_COMPILER_UP STREQUAL "DXC" OR COLONY_HLSL_MODEL MATCHES "^6\\.")
      # Prefer vcpkg-provided DXC; fall back to PATH
      if(NOT DEFINED DIRECTX_DXC_TOOL)
        find_program(DIRECTX_DXC_TOOL NAMES dxc)
      endif()
      if(DIRECTX_DXC_TOOL)
        set(COLONY_HLSL_SUMMARY "DXC (DXIL) @ ${DIRECTX_DXC_TOOL}")
      else()
        set(COLONY_HLSL_SUMMARY "DXC (DXIL) @ <not found>")
      endif()
    else()
      # FXC (DXBC) – best match for D3D11 / SM5.x
      find_program(COLONY_FXC_EXE NAMES fxc
        HINTS
          "$ENV{WindowsSdkDir}/bin"
          "C:/Program Files (x86)/Windows Kits/10/bin"
          "C:/Program Files/Windows Kits/10/bin"
        PATH_SUFFIXES x64 x86)
      if(COLONY_FXC_EXE)
        set(COLONY_HLSL_SUMMARY "FXC (DXBC) @ ${COLONY_FXC_EXE}")
      else()
        set(COLONY_HLSL_SUMMARY "FXC (DXBC) @ <not found>")
      endif()
    endif()
  endif()
endif()

# ---- Erosion demo toggle (Windows-only) ----
option(COLONY_BUILD_EROSION_DEMO "Build GPU erosion compute demo (Windows only)" OFF)

# Choose CRT: default dynamic (/MD,/MDd) for parity with vcpkg bin artifacts
option(COLONY_STATIC_CRT "Link the MSVC runtime statically (/MT,/MTd)" OFF)
option(MSVC_STATIC_RUNTIME "DEPRECATED: use COLONY_STATIC_CRT instead" OFF)
if(MSVC_STATIC_RUNTIME)
  set(COLONY_STATIC_CRT ON CACHE BOOL "" FORCE)
endif()

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

# Optional: AddressSanitizer for MSVC Debug builds (-DCOLONY_ASAN=ON)
option(COLONY_ASAN "Enable AddressSanitizer (MSVC) for Debug builds" OFF)

# Optional: turn warnings into errors (handy for CI)
option(COLONY_WARNINGS_AS_ERRORS "Treat all warnings as errors (/WX on MSVC)" OFF)

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
  # Require VS 2022+ toolset (v19.30+) for full C++20/23 library coverage.
  if(MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "MSVC v19.30+ (Visual Studio 2022) required.")
  endif()

  # MSVC runtime selection via CMP0091
  if(COLONY_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # /MT or /MTd
    set(_crt_kind "static (/MT,/MTd)")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL") # /MD or /MDd
    set(_crt_kind "dynamic (/MD,/MDd)")
  endif()
  message(STATUS "MSVC runtime selected: ${CMAKE_MSVC_RUNTIME_LIBRARY}  [${_crt_kind}]")

  # UTF-8, high warnings, modern conformance.
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /Zc:throwingNew /Zc:preprocessor /Zc:inline /EHsc)
  # Reduce noise from external headers (VS 2019 16.6+ / VS 2022)
  add_compile_options(/external:anglebrackets /external:W3)
  if(COLONY_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)  # also affect non-MSVC, if enabled there
  endif()
  # Better debug info by default (PDBs handled by CMP0141 above)
  add_link_options(/DEBUG:FULL)
endif()

# ---- Build options interface (centralized Windows macros) ----
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
      message(STATUS "Sanitized ${VAR}: '${${VAR}}' -> '${_v}'")
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

set(COLONY_THIRDPARTY_LIBS         "" CACHE INTERNAL "")
set(COLONY_THIRDPARTY_INCLUDE_DIRS "" CACHE INTERNAL "")

# --- DirectX Shader Compiler (DXC) via vcpkg (for runtime DXC API usage) ---
# Exposes imported target: Microsoft::DirectXShaderCompiler
find_package(directx-dxc CONFIG REQUIRED)  # requires 'directx-dxc' in your vcpkg.json
list(APPEND COLONY_THIRDPARTY_LIBS Microsoft::DirectXShaderCompiler)  # link later with ColonyGame

# --- SDL2 (only if FRONTEND is sdl) ---
if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)  # vcpkg: SDL2::SDL2 and SDL2::SDL2main
  add_compile_definitions(COLONY_WITH_SDL2=1 COLONY_WITH_SDL=1 SDL_MAIN_HANDLED=1)
  if(TARGET SDL2::SDL2main)
    list(APPEND COLONY_THIRDPARTY_LIBS SDL2::SDL2 SDL2::SDL2main)
  else()
    list(APPEND COLONY_THIRDPARTY_LIBS SDL2::SDL2)
  endif()
endif()

# --- ImGui (optional overlay) ---
if(ENABLE_IMGUI)
  find_package(imgui CONFIG QUIET)
  if(TARGET imgui::imgui)
    add_compile_definitions(COLONY_WITH_IMGUI=1)
    list(APPEND COLONY_THIRDPARTY_LIBS imgui::imgui)
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/externals/imgui/imgui.cpp")
    add_library(imgui STATIC
      externals/imgui/imgui.cpp
      externals/imgui/imgui_draw.cpp
      externals/imgui/imgui_tables.cpp
      externals/imgui/imgui_widgets.cpp
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
  find_package(Tracy CONFIG QUIET)
  if(TARGET Tracy::TracyClient)
    list(APPEND COLONY_THIRDPARTY_LIBS Tracy::TracyClient)
  elseif(TRACY_FETCH)
    include(FetchContent)
    FetchContent_Declare(tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG v0.11
      GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(tracy)
    if(NOT tracy_POPULATED)
      FetchContent_Populate(tracy)
    endif()
    if(NOT TARGET tracy_client)
      add_library(tricy_client_missing_name_fix INTERFACE) # safeguard if old cache kept a stale target name
      add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
      target_include_directories(tracy_client PUBLIC
        "${tracy_SOURCE_DIR}/public"
        "${tracy_SOURCE_DIR}/public/tracy"
      )
      target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE=1)
    endif()
    list(APPEND COLONY_THIRDPARTY_LIBS tracy_client)
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
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(ColonyHLSL OPTIONAL RESULT_VARIABLE _colony_hlsl_loaded)
if(_colony_hlsl_loaded)
  message(STATUS "ColonyHLSL.cmake loaded; Visual Studio HLSL integration available.")
endif()
include(ShaderSetup OPTIONAL RESULT_VARIABLE _shader_setup_loaded)
if(_shader_setup_loaded)
  message(STATUS "ShaderSetup.cmake loaded; colony_compile_hlsl() is available.")
endif()
include(cmake/shaders.cmake OPTIONAL)
# --- Auto HLSL per-file configuration (Visual Studio) ---
include(AutoHlsl OPTIONAL)

# Initialize friendly status for shader route
set(_HLSL_TOOLCHAIN "unknown")

# Prefer using Visual Studio's native HLSL pipeline when ShaderSetup is available
set(CG_HLSL_MSBUILD OFF)
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio" AND _shader_setup_loaded AND COMMAND colony_compile_hlsl)
  set(CG_HLSL_MSBUILD ON)
  set(_HLSL_TOOLCHAIN "Visual Studio HLSL (ShaderSetup.cmake)")
endif()

# ================================
# Explicit shader build (preferred if helper exists)
# ================================
if(NOT CG_HLSL_MSBUILD)
  if(COMMAND cg_compile_hlsl)
    set(COMPILED_SHADERS)
    if(COLONY_RENDERER STREQUAL "d3d12")
      set(_VS_PROF "vs_6_7")
      set(_PS_PROF "ps_6_7")
      set(_CS_PROF "cs_6_7")
    else()
      set(_VS_PROF "vs_5_0")
      set(_PS_PROF "ps_5_0")
      set(_CS_PROF "cs_5_0")
    endif()

    if(COLONY_RENDERER STREQUAL "d3d12")
      set(_SHADER_DIRS "${CMAKE_SOURCE_DIR}/src/pcg/shaders" "${CMAKE_SOURCE_DIR}/shaders")
    else()
      set(_SHADER_DIRS "${CMAKE_SOURCE_DIR}/shaders")
    endif()

    foreach(_dir IN LISTS _SHADER_DIRS)
      if(COLONY_RENDERER STREQUAL "d3d12" AND EXISTS "${_dir}/noise_fbm_cs.hlsl")
        cg_compile_hlsl(NAME noise_fbm_cs
          SRC "${_dir}/noise_fbm_cs.hlsl"
          ENTRY "main"
          PROFILE "${_CS_PROF}"
          INCLUDEDIRS "${_dir}"
          OUTVAR NOISE_FBM_CS_CSO)
        list(APPEND COMPILED_SHADERS "${NOISE_FBM_CS_CSO}")
      endif()

      if(EXISTS "${_dir}/quad_vs.hlsl")
        cg_compile_hlsl(NAME quad_vs
          SRC "${_dir}/quad_vs.hlsl"
          ENTRY "VSMain"
          PROFILE "${_VS_PROF}"
          INCLUDEDIRS "${_dir}"
          OUTVAR QUAD_VS_CSO)
        list(APPEND COMPILED_SHADERS "${QUAD_VS_CSO}")
      endif()

      if(EXISTS "${_dir}/quad_ps.hlsl")
        cg_compile_hlsl(NAME quad_ps
          SRC "${_dir}/quad_ps.hlsl"
          ENTRY "PSMain"
          PROFILE "${_PS_PROF}"
          INCLUDEDIRS "${_dir}"
          OUTVAR QUAD_PS_CSO)
        list(APPEND COMPILED_SHADERS "${QUAD_PS_CSO}")
      endif()
    endforeach()

    if(COMPILED_SHADERS)
      add_custom_target(shaders ALL DEPENDS ${COMPILED_SHADERS})
      set_property(TARGET shaders PROPERTY FOLDER "assets")
      set(_HLSL_TOOLCHAIN "legacy helper cg_compile_hlsl")
    endif()
  endif()
endif()

# ---- Renderer-driven built-in shader compilation routing ----
if(NOT CG_HLSL_MSBUILD)
  if(NOT (COMMAND colony_add_hlsl AND CMAKE_GENERATOR MATCHES "Visual Studio"))

    # D3D11 path -> FXC + Shader Model 5.x (DXBC)
    if(COLONY_RENDERER STREQUAL "d3d11")
      option(BUILD_SHADERS_FXC "Compile HLSL shaders with FXC (SM5) for D3D11" ON)
      set(FXC_DEFAULT_SM "5_0" CACHE STRING "Default Shader Model for FXC (e.g. 5_0, 5_1)")
      set_property(CACHE FXC_DEFAULT_SM PROPERTY STRINGS 5_0 5_1)

      if(BUILD_SHADERS_FXC AND NOT TARGET shaders)
        find_program(FXC_EXE NAMES fxc HINTS
          "$ENV{WindowsSdkDir}/bin/x64"
          "$ENV{WindowsSdkDir}/bin"
        )
        if(NOT FXC_EXE)
          file(GLOB _fxc_cands
            "$ENV{WindowsSdkDir}/bin/*/x64/fxc.exe"
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
            "C:/Program Files/Windows Kits/10/bin/*/x64/fxc.exe"
          )
          if(_fxc_cands)
            list(SORT _fxc_cands DESC)
            list(GET _fxc_cands 0 FXC_EXE)
            message(STATUS "FXC resolved via glob: ${FXC_EXE}")
          endif()
        endif()

        if(NOT FXC_EXE)
          message(WARNING "FXC not found on PATH or Windows SDK; set FXC_EXE or install the Windows SDK. Shaders will not be compiled.")
        endif()

        if(FXC_EXE AND (EXISTS "${CMAKE_SOURCE_DIR}/shaders"))
          file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
          )
          set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/res/shaders")
          file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")
          set(_HLSL_TOOLCHAIN "FXC (SM5) -> ${SHADER_BIN_DIR}")

          function(_cg_infer_profile_and_entry_fxc SRC OUT_PROFILE OUT_ENTRY OUT_SKIP)
            get_filename_component(_name_we "${SRC}" NAME_WE)
            string(TOLOWER "${_name_we}" _lower)
            set(_prof "")
            set(_entry "")
            set(_skip FALSE)
            if(_lower MATCHES "(_|\\.)vs$|^vs_")
              set(_prof "vs_${FXC_DEFAULT_SM}")
              set(_entry "VSMain")
            elseif(_lower MATCHES "(_|\\.)ps$|^ps_")
              set(_prof "ps_${FXC_DEFAULT_SM}")
              set(_entry "PSMain")
            elseif(_lower MATCHES "(_|\\.)cs$|^cs_")
              set(_prof "cs_${FXC_DEFAULT_SM}")
              set(_entry "CSMain")
            elseif(_lower MATCHES "(_|\\.)gs$|^gs_")
              set(_prof "gs_${FXC_DEFAULT_SM}")
              set(_entry "GSMain")
            elseif(_lower MATCHES "(_|\\.)hs$|^hs_")
              set(_prof "hs_${FXC_DEFAULT_SM}")
              set(_entry "HSMain")
            elseif(_lower MATCHES "(_|\\.)ds$|^ds_")
              set(_prof "ds_${FXC_DEFAULT_SM}")
              set(_entry "DSMain")
            elseif(_lower MATCHES "(_|\\.)ms$|^ms_" OR _lower MATCHES "(_|\\.)as$|^as_")
              set(_skip TRUE) # SM6-only
            endif()
            set(${OUT_PROFILE} "${_prof}" PARENT_SCOPE)
            set(${OUT_ENTRY}   "${_entry}" PARENT_SCOPE)
            set(${OUT_SKIP}    "${_skip}" PARENT_SCOPE)
          endfunction()

          set(_BUILT_CSOS "")
          foreach(SRC IN LISTS HLSL_SOURCES)
            _cg_infer_profile_and_entry_fxc("${SRC}" PROFILE ENTRY SKIP_THIS)
            if(SKIP_THIS)
              message(STATUS "Skipping DX12-only shader for D3D11/FXC: ${SRC}")
              continue()
            endif()
            if(NOT PROFILE)
              message(STATUS "Skipping shader (stage not inferred): ${SRC}  [Use suffix _vs/_ps/_cs/_gs/_hs/_ds]")
              continue()
            endif()

            get_filename_component(NAME_WE "${SRC}" NAME_WE)
            if("${NAME_WE}" STREQUAL "noise_fbm_cs")
              set(ENTRY "main")
            endif()

            set(OUTFILE "${SHADER_BIN_DIR}/${NAME_WE}.cso")

            add_custom_command(
              OUTPUT "${OUTFILE}"
              COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BIN_DIR}"
              COMMAND "${FXC_EXE}" /nologo
                      /T ${PROFILE} /E ${ENTRY}
                      $<$<CONFIG:Debug>:/Zi>
                      $<$<CONFIG:Debug>:/Od>
                      /Fo "${OUTFILE}"
                      /I "${CMAKE_SOURCE_DIR}/shaders"
                      /I "${CMAKE_SOURCE_DIR}/src/pcg/shaders"
                      "${SRC}"
              DEPENDS "${SRC}"
              COMMENT "FXC ${PROFILE} ${SRC} -> ${OUTFILE}"
              VERBATIM
            )
            list(APPEND _BUILT_CSOS "${OUTFILE}")
          endforeach()

          if(_BUILT_CSOS)
            # FIXED: make shaders target depend on the actual outputs
            add_custom_target(shaders ALL DEPENDS ${_BUILT_CSOS})
            set_property(TARGET shaders PROPERTY FOLDER "assets")
          else()
            message(STATUS "No HLSL shaders compiled for D3D11 (no matching *.hlsl with stage suffix).")
          endif()
        endif()
      endif()

    # D3D12 path -> DXC + Shader Model 6.x (DXIL)
    elseif(COLONY_RENDERER STREQUAL "d3d12")
      option(BUILD_SHADERS_DXC "Compile HLSL shaders with DXC (SM6) for D3D12" ON)
      set(DXC_DEFAULT_SM "6_7" CACHE STRING "Default Shader Model version (e.g. 6_6, 6_7)")
      set_property(CACHE DXC_DEFAULT_SM PROPERTY STRINGS 6_6 6_7)

      if(BUILD_SHADERS_DXC AND NOT TARGET shaders)
        # Improved DXC discovery: env var, Vulkan SDK, VS locations, Windows SDK
        find_program(DXC_EXE NAMES dxc HINTS
          "$ENV{DXC_PATH}"
          "$ENV{VULKAN_SDK}/Bin"
          "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
          "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
          "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin"
          "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin"
          "$ENV{WindowsSdkDir}/bin/x64"
          "$ENV{WindowsSdkDir}/bin"
        )
        if(NOT DXC_EXE)
          message(WARNING "DXC not found on PATH or SDK; set DXC_EXE or install the DirectX Shader Compiler. Shaders will not be compiled.")
        endif()

        if(DXC_EXE AND (EXISTS "${CMAKE_SOURCE_DIR}/shaders" OR EXISTS "${CMAKE_SOURCE_DIR}/src/pcg/shaders"))
          file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
            "${CMAKE_SOURCE_DIR}/src/pcg/shaders/*.hlsl"
          )
          set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/res/shaders")
          file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")
          set(_HLSL_TOOLCHAIN "DXC (SM6) -> ${SHADER_BIN_DIR}")

          function(_cg_infer_profile_and_entry_dxc SRC OUT_PROFILE OUT_ENTRY)
            get_filename_component(_name_we "${SRC}" NAME_WE)
            string(TOLOWER "${_name_we}" _lower)
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
            _cg_infer_profile_and_entry_dxc("${SRC}" PROFILE ENTRY)
            if(NOT PROFILE)
              message(STATUS "Skipping shader (stage not inferred): ${SRC}  [Use suffix _vs/_ps/_cs/_gs/_hs/_ds/_ms/_as]")
              continue()
            endif()

            get_filename_component(NAME_WE "${SRC}" NAME_WE)
            if("${NAME_WE}" STREQUAL "noise_fbm_cs")
              set(ENTRY "main")
            endif()

            set(OUTFILE "${SHADER_BIN_DIR}/${NAME_WE}.cso")

            add_custom_command(
              OUTPUT "${OUTFILE}"
              COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BIN_DIR}"
              COMMAND "${DXC_EXE}" -nologo
                      -T ${PROFILE} -E ${ENTRY}
                      $<$<CONFIG:Debug>:-Zi>
                      $<$<CONFIG:Debug>:-Qembed_debug>
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
          else()
            message(STATUS "No HLSL shaders compiled for D3D12 (no matching *.hlsl with stage suffix).")
          endif()
        endif()
      endif()
    endif()  # renderer select

  else()
    # VS generator + colony_add_hlsl() will handle compilation via MSBuild.
    set(_HLSL_TOOLCHAIN "Visual Studio HLSL (MSBuild)")
  endif()
endif()

# Friendly summary of which HLSL path we're using (print once)
if(NOT DEFINED CG_HLSL_STATUS_PRINTED)
  set(CG_HLSL_STATUS_PRINTED ON CACHE INTERNAL "Printed HLSL toolchain status once")
  message(STATUS "HLSL toolchain: ${_HLSL_TOOLCHAIN}")
endif()

# ===========================
#   PCG library & demo (NEW)
# ===========================
option(PCG_USE_YAML   "Enable YAML storylets in the pcg library" OFF)
option(BUILD_PCG_DEMO "Build the PCG demo tool"                  ON)

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
  find_package(yaml-cpp CONFIG REQUIRED)
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
  target_compile_definitions(pcg_demo PRIVATE SHADER_DIR=L\"res/shaders/\")
  if(MSVC)
    set_target_properties(pcg_demo PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
  set_property(TARGET pcg_demo PROPERTY FOLDER "tools")
endif()

# ===========================
#   Factions module (NEW)
# ===========================
add_library(Factions STATIC
    src/sim/factions/FactionGen.cpp
)
target_include_directories(Factions PUBLIC ${CMAKE_SOURCE_DIR}/src)
set_property(TARGET Factions PROPERTY FOLDER "libs")

find_package(nlohmann_json CONFIG REQUIRED)
target_sources(Factions PRIVATE src/sim/factions/FactionConfigLoader.hpp)
target_link_libraries(Factions PUBLIC nlohmann_json::nlohmann_json)

# ===========================
#   Main game sources / target
# ===========================
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

if(TARGET ColonyGame)
  if(COLONY_THIRDPARTY_LIBS)
    target_link_libraries(ColonyGame PRIVATE ${COLONY_THIRDPARTY_LIBS})
  endif()

  target_link_libraries(ColonyGame PRIVATE pcg)
  target_link_libraries(ColonyGame PRIVATE Factions)

  if(TARGET colony_build_options)
    target_link_libraries(ColonyGame PRIVATE colony_build_options)
  endif()

  # ---------------------------
  # Option A — add sources directly to your game target (Refactor)
  # ---------------------------
  target_sources(ColonyGame PRIVATE
      src/app/EntryWinMain.cpp
      src/app/App.h
      src/app/App.cpp

      src/core/FixedTimestep.h
      src/core/FixedTimestep.cpp

      src/render/HrCheck.h
      src/render/DeviceD3D11.h
      src/render/DeviceD3D11.cpp
      src/render/Shaders.h
      src/render/Shaders.cpp
      src/render/Textures.h
      src/render/Textures.cpp

      src/terrain/ThermalErosion.h
      src/terrain/ThermalErosion.cpp
  )

  # --- NEW: DXC wrapper + thread-pool/job adapters ---
  target_sources(ColonyGame PRIVATE
      src/engine/Renderer/ShaderCompiler.cpp
      src/common/ThreadPool.hpp
      src/pathfinding/PathJobs.hpp
      src/worldgen/WorldgenJobs.hpp
  )

  # Include root/src (for the headers above)
  target_include_directories(ColonyGame PRIVATE
      ${CMAKE_SOURCE_DIR}/src
  )

  # Link core D3D11/FXC/DXGI libs for the new render/terrain sources
  if(MSVC)
    target_link_libraries(ColonyGame PRIVATE d3d11 dxgi d3dcompiler)
  endif()

  if(FRONTEND STREQUAL "win32")
    target_compile_definitions(ColonyGame PRIVATE CG_FRONTEND_WIN32=1)
  elseif(FRONTEND STREQUAL "sdl")
    target_compile_definitions(ColonyGame PRIVATE CG_FRONTEND_SDL=1)
  endif()

  if(WIN32 AND NOT SHOW_CONSOLE)
    set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE ON)
  endif()

  # Precompiled headers, if present and allowed
  if(COLONY_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/src/common/pch.hpp")
    target_precompile_headers(ColonyGame PRIVATE src/common/pch.hpp)
  endif()

  # Unity build (opt-in)
  if(COLONY_UNITY_BUILD)
    set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
  endif()

  if(EXISTS "${CMAKE_BINARY_DIR}/generated/build_info.h")
    target_include_directories(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated")
  endif()
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()

  target_compile_definitions(ColonyGame PRIVATE SHADER_DIR=L\"res/shaders/\")

  # --- Visual Studio MSBuild HLSL path via ShaderSetup.cmake ---
  if(CG_HLSL_MSBUILD)
    # Project shaders
    if(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
      colony_compile_hlsl(
        TARGET ColonyGame
        DIR    "${CMAKE_SOURCE_DIR}/shaders"
        RECURSE
        MODEL  ${COLONY_HLSL_MODEL}
        OUTPUT_SUBDIR "shaders"
        ENTRYPOINT_MAP "noise_fbm_cs.hlsl=main"
      )
    endif()
    # PCG shaders
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/pcg/shaders")
      colony_compile_hlsl(
        TARGET ColonyGame
        DIR    "${CMAKE_SOURCE_DIR}/src/pcg/shaders"
        RECURSE
        MODEL  ${COLONY_HLSL_MODEL}
        OUTPUT_SUBDIR "shaders"
        ENTRYPOINT_MAP "noise_fbm_cs.hlsl=main"
      )
    endif()

    # Mirror MSBuild's $(OutDir)/shaders into ${CMAKE_BINARY_DIR}/res/shaders for stage/install
    add_custom_command(TARGET ColonyGame POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/res/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:ColonyGame>/shaders" "${CMAKE_BINARY_DIR}/res/shaders"
      COMMENT "Copying compiled shader objects to res/shaders for staging/install."
      VERBATIM
    )
  endif()

  if(TARGET shaders AND NOT CG_HLSL_MSBUILD)
    add_dependencies(ColonyGame shaders)
  endif()

  if(MSVC)
    set_target_properties(ColonyGame PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()

  # ---- AddressSanitizer (Debug only, MSVC) ----
  if(MSVC AND COLONY_ASAN)
    target_compile_options(ColonyGame PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
    target_link_options(ColonyGame PRIVATE   "$<$<CONFIG:Debug>:/fsanitize=address>")
  endif()

  # --- Auto HLSL per-file configuration for Visual Studio (executes after target exists) ---
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT COMMAND colony_add_hlsl AND NOT CG_HLSL_MSBUILD)
    if(COMMAND colony_autoconfigure_hlsl)
      if(NOT DEFINED COLONY_HLSL_MODEL)
        set(COLONY_HLSL_MODEL "5.0")
      endif()
      colony_autoconfigure_hlsl(ColonyGame)
    endif()
  endif()

  # ===========================
  # Visual Studio HLSL integration (only with VS generator)
  # ===========================
  if(COMMAND colony_add_hlsl AND CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT CG_HLSL_MSBUILD)
    message(STATUS "HLSL toolchain: Visual Studio HLSL (MSBuild)")

    set(_SHADERS_DIR "${CMAKE_SOURCE_DIR}/shaders")
    set(_PCG_DIR     "${CMAKE_SOURCE_DIR}/src/pcg/shaders")

    file(GLOB _VS_SHADERS CONFIGURE_DEPENDS "${_SHADERS_DIR}/*_vs.hlsl")
    file(GLOB _PS_SHADERS CONFIGURE_DEPENDS "${_SHADERS_DIR}/*_ps.hlsl")
    file(GLOB _CS_SHADERS CONFIGURE_DEPENDS
         "${_SHADERS_DIR}/*_cs.hlsl"
         "${_PCG_DIR}/*_cs.hlsl")

    if(COLONY_RENDERER STREQUAL "d3d12")
      set(_SM_VS "vs_6_7")
      set(_SM_PS "ps_6_7")
      set(_SM_CS "cs_6_7")
    else()
      set(_SM_VS "vs_5_0")
      set(_SM_PS "ps_5_0")
      set(_SM_CS "cs_5_0")
    endif()

    if(_VS_SHADERS)
      colony_add_hlsl(
        TARGET  ColonyGame
        OUTDIR  "res/shaders"
        SOURCES ${_VS_SHADERS}
        PROFILE ${_SM_VS}
        ENTRY   "VSMain"
        INCLUDES ${_SHADERS_DIR} ${_PCG_DIR}
      )
    endif()

    if(_PS_SHADERS)
      colony_add_hlsl(
        TARGET  ColonyGame
        OUTDIR  "res/shaders"
        SOURCES ${_PS_SHADERS}
        PROFILE ${_SM_PS}
        ENTRY   "PSMain"
        INCLUDES ${_SHADERS_DIR} ${_PCG_DIR}
      )
    endif()

    set(_CS_GENERAL "")
    foreach(_cs ${_CS_SHADERS})
      if(_cs MATCHES ".*/noise_fbm_cs\\.hlsl$")
        set(_HAS_NOISE_FBM_CS TRUE)
      else()
        list(APPEND _CS_GENERAL "${_cs}")
      endif()
    endforeach()

    if(_CS_GENERAL)
      colony_add_hlsl(
        TARGET  ColonyGame
        OUTDIR  "res/shaders"
        SOURCES ${_CS_GENERAL}
        PROFILE ${_SM_CS}
        ENTRY   "CSMain"
        INCLUDES ${_SHADERS_DIR} ${_PCG_DIR}
      )
    endif()

    if(_HAS_NOISE_FBM_CS)
      colony_add_hlsl(
        TARGET  ColonyGame
        OUTDIR  "res/shaders"
        SOURCES "${_PCG_DIR}/noise_fbm_cs.hlsl"
        PROFILE ${_SM_CS}
        ENTRY   "main"
        INCLUDES ${_SHADERS_DIR} ${_PCG_DIR}
      )
    endif()
  endif()
endif()

# ---- Optional: Erosion demo (guarded) --------------------------------------
if (WIN32 AND COLONY_BUILD_EROSION_DEMO)
  if (EXISTS "${CMAKE_SOURCE_DIR}/demos/erosion/CMakeLists.txt")
    message(STATUS "Colony erosion demo: enabled")
    add_subdirectory(demos/erosion)
  else()
    message(WARNING "COLONY_BUILD_EROSION_DEMO=ON but demos/erosion sources are missing; skipping")
  endif()
else()
  message(STATUS "Colony erosion demo: disabled")
endif()

# ===========================
#       Stage (Windows)
# ===========================
if(WIN32 AND TARGET ColonyGame)
  set(STAGE_DIR "${CMAKE_BINARY_DIR}/stage")

  set(_STAGE_COPY_RES_CMD "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/res")
    set(_STAGE_COPY_RES_CMD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/res" "${STAGE_DIR}/res")
  endif()

  set(_STAGE_COPY_SHADERS_CMD "")
  if(EXISTS "${CMAKE_BINARY_DIR}/res/shaders")
    set(_STAGE_COPY_SHADERS_CMD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/res/shaders" "${STAGE_DIR}/shaders")
  elseif(EXISTS "${CMAKE_BINARY_DIR}/shaders")
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
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/res/shaders"
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
  # Bundle the VC++ runtime where appropriate
  include(InstallRequiredSystemLibraries)

  install(TARGETS ColonyGame RUNTIME DESTINATION .)
  if(EXISTS "${CMAKE_SOURCE_DIR}/res")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/res" DESTINATION .)
  endif()
  # Always install into a top-level 'shaders' directory next to the exe.
  if(EXISTS "${CMAKE_BINARY_DIR}/res/shaders")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/res/shaders/" DESTINATION "shaders")
  elseif(EXISTS "${CMAKE_BINARY_DIR}/shaders")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION "shaders")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders/" DESTINATION "shaders")
  endif()

  set(CPACK_GENERATOR "ZIP")
  # Optional: annotate the package with build metadata
  set(CPACK_PACKAGE_FILE_NAME "ColonyGame-${CG_GIT_HASH}")
  include(CPack)
endif()

# ===========================
#        Tests
# ===========================
include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()

# ===========================
#   Configuration Summary
# ===========================
message(STATUS "================ ColonyGame Configuration ================")
message(STATUS "C++ Standard          : C++${CMAKE_CXX_STANDARD}")
if(WIN32)
  message(STATUS "Renderer              : ${COLONY_RENDERER}  [HLSL: ${COLONY_HLSL_SUMMARY}, SM ${COLONY_HLSL_MODEL}]")
else()
  message(STATUS "Renderer              : ${COLONY_RENDERER}")
endif()
message(STATUS "Frontend              : ${FRONTEND}")
message(STATUS "PCH enabled           : ${COLONY_USE_PCH}")
message(STATUS "Unity build           : ${COLONY_UNITY_BUILD}")
message(STATUS "Warnings as errors    : ${COLONY_WARNINGS_AS_ERRORS}")
message(STATUS "LTO/IPO (Release)     : ${CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE}")
message(STATUS "ASan (Debug)          : ${COLONY_ASAN}")
message(STATUS "HLSL toolchain        : ${_HLSL_TOOLCHAIN}")
message(STATUS "Git revision          : ${CG_GIT_HASH}")
message(STATUS "==========================================================")
