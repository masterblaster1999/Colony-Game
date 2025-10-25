# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.20) # genex in OUTPUT requires 3.20+; VS HLSL props are 3.1+/3.2+

# ---- Windows-only global defines (set once, apply to all targets in this dir tree) ----
if(WIN32)
  add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )
endif()

# ---- Third-party via package manager (vcpkg) ----
# Enable these only if you actually use the subsystems (guards keep CI configurable)
if(ENABLE_IMGUI)
  find_package(imgui CONFIG REQUIRED)        # provides imgui::imgui and (via features) Win32/DX11 backends
endif()
# Taskflow is header-only; some vcpkg ports have CONFIG, others don't. Try CONFIG first.
find_package(taskflow CONFIG QUIET)
if(ENABLE_TRACY)
  find_package(tracy CONFIG REQUIRED)        # provides Tracy::TracyClient on vcpkg
endif()

# -------- Discover sources (exclude vendored imgui if present) --------
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

# --- BEGIN: Enable crash-dump TU with per-file /EHa (fixes MSVC C2712) ---
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/CrashDumpStub\\.cpp$")
  if(MSVC)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp"
      PROPERTIES COMPILE_OPTIONS "/EHa")
  endif()
endif()
# --- END: Enable crash-dump TU with per-file /EHa ---

# --- Frontend gating: native Win32 (default) or SDL ---
if(FRONTEND STREQUAL "sdl")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win32_main\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/launcher/LauncherMain\\.cpp$")
else()
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/Launcher_SDL\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/SDL.*\\.(c|cpp)$")
endif()

# =========================================================================================
# Build everything into a reusable core library that both the executable and the tests can link.
# This resolves unresolved externals in tests and centralizes engine code (Windows-only).
# =========================================================================================
add_library(colony_core STATIC ${SRC_ALL})

target_compile_features(colony_core PUBLIC cxx_std_20)
target_include_directories(colony_core
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"    # ADDED: expose public headers (e.g., include/rendering/...)
    "${CMAKE_SOURCE_DIR}"            # ADDED: allow includes like "platform/win/WinApp.h"
    "${CMAKE_BINARY_DIR}/generated"
)

# Optional PCH for faster compiles
if(COLONY_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(colony_core PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
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
  target_compile_options(colony_core PRIVATE /permissive- /Zc:preprocessor /utf-8)
endif()

# Windows system libs needed by path helpers, COM/KnownFolders, etc. (PUBLIC for tests)
if(WIN32)
  target_link_libraries(colony_core PUBLIC shell32 ole32)
endif()

# -------------------- NEW SECTION: Link all third-party libraries --------------------
# Ensure core library has all vcpkg-managed dependencies and build options linked in.
# This ensures fmt, spdlog, EnTT, nlohmann_json, and DirectX headers are visible for PCH.
target_link_libraries(colony_core
  PUBLIC
    ${COLONY_THIRDPARTY_LIBS}
    colony_build_options
)

# ImGui (Win32 + DX11 backends), only if enabled and available
if(ENABLE_IMGUI)
  if(TARGET imgui::imgui)
    target_link_libraries(colony_core PRIVATE imgui::imgui)
  endif()
  # Backends are exposed as separate targets by vcpkg features; guard in case of naming changes
  if(TARGET imgui::imgui-win32-binding)
    target_link_libraries(colony_core PRIVATE imgui::imgui-win32-binding)
  endif()
  if(TARGET imgui::imgui-dx11-binding)
    target_link_libraries(colony_core PRIVATE imgui::imgui-dx11-binding)
  endif()
endif()

# Tracy client library, only if enabled
if(ENABLE_TRACY)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(colony_core PRIVATE Tracy::TracyClient)
  else()
    message(FATAL_ERROR "Tracy enabled but CMake target Tracy::TracyClient not found. Install tracy via vcpkg or add TracyClient.cpp manually.")
  endif()
endif()

# Taskflow header-only include path fallback if CONFIG wasn't found
if(NOT taskflow_FOUND)
  find_path(TASKFLOW_INCLUDE_DIR taskflow/taskflow.hpp)
  if(NOT TASKFLOW_INCLUDE_DIR)
    message(FATAL_ERROR "Taskflow not found. Install via vcpkg (taskflow) or set TASKFLOW_INCLUDE_DIR.")
  endif()
  target_include_directories(colony_core PUBLIC "${TASKFLOW_INCLUDE_DIR}")
endif()

# =========================================================================================
# Executable: keep the game thin; it links the core lib.
# (The actual entry point(s) are compiled into colony_core by design.)
# =========================================================================================
add_executable(ColonyGame)

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

target_compile_features(ColonyGame PRIVATE cxx_std_20)
target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/include"    # ADDED
  "${CMAKE_SOURCE_DIR}"            # ADDED
  "${CMAKE_BINARY_DIR}/generated"
)

if(MSVC)
  target_compile_options(ColonyGame PRIVATE /permissive- /Zc:preprocessor /utf-8)
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
endif()

# Reusable runtime dir (works for single- and multi-config generators)
set(_RUNTIME_DIR "$<TARGET_FILE_DIR:ColonyGame>")

# ---------------------------------------------------------------------------
# POST-BUILD: stage runtime assets next to the ColonyGame.exe
# ---------------------------------------------------------------------------
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

# ------------------------- SHADERS -------------------------
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

# Precompiled header for the EXE
if(COLONY_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
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

if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include/common.hlsli")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res/shaders/include"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/renderer/Shaders/include/common.hlsli"
            "${_RUNTIME_DIR}/res/shaders/include/common.hlsli"
    VERBATIM
  )
endif()
