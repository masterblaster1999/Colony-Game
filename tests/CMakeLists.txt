# tests/CMakeLists.txt
#
# Builds colony_tests (Windows-focused, MSVC-friendly).
# Uses doctest as header-only (FetchContent without add_subdirectory)
# to avoid CMake 4.x failing on doctest's old cmake_minimum_required().
#
# Also includes an MSB8027 mitigation for Visual Studio generators.

include(FetchContent)

# ----------------------------
# Dependency: doctest (header-only, CMake-4-safe)
# ----------------------------
if(NOT TARGET doctest::doctest)
  FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
  )

  FetchContent_GetProperties(doctest)
  if(NOT doctest_POPULATED)
    FetchContent_Populate(doctest)
  endif()

  # doctest is header-only; create our own interface target.
  add_library(doctest INTERFACE)
  add_library(doctest::doctest ALIAS doctest)

  # The header is included as <doctest/doctest.h>, so the repo root is the include dir.
  target_include_directories(doctest INTERFACE "${doctest_SOURCE_DIR}")
endif()

# ----------------------------
# Helper: MSB8027 fix (MSVC+Visual Studio generator)
# - MSBuild warns if multiple .cpp have same basename -> same .obj output.
# - Use VS_SETTINGS when possible; fallback to /Fo otherwise.
# ----------------------------
function(colony_msvc_unique_objdirs)
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(_src IN LISTS ARGN)
      if(_src MATCHES "\\.(c|cc|cpp|cxx)$")
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.22")
          set_property(
            SOURCE "${_src}"
            PROPERTY VS_SETTINGS "ObjectFileName=$(IntDir)%(RelativeDir)"
          )
        else()
          set_property(
            SOURCE "${_src}"
            APPEND PROPERTY COMPILE_OPTIONS "/Fo$(IntDir)%(RelativeDir)"
          )
        endif()
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

target_link_libraries(colony_tests PRIVATE doctest::doctest)

# Project-wide options (if present)
if(TARGET colony_build_options)
  target_link_libraries(colony_tests PRIVATE colony_build_options)
endif()

# Core library
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
# Worldgen implementation fallback (fixes unresolved externals)
#
# Your test link errors include StageContext and stage generate() vfuncs.
# That usually means the implementation .cpps are not being linked into tests.
#
# This builds worldgen+pcg sources into an OBJECT library and injects them into
# colony_tests via $<TARGET_OBJECTS:...>.
# ---------------------------------------------------------
set(COLONY_WORLDGEN_SOURCES "")

if(EXISTS "${CMAKE_SOURCE_DIR}/src/worldgen")
  file(GLOB_RECURSE _wg CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.c"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cc"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cxx"
  )
  list(APPEND COLONY_WORLDGEN_SOURCES ${_wg})
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/src/pcg")
  file(GLOB_RECURSE _pcg CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/pcg/*.c"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cc"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cpp"
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

  # ✅ Correct generator expression (your current file had a broken "$")
  target_sources(colony_tests PRIVATE $<TARGET_OBJECTS:colony_worldgen_obj>)
endif()

# Optional repo helper hooks (only if defined)
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(colony_tests)
endif()

if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(colony_tests)
endif()
