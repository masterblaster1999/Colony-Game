# src/CMakeLists.txt

# -------- Discover sources (exclude vendored imgui if present) --------
file(GLOB_RECURSE SRC_ALL
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c"
  "${CMAKE_SOURCE_DIR}/src/*.h"   "${CMAKE_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_SOURCE_DIR}/platform/win/*.h"
)
list(FILTER SRC_ALL EXCLUDE REGEX ".*/third_party/imgui/.*")

if(EXISTS "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
  list(APPEND SRC_ALL "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
endif()

# --- BEGIN: Stabilize crash-dump TU for MSVC (C2712) ---
# The TU 'platform/win/CrashDumpWin.cpp' uses __try/__except in functions
# that currently trigger C2712 under /EHsc. We:
#   - exclude it from this build
#   - add a tiny stub that exports the same API so linking succeeds
#   - leave a one-line switch to bring it back (compiled with /EHa) once refactored.
list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/CrashDumpWin\\.cpp$")

if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.h")
  list(APPEND SRC_ALL "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpStub.cpp")
endif()
# --- END: Stabilize crash-dump TU for MSVC (C2712) ---

# --- Frontend gating: build either the native Win32 frontend (default) or the SDL one ---
if(FRONTEND STREQUAL "sdl")
  # For SDL builds, drop Win32 entry points that would conflict with SDL2main
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win32_main\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/launcher/LauncherMain\\.cpp$")
else()
  # Default: Win32 frontend – exclude any SDL-only sources
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/Launcher_SDL\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/SDL.*\\.(c|cpp)$")
endif()

add_executable(ColonyGame ${SRC_ALL})

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

target_compile_features(ColonyGame PRIVATE cxx_std_20)
target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
)

# Make sure this target is /utf-8 too (fmt 11.x requires it)
if(MSVC)
  target_compile_options(ColonyGame PRIVATE /utf-8)
endif()

# A4: Precompiled header (optional, gated by COLONY_USE_PCH)
if(COLONY_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
endif()

# A4: Unity builds (opt‑in; speeds up large translation units)
if(COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
endif()

# Compile-time defines for optional systems + deterministic frontend switch
# Define COLONY_USE_SDL only when building the SDL frontend.
target_compile_definitions(ColonyGame PRIVATE
  $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
)

# Link libs (propagate Windows flags/defs from interface target)
# Note: link SDL2 only for the SDL frontend; make SDL2main optional if the package doesn't provide it.
target_link_libraries(ColonyGame
  PRIVATE
    colony_build_options
    $<$<STREQUAL:${FRONTEND},sdl>:$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>>
    $<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2>
)

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
  target_link_libraries(ColonyGame PRIVATE dbghelp shell32 ole32)
endif()

# Make the game the startup project in Visual Studio
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)

# Output directories per configuration (for multi-config generators like VS)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  endforeach()
endif()

# Stage assets next to the EXE after build (only if the source dirs exist)
if(EXISTS "${CMAKE_SOURCE_DIR}/res")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "$<TARGET_FILE_DIR:ColonyGame>/res"
    VERBATIM
  )
endif()

# A6: Stage compiled shader blobs into the runtime res/shaders folder next to the EXE.
# All toolchains (VS HLSL, FXC, DXC) now emit *.cso under ${CMAKE_BINARY_DIR}/res/shaders[/<cfg>].
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_BINARY_DIR}/res/shaders/$<IF:$<BOOL:${CMAKE_CONFIGURATION_TYPES}>,$<CONFIG>,.>"
          "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
  VERBATIM
)

# Ensure shaders are built before we copy (when a 'shaders' target exists)
if(TARGET shaders)
  add_dependencies(ColonyGame shaders)
endif()

# ------------------------------------------------------------------------------
# OPTIONAL (when you’re ready to bring back the real CrashDumpWin.cpp):
#   1) Remove the EXCLUDE + Stub block above
#   2) Add this per-source flag to allow SEH+C++ in that TU:
# if(MSVC AND EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp")
#   set_source_files_properties("${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp"
#     PROPERTIES COMPILE_OPTIONS "/EHa")
# endif()
#   3) In CrashDumpWin.cpp, make PresetFor take CrashDumpWin::DumpLevel and
#      add static_casts where you assign between enum types.
# ------------------------------------------------------------------------------
