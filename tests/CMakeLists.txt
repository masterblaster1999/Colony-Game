# tests/CMakeLists.txt

# ------------------------------------------------------------
# Doctest
#
# Prefer vcpkg's manifest-provided doctest (already listed in vcpkg.json)
# to avoid network fetches during configure/build.
#
# Fallback to FetchContent header-only if doctest isn't available.
# ------------------------------------------------------------
find_package(doctest CONFIG QUIET)

if(NOT doctest_FOUND)
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
endif()

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

# Legacy runner kept in-tree for reference; the actual runner is test_main.cpp.
list(FILTER COLONY_TEST_SOURCES EXCLUDE REGEX "([/\\\\])main\\.cpp$")

# Be explicit about the runner TU so CI / VS generators never accidentally drop
# it when unity builds are enabled globally.
set(_COLONY_TEST_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp")
if(EXISTS "${_COLONY_TEST_MAIN}")
  get_filename_component(_COLONY_TEST_MAIN_ABS "${_COLONY_TEST_MAIN}" ABSOLUTE)
  list(REMOVE_ITEM COLONY_TEST_SOURCES "${_COLONY_TEST_MAIN_ABS}")
  list(APPEND COLONY_TEST_SOURCES "${_COLONY_TEST_MAIN_ABS}")
endif()

list(SORT COLONY_TEST_SOURCES)

add_executable(colony_tests ${COLONY_TEST_SOURCES})
set_target_properties(colony_tests PROPERTIES FOLDER "tests")
target_compile_features(colony_tests PRIVATE cxx_std_23)

# Tests are a bad fit for unity/jumbo builds: macros from unrelated translation
# units can leak into the runner and even rewrite `main()` (e.g., SDL2's
# historical `#define main SDL_main`). Disable unity builds for the test target
# even if they are enabled project-wide.
set_target_properties(colony_tests PROPERTIES UNITY_BUILD OFF)

target_link_libraries(colony_tests PRIVATE doctest::doctest)

# Ensure the doctest implementation + custom main() TU never ends up inside a
# unity/jumbo TU. Otherwise its implementation macros can leak into other test
# TUs and/or cause ODR issues.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp"
    PROPERTIES
      SKIP_UNITY_BUILD_INCLUSION ON
  )
endif()

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
set(COLONY_WORLDGEN_CORE_SOURCES "")
set(COLONY_WORLDGEN_STAGE_SOURCES "")
set(COLONY_PCG_SOURCES "")

if(EXISTS "${CMAKE_SOURCE_DIR}/src/worldgen")
  # Split worldgen into multiple OBJECT libs to avoid MSVC .obj name collisions
  # (e.g. Biomes.cpp, Noise.cpp, Hydrology.cpp exist in multiple subfolders).
  file(GLOB_RECURSE _wg_all CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.c"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cc"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/worldgen/*.cxx"
  )
  foreach(_f IN LISTS _wg_all)
    if(_f MATCHES "[/\\\\]stages[/\\\\]")
      list(APPEND COLONY_WORLDGEN_STAGE_SOURCES "${_f}")
    else()
      list(APPEND COLONY_WORLDGEN_CORE_SOURCES  "${_f}")
    endif()
  endforeach()
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/src/pcg")
  file(GLOB_RECURSE _pcg CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/pcg/*.c"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cc"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/pcg/*.cxx"
  )
  list(APPEND COLONY_PCG_SOURCES ${_pcg})
endif()

list(REMOVE_DUPLICATES COLONY_WORLDGEN_CORE_SOURCES)
list(REMOVE_DUPLICATES COLONY_WORLDGEN_STAGE_SOURCES)
list(REMOVE_DUPLICATES COLONY_PCG_SOURCES)

function(_colony_tests_add_objlib _name _srcs)
  if(NOT _srcs)
    return()
  endif()

  add_library(${_name} OBJECT ${_srcs})
  set_target_properties(${_name} PROPERTIES
    FOLDER "tests"
    UNITY_BUILD OFF  # critical: prevents stage helper collisions
  )

  target_compile_features(${_name} PUBLIC cxx_std_23)

  target_include_directories(${_name} PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
  )

  # Link Core if it exists (types/utilities used by worldgen / pcg).
  if(TARGET colony::core)
    target_link_libraries(${_name} PUBLIC colony::core)
  elseif(TARGET colony_core)
    target_link_libraries(${_name} PUBLIC colony_core)
  endif()

  # Actually link the compiled objects into the test executable.
  target_sources(colony_tests PRIVATE $<TARGET_OBJECTS:${_name}>)
endfunction()

_colony_tests_add_objlib(colony_worldgen_core_obj   "${COLONY_WORLDGEN_CORE_SOURCES}")
_colony_tests_add_objlib(colony_worldgen_stages_obj "${COLONY_WORLDGEN_STAGE_SOURCES}")
_colony_tests_add_objlib(colony_pcg_obj             "${COLONY_PCG_SOURCES}")

# Fix MSB8027 for the tests' own sources too
colony_msvc_unique_objnames_for_sources(COLONY_TEST_SOURCES)

# Register with CTest (`ctest --test-dir <build>`).
add_test(NAME colony_tests COMMAND colony_tests)
