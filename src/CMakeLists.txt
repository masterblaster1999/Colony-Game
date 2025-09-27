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

# --- Exclude SDL sources unless the SDL frontend is selected ---
if(NOT FRONTEND STREQUAL "sdl")
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

# Precompiled header (optional)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
endif()

# Compile-time defines for optional systems + deterministic SDL switch
target_compile_definitions(ColonyGame PRIVATE
  $<$<STREQUAL:${FRONTEND},sdl>:BUILD_WITH_SDL2=1>
  $<$<NOT:$<STREQUAL:${FRONTEND},sdl>>:BUILD_WITH_SDL2=0>
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE>
)

# Link libs (propagate Windows flags/defs/libs from interface target)
target_link_libraries(ColonyGame
  PRIVATE
    colony_win32_libs
    "$<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2;SDL2::SDL2main>"
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

# Make the game the startup project in Visual Studio and set a sane working directory
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
set_target_properties(ColonyGame PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$(Configuration)"
)

# Output directories per configuration (for multi-config generators like VS)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  endforeach()
endif()

# Stage assets next to the EXE after build
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res"     "$<TARGET_FILE_DIR:ColonyGame>/res"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:ColonyGame>/shaders"
  VERBATIM
)

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
