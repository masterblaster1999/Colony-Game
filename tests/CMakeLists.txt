cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

include(FetchContent)

# -- doctest from package (vcpkg provides doctest::doctest)
find_package(doctest CONFIG REQUIRED)

# -- PCG header-only as an INTERFACE target if needed
if(NOT TARGET pcg)
  # Try a config package first
  find_package(pcg CONFIG QUIET)
  if(NOT TARGET pcg)
    find_path(PCG_INCLUDE_DIR NAMES pcg_random.hpp)
    if(PCG_INCLUDE_DIR)
      add_library(pcg INTERFACE)
      target_include_directories(pcg INTERFACE "${PCG_INCLUDE_DIR}")
    else()
      # FetchContent fallback
      set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
      FetchContent_Declare(pcg_cpp
        GIT_REPOSITORY https://github.com/imneme/pcg-cpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
      FetchContent_MakeAvailable(pcg_cpp)
      add_library(pcg INTERFACE)
      target_include_directories(pcg INTERFACE "${pcg_cpp_SOURCE_DIR}/include")
    endif()
  endif()
endif()

# -- Collect tests (exclude any extra runner TU if you had one)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/tests/*.cpp")
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp") # if it defines its own main

# -- Build a dedicated static lib for pathfinding and link it
#    (captures jps/hpa implementations so tests resolve symbols)
file(GLOB_RECURSE PATH_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/pathfinding/*.cpp"
     "${CMAKE_SOURCE_DIR}/hpa/*.cpp")
if(PATH_SOURCES)
  add_library(colony_path STATIC ${PATH_SOURCES})
  target_include_directories(colony_path PUBLIC
    "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/pathfinding" "${CMAKE_SOURCE_DIR}/hpa")
  target_compile_features(colony_path PUBLIC cxx_std_20)
endif()

add_executable(colony_smoke ${TEST_SOURCES})
target_compile_features(colony_smoke PRIVATE cxx_std_23)

if(MSVC)
  target_compile_options(colony_smoke PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
else()
  target_compile_options(colony_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(colony_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/worldgen
)

# Optional, if your repo defines these:
if(TARGET procgen)
  target_link_libraries(colony_smoke PRIVATE procgen)
endif()
if(TARGET colony_core)
  target_link_libraries(colony_smoke PRIVATE colony_core)
endif()
if(TARGET colony_path)
  target_link_libraries(colony_smoke PRIVATE colony_path)
endif()

# Header-only deps
target_link_libraries(colony_smoke PRIVATE pcg doctest::doctest)

enable_testing()
add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
