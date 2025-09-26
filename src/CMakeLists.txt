# -------- Discover sources (exclude vendored imgui if present) --------
file(GLOB_RECURSE SRC_ALL
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c"
  "${CMAKE_SOURCE_DIR}/src/*.h"   "${CMAKE_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_SOURCE_DIR}/platform/win/*.h"
)

# Remove any accidental third_party/imgui sources from builds; we link vcpkg imgui instead.
list(FILTER SRC_ALL EXCLUDE REGEX ".*/third_party/imgui/.*")

# Include any WinLauncher at repo root if it exists (some repos keep this outside src/)
if(EXISTS "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
  list(APPEND SRC_ALL "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
endif()

# -------- Target --------
add_executable(ColonyGame ${SRC_ALL})

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

# Compile features & include paths
target_compile_features(ColonyGame PRIVATE cxx_std_20)
target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
)

# Precompiled header if available
if(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
endif()

# Compile-time defines for optional systems
target_compile_definitions(ColonyGame PRIVATE
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE>
)

# Link libraries (Windows libs + vcpkg third-party)
target_link_libraries(ColonyGame
  PRIVATE
    colony_win32_libs
    ${COLONY_THIRDPARTY_LIBS}
    $<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2;SDL2::SDL2main>
)

# Add Windows resources if you ship them
if(WIN32)
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()
  # Crash handler or GPU preference helpers if they exist
  foreach(_f IN ITEMS
      "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp"
      "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp"
    )
    if(EXISTS "${_f}")
      target_sources(ColonyGame PRIVATE "${_f}")
    endif()
  endforeach()
  # dbghelp/shell32 if needed by your crash handler or shell integration
  target_link_libraries(ColonyGame PRIVATE dbghelp shell32 ole32)
endif()

# Make ColonyGame the startup project in VS and set a sane working directory
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
set_target_properties(ColonyGame PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$(Configuration)"
)

# Output directories per configuration (VS multi-config generators)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  endforeach()
endif()

# Stage assets after build (so a local run works out-of-the-box)
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res"     "$<TARGET_FILE_DIR:ColonyGame>/res"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:ColonyGame>/shaders"
  VERBATIM
)
