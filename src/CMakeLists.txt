# -------- Source lists --------
set(SRC_CORE
  core/app.cpp core/app.h
  # ... add your actual sources here
)

set(SRC_WIN32
  platform/win/WinLauncher.cpp
  platform/win/win32_main.cpp
  # split big platform file(s) here if needed (see section 5)
)

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
target_link_libraries(ColonyGame
  d3d11 dxgi d3dcompiler
  $<$<BOOL:${ENABLE_IMGUI}>:imgui>
  $<$<BOOL:${ENABLE_TRACY}>:tracy_client>
  $<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2 SDL2::SDL2main>
)

# Add Windows platform resources and helpers (manifest, version info, crash handler, GPU preference)
if(WIN32)
  target_sources(ColonyGame PRIVATE
    "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc"
    "${CMAKE_BINARY_DIR}/generated/Version.rc"
    "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp"
    "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.h"
    "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp"
  )
  # Explicit system libs for crash dumps and shell paths
  target_link_libraries(ColonyGame PRIVATE dbghelp shell32 ole32)
endif()

# Compile-time defines for optional systems
target_compile_definitions(ColonyGame PRIVATE
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE>
)

# Include paths: local src + generated headers (build_info.h, etc.)
target_include_directories(ColonyGame PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
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
