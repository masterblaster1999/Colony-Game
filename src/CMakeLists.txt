cmake_minimum_required(VERSION 3.20)
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

# (Optional) Set output name exactly as requested (colonygame.exe)
set_target_properties(colonygame PROPERTIES OUTPUT_NAME "colonygame")
