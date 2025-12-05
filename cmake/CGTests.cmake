# cmake/CGTests.cmake  (idempotent)
# Add tests/ once and advertise via a cache flag other scripts can see.

# If someone already added the tests, bail out early.
if(DEFINED COLONY_TESTS_ADDED)
  message(STATUS "CGTests: tests/ already added (COLONY_TESTS_ADDED). Skipping.")
  return()
endif()

# Respect the project-wide toggle (default ON in your root)
if(NOT COLONY_BUILD_TESTS)
  message(STATUS "CGTests: COLONY_BUILD_TESTS=OFF; skipping tests/")
  return()
endif()

# Add tests/ with an explicit binary dir to make intent obvious.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory("${CMAKE_SOURCE_DIR}/tests" "${CMAKE_BINARY_DIR}/tests")
  set(COLONY_TESTS_ADDED ON CACHE INTERNAL "tests/ added by CGTests")
else()
  message(WARNING "CGTests: tests/CMakeLists.txt not found; skipping.")
endif()
