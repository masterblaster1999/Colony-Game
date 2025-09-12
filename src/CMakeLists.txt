cmake_minimum_required(VERSION 3.21) # needs 3.21+ for $<TARGET_RUNTIME_DLLS>
project(colonygame_win LANGUAGES CXX)

# Windows-only
if (NOT WIN32)
  message(FATAL_ERROR "This CMake file builds a Windows-only executable (Win32 + GDI).")
endif()

# Create executable (WIN32 => GUI subsystem, no console window)
add_executable(colonygame WIN32
    src/colonygame.cpp
)

# C++ standard + warnings
target_compile_features(colonygame PRIVATE cxx_std_17)
if (MSVC)
  target_compile_options(colonygame PRIVATE /W4 /permissive-)
endif()

# Defines to keep code concise
target_compile_definitions(colonygame PRIVATE
  UNICODE
  WIN32_LEAN_AND_MEAN
  WINVER=0x0A00
  _WIN32_WINNT=0x0A00
)

# Link against core Win32 libraries
target_link_libraries(colonygame PRIVATE
  user32
  gdi32
  shell32
  shlwapi
  comdlg32
  comctl32
  advapi32
  ole32
)

# (Optional) Emit PDBs in Release when desired
option(COLONY_RELEASE_SYMBOLS "Emit PDBs for Release on MSVC" OFF)
if (MSVC AND COLONY_RELEASE_SYMBOLS)
  target_compile_options(colonygame PRIVATE $<$<CONFIG:Release>:/Zi>)
  target_link_options(colonygame PRIVATE    $<$<CONFIG:Release>:/DEBUG>)
endif()

# (Optional) Set output name exactly as requested (colonygame.exe)
set_target_properties(colonygame PROPERTIES OUTPUT_NAME "colonygame")

# --- Helper scripts written into the build tree so this file is self-contained ---
set(_cmake_aux_dir "${CMAKE_BINARY_DIR}/cmake_aux")
file(MAKE_DIRECTORY "${_cmake_aux_dir}")

# Copies a single file if it exists: expects -Dsrc=... -Ddst=...
file(WRITE "${_cmake_aux_dir}/CopyIfExists.cmake" [=[
if(NOT DEFINED src OR src STREQUAL "")
  return()
endif()
if(EXISTS "${src}")
  file(COPY "${src}" DESTINATION "${dst}")
endif()
]=])

# Copies a '|' separated list of files if they exist: expects -Ddlls=... -Ddst=...
file(WRITE "${_cmake_aux_dir}/CopyRuntimeDLLs.cmake" [=[
if(NOT DEFINED dlls OR dlls STREQUAL "")
  return()
endif()
string(REPLACE "|" ";" _dlls "${dlls}")
foreach(f IN LISTS _dlls)
  if(EXISTS "${f}")
    file(COPY "${f}" DESTINATION "${dst}")
  endif()
endforeach()
]=])

# --- Post-build staging (exe + optional PDB + runtime DLLs) ---
# Stages to: build/stage/<Config> (e.g., build/stage/Release)
set(COLONY_STAGE_DIR "${CMAKE_BINARY_DIR}/stage/$<CONFIG>")
add_custom_command(TARGET colonygame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "Staging colonygame and runtime DLLs to: ${COLONY_STAGE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${COLONY_STAGE_DIR}"
  # 1) Always stage the EXE
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:colonygame>" "${COLONY_STAGE_DIR}"
  # 2) Copy PDB only if it exists
  COMMAND ${CMAKE_COMMAND} -Dsrc="$<TARGET_PDB_FILE:colonygame>" -Ddst="${COLONY_STAGE_DIR}" -P "${_cmake_aux_dir}/CopyIfExists.cmake"
  # 3) Copy all runtime DLLs (requires CMake 3.21+). Join with '|' to keep list intact across -D.
  COMMAND ${CMAKE_COMMAND} -Ddlls="$<JOIN:$<TARGET_RUNTIME_DLLS:colonygame>,|>" -Ddst="${COLONY_STAGE_DIR}" -P "${_cmake_aux_dir}/CopyRuntimeDLLs.cmake"
  VERBATIM
)
