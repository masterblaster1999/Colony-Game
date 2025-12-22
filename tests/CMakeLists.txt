# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# ----------------------------
# Dependencies (doctest)
# ----------------------------
if(NOT TARGET doctest::doctest)
  FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
  )
  FetchContent_MakeAvailable(doctest)
endif()

# ----------------------------
# Helper: MSB8027 fix (MSVC+VS)
# ----------------------------
function(colony_msvc_unique_objdirs)
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(_src IN LISTS ARGN)
      if(_src MATCHES "\\.(c|cc|cpp|cxx)$")
        # MSBuild recommended fix for MSB8027:
        set_property(SOURCE "${_src}" PROPERTY VS_SETTINGS "ObjectFileName=$(IntDir)%(RelativeDir)")
      endif()
    endforeach()
  endif()
endfunction()

# ----------------------------
# Test sources
# ----------------------------
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
)

if(NOT TEST_SOURCES)
  message(FATAL_ERROR "colony_tests: no test sources found in ${CMAKE_CURRENT_LIST_DIR}")
endif()

colony_msvc_unique_objdirs(${TEST_SOURCES})

add_executable(colony_tests ${TEST_SOURCES})
set_target_properties(colony_tests PROPERTIES FOLDER "tests")
target_compile_features(colony_tests PRIVATE cxx_std_23)

target_include_directories(colony_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}"
)

if(TARGET doctest::doctest)
  target_link_libraries(colony_tests PRIVATE doctest::doctest)
endif()

# Project-wide options (if present)
if(TARGET colony_build_options)
  target_link_libraries(colony_tests PRIVATE colony_build_options)
endif()

# Core
if(TARGET colony::core)
  target_link_libraries(colony_tests PRIVATE colony::core)
elseif(TARGET colony_core)
  target_link_libraries(colony_tests PRIVATE colony_core)
endif()

# Nav/path library (from pathfinding/)
if(TARGET colony::nav)
  target_link_libraries(colony_tests PRIVATE colony::nav)
elseif(TARGET colony::path)
  target_link_libraries(colony_tests PRIVATE colony::path)
elseif(TARGET colony_nav)
  target_link_libraries(colony_tests PRIVATE colony_nav)
endif()

# ---------------------------------------------------------
# Worldgen implementation fallback
# Fixes unresolved externals for StageContext + stages.
# ---------------------------------------------------------
set(COLONY_WORLDGEN_SOURCES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/src/worldgen")
  file(GLOB_RECURSE _wg CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.c"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cc"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cxx"
  )
  list(APPEND COLONY_WORLDGEN_SOURCES ${_wg})
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/src/pcg")
  file(GLOB_RECURSE _pcg CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/pcg/*.c"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cc"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cxx"
  )
  list(APPEND COLONY_WORLDGEN_SOURCES ${_pcg})
endif()

list(REMOVE_DUPLICATES COLONY_WORLDGEN_SOURCES)

if(COLONY_WORLDGEN_SOURCES)
  colony_msvc_unique_objdirs(${COLONY_WORLDGEN_SOURCES})

  add_library(colony_worldgen_obj OBJECT ${COLONY_WORLDGEN_SOURCES})
  set_target_properties(colony_worldgen_obj PROPERTIES FOLDER "tests")
  target_compile_features(colony_worldgen_obj PUBLIC cxx_std_23)

  target_include_directories(colony_worldgen_obj PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}"
  )

  if(TARGET colony::core)
    target_link_libraries(colony_worldgen_obj PUBLIC colony::core)
  elseif(TARGET colony_core)
    target_link_libraries(colony_worldgen_obj PUBLIC colony_core)
  endif()

  target_sources(colony_tests PRIVATE $<TARGET_OBJECTS:colony_worldgen_obj>)
endif()

# Optional repo helpers (only if defined)
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(colony_tests)
endif()
if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(colony_tests)
endif()
