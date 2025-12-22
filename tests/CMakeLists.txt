# tests/CMakeLists.txt

# ------------------------------------------------------------
# Doctest (header-only fetch). Avoids running doctest's CMake.
# ------------------------------------------------------------
if(POLICY CMP0169)
  # CMake 4.2+: calling FetchContent_Populate directly is deprecated under NEW.
  # We keep the header-only approach and allow it via OLD for now.
  cmake_policy(SET CMP0169 OLD)
endif()

include(FetchContent)

FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.11
  GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(doctest)
if(NOT doctest_POPULATED)
  FetchContent_Populate(doctest)
endif()

add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
# doctest header is usually included as: #include <doctest/doctest.h>
target_include_directories(doctest INTERFACE "${doctest_SOURCE_DIR}")

# ------------------------------------------------------------
# Helper: unique, drive-letter-safe .obj naming for MSVC/VS
# ------------------------------------------------------------
function(colony_msvc_unique_objnames_for_sources sources_list)
  if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    return()
  endif()
  if(NOT CMAKE_VERSION VERSION_GREATER_EQUAL "3.22")
    message(WARNING "CMake < 3.22: per-source VS_SETTINGS may not be honored reliably.")
    return()
  endif()

  foreach(_src IN LISTS ${sources_list})
    if(NOT _src MATCHES "\\.(c|cc|cpp|cxx)$")
      continue()
    endif()

    get_filename_component(_abs "${_src}" ABSOLUTE)

    # Make a stable relative path under the repo root.
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")

    # If the file is outside the repo root, hash it into a safe bucket.
    if(_rel MATCHES "^\\.\\.")
      string(MD5 _h "${_abs}")
      set(_rel "_external/${_h}/${_src}")
    endif()

    get_filename_component(_subdir "${_rel}" DIRECTORY)
    get_filename_component(_base   "${_src}" NAME_WE)

    # Sanitize (no ':' allowed inside the path).
    string(REPLACE ":" "_" _subdir "${_subdir}")
    string(REPLACE ".." "__" _subdir "${_subdir}")
    string(REPLACE "/" "\\" _subdir_win "${_subdir}")

    if(_subdir_win STREQUAL ".")
      set(_subdir_win "")
    endif()

    if(_subdir_win STREQUAL "")
      set(_obj "$(IntDir)${_base}.obj")
    else()
      set(_obj "$(IntDir)${_subdir_win}\\${_base}.obj")
    endif()

    set_property(SOURCE "${_src}" PROPERTY VS_SETTINGS "ObjectFileName=${_obj}")
  endforeach()
endfunction()

# ------------------------------------------------------------
# Test executable
# ------------------------------------------------------------
file(GLOB CONFIGURE_DEPENDS COLONY_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

add_executable(colony_tests ${COLONY_TEST_SOURCES})
set_target_properties(colony_tests PROPERTIES FOLDER "tests")
target_compile_features(colony_tests PRIVATE cxx_std_23)
target_compile_definitions(colony_tests PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)

target_link_libraries(colony_tests PRIVATE doctest::doctest)

# Link project libs when present
if(TARGET colony::core)
  target_link_libraries(colony_tests PRIVATE colony::core)
elseif(TARGET colony_core)
  target_link_libraries(colony_tests PRIVATE colony_core)
endif()

if(TARGET colony::nav)
  target_link_libraries(colony_tests PRIVATE colony::nav)
elseif(TARGET colony_nav)
  target_link_libraries(colony_tests PRIVATE colony_nav)
endif()

if(TARGET colony::platform_win)
  target_link_libraries(colony_tests PRIVATE colony::platform_win)
elseif(TARGET colony_platform_win)
  target_link_libraries(colony_tests PRIVATE colony_platform_win)
elseif(TARGET win_platform)
  target_link_libraries(colony_tests PRIVATE win_platform)
endif()

# ------------------------------------------------------------
# Worldgen objects for tests (fix unresolved externals + keep tests hermetic)
# ------------------------------------------------------------
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
  add_library(colony_worldgen_obj OBJECT ${COLONY_WORLDGEN_SOURCES})
  set_target_properties(colony_worldgen_obj PROPERTIES
    FOLDER "tests"
    UNITY_BUILD OFF          # <-- critical: prevents stage helper collisions
  )

  target_compile_features(colony_worldgen_obj PUBLIC cxx_std_23)

  # Inherit include dirs from the main project layout.
  target_include_directories(colony_worldgen_obj PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
  )

  # Link Core if it exists (types/utilities used by worldgen).
  if(TARGET colony::core)
    target_link_libraries(colony_worldgen_obj PUBLIC colony::core)
  elseif(TARGET colony_core)
    target_link_libraries(colony_worldgen_obj PUBLIC colony_core)
  endif()

  # Fix MSB3191/MSB8027 for this object lib in VS/MSVC
  colony_msvc_unique_objnames_for_sources(COLONY_WORLDGEN_SOURCES)

  # Actually link the compiled objects into the test executable
  target_sources(colony_tests PRIVATE $<TARGET_OBJECTS:colony_worldgen_obj>)
endif()

# Fix MSB8027 for the tests' own sources too
colony_msvc_unique_objnames_for_sources(COLONY_TEST_SOURCES)
