# -------- Source lists (robust, no hardcoded nonexistent files) --------

# Discover core/game sources under src/
file(GLOB_RECURSE SRC_CORE CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/*.cxx"
  "${CMAKE_SOURCE_DIR}/src/*.cc"
  "${CMAKE_SOURCE_DIR}/src/*.c"
)

# Discover Windows platform sources (support either layout: platform/win/* or src/platform/win/*)
file(GLOB_RECURSE SRC_WIN32 CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp"
  "${CMAKE_SOURCE_DIR}/platform/win/*.c"
  "${CMAKE_SOURCE_DIR}/src/platform/win/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/win/*.c"
)

# Include the existing launcher at the repo root if present
if(EXISTS "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
  list(APPEND SRC_WIN32 "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
endif()

list(REMOVE_DUPLICATES SRC_CORE)
list(REMOVE_DUPLICATES SRC_WIN32)

# Fail early if we truly have no sources (prevents add_executable() with an empty list)
if(NOT SRC_CORE AND NOT SRC_WIN32)
  message(FATAL_ERROR "No source files found under src/ or platform/win/, and no WinLauncher.cpp at repo root.")
endif()

# -------- Target --------
add_executable(ColonyGame
  ${SRC_CORE}
  ${SRC_WIN32}
)

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

# Link libs (keep existing + append Windows-specific as needed)
# NOTE: Use keyword form everywhere (PRIVATE) to avoid mixing signatures (CMP0023).
# Also: when a generator expression needs to expand to multiple libraries,
#       use a *semicolon*-separated list and quote the genex argument.
target_link_libraries(ColonyGame
  PRIVATE
    d3d11
    dxgi
    d3dcompiler
    $<$<BOOL:${ENABLE_IMGUI}>:imgui>
    $<$<BOOL:${ENABLE_TRACY}>:tracy_client>
    "$<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2;SDL2::SDL2main>"
)

# Add Windows platform resources and helpers (manifest, version info, crash handler, GPU preference)
if(WIN32)
  # Collect optional sources/resources safely (only add if they exist)
  set(_WIN_OPTIONAL_SOURCES)

  # Manifest & version resource
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    list(APPEND _WIN_OPTIONAL_SOURCES "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    list(APPEND _WIN_OPTIONAL_SOURCES "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()

  # Crash handler (cpp only; header doesn't need to be added as a source to compile)
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp")
    list(APPEND _WIN_OPTIONAL_SOURCES "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp")
  endif()

  # GPU preference shim (support both possible layouts)
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp")
    list(APPEND _WIN_OPTIONAL_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/GpuPreference.cpp")
    list(APPEND _WIN_OPTIONAL_SOURCES "${CMAKE_SOURCE_DIR}/platform/win/GpuPreference.cpp")
  endif()

  if(_WIN_OPTIONAL_SOURCES)
    target_sources(ColonyGame PRIVATE ${_WIN_OPTIONAL_SOURCES})
  endif()

  # Explicit system libs for crash dumps and shell paths (only link if on Windows)
  target_link_libraries(ColonyGame PRIVATE dbghelp shell32 ole32)
endif()

# Compile-time defines for optional systems
target_compile_definitions(ColonyGame PRIVATE
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE>
)

# Include paths: local src + generated headers (build_info.h, etc.)
target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}/generated"
)

# Precompiled header (if the file exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
endif()

# Make the game the startup project in Visual Studio and set a sane working directory
# (Working dir resolves to <build>/bin/<cfg> so relative asset paths work out-of-the-box.)
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

# Install rules & staging target (exe + pdb + assets)
include(GNUInstallDirs)
install(TARGETS ColonyGame RUNTIME DESTINATION "bin/$<CONFIG>")
install(FILES "$<TARGET_PDB_FILE:ColonyGame>" DESTINATION "bin/$<CONFIG>" OPTIONAL)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/"     DESTINATION "stage/$<CONFIG>/res")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders/" DESTINATION "stage/$<CONFIG>/shaders" OPTIONAL)

# Stage into build/stage/<cfg> using CMake's install step
if(NOT TARGET stage_win)
  add_custom_target(stage_win
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
    COMMENT "Staging into build/stage/$<CONFIG>")
endif()
