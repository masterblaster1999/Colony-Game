cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

# Ensure CTest is enabled for add_test()/ctest integration
include(CTest)

# --------------------------------------------------------------------
# Collect test sources (and auto-create a tiny smoke test if none exist)
# --------------------------------------------------------------------
file(GLOB_RECURSE TEST_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp"
)

if(NOT TEST_SRC)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/smoke")
  set(_smoke "${CMAKE_CURRENT_SOURCE_DIR}/smoke/smoke.cpp")
  if(NOT EXISTS "${_smoke}")
    file(WRITE "${_smoke}" "#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include <doctest/doctest.h>\nTEST_CASE(\"smoke\"){CHECK(true);} \n")
  endif()
  list(APPEND TEST_SRC "${_smoke}")
endif()

# --------------------------------------------------------------------
# Doctest: prefer vcpkg config package; otherwise a clean header-only fallback
# (No subproject → no external CMake deprecation warnings.)
# If desired, you may flip DOCTEST_FETCH=ON to fetch the header as a last resort.
# --------------------------------------------------------------------
find_package(doctest CONFIG QUIET)

if(doctest_FOUND)
  set(_doctest_target doctest::doctest)
else()
  add_library(doctest INTERFACE)
  # If you keep a single-header copy, place it under third_party/doctest/
  target_include_directories(doctest INTERFACE "${CMAKE_SOURCE_DIR}/third_party/doctest")
  set(_doctest_target doctest)

  option(DOCTEST_FETCH "Fetch doctest header via FetchContent when not available via vcpkg or third_party" OFF)
  if(DOCTEST_FETCH)
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(doctest_upstream
      GIT_REPOSITORY https://github.com/doctest/doctest.git
      GIT_TAG v2.4.11
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(doctest_upstream) # header-only; no subproject build
    target_include_directories(doctest INTERFACE "${doctest_upstream_SOURCE_DIR}")
  endif()
endif()

# --------------------------------------------------------------------
# Test executable
# --------------------------------------------------------------------
add_executable(colony_smoke ${TEST_SRC})
target_compile_features(colony_smoke PRIVATE cxx_std_20)
target_link_libraries(colony_smoke PRIVATE ${_doctest_target})

# Register with CTest; set a stable working directory for file-relative tests
add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
