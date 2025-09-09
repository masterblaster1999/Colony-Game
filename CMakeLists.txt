cmake_minimum_required(VERSION 3.16)
project(ColonyGame LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER GAME_SOURCES EXCLUDE REGEX "SingleClick\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "WinLauncher\\.cpp$")

add_executable(ColonyGame ${GAME_SOURCES})

if (MSVC)
  target_compile_options(ColonyGame PRIVATE /W4 /permissive-)
else()
  target_compile_options(ColonyGame PRIVATE -Wall -Wextra -Wpedantic)
endif()
