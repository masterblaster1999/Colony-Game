cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

include(FetchContent)

# --------------------------------------------------------------
# CTest wiring (make tests visible to ctest from the build root)
# --------------------------------------------------------------
include(CTest)            # defines BUILD_TESTING and auto-calls enable_testing() unless BUILD_TESTING=OFF
enable_testing()          # ensure a CTestTestfile.cmake exists for this dir (important for ctest)  # docs: cmake.org/cmake/help/latest/command/enable_testing.html

if(NOT BUILD_TESTING)
    message(STATUS "BUILD_TESTING=OFF => skipping unit tests configuration")
    return()
endif()

# --------------------------------------------------------------
# doctest (header-only) as an INTERFACE target
# --------------------------------------------------------------
if(NOT TARGET doctest)
    find_package(doctest CONFIG QUIET)
    if(TARGET doctest::doctest)
        # Use the imported target from a package manager / system install.
        add_library(doctest INTERFACE)
        target_link_libraries(doctest INTERFACE doctest::doctest)
        # Imported targets should propagate INTERFACE_INCLUDE_DIRECTORIES.
        # Nothing else is needed here.

        # NOTE: Some doctest packages also provide doctest::doctest_with_main.
        # We intentionally do NOT link any "...with_main" target, because we provide
        # our own single runner TU (test_main.cpp) to avoid ODR/linker conflicts.
        if(TARGET doctest::doctest_with_main)
            message(STATUS "Found doctest::doctest_with_main (NOT linking it; using our own test_main.cpp runner)")
        endif()
    else()
        # FetchContent fallback: bring in upstream and publish its includes
        set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
        FetchContent_Declare(doctest_upstream
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        v2.4.11
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(doctest_upstream)

        add_library(doctest INTERFACE)
        # Publish the repository root so <doctest/doctest.h> works.
        target_include_directories(doctest INTERFACE
            "${doctest_upstream_SOURCE_DIR}"
        )
    endif()
endif()

# Try to load doctest's CMake helper for test discovery if we fetched the repo.
# (Falls back to a manual add_test() if the module isn't available.)
set(_DOCTEST_CMAKE_FOUND OFF)
if(DEFINED doctest_upstream_SOURCE_DIR AND EXISTS "${doctest_upstream_SOURCE_DIR}/scripts/cmake/doctest.cmake")
    list(APPEND CMAKE_MODULE_PATH "${doctest_upstream_SOURCE_DIR}/scripts/cmake")
    include(doctest)      # defines doctest_discover_tests()
    set(_DOCTEST_CMAKE_FOUND ON)
endif()

# --------------------------------------------------------------
# PCG RNG (header-only)
# --------------------------------------------------------------
if(NOT TARGET pcg)
    find_package(pcg CONFIG QUIET)
    if(NOT TARGET pcg)
        find_path(PCG_INCLUDE_DIR NAMES pcg_random.hpp)
        if(PCG_INCLUDE_DIR)
            add_library(pcg INTERFACE)
            target_include_directories(pcg INTERFACE "${PCG_INCLUDE_DIR}")
        else()
            set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
            FetchContent_Declare(pcg_cpp
                GIT_REPOSITORY https://github.com/imneme/pcg-cpp.git
                GIT_TAG        master
                GIT_SHALLOW    TRUE
            )
            FetchContent_MakeAvailable(pcg_cpp)

            add_library(pcg INTERFACE)
            target_include_directories(pcg INTERFACE
                "${pcg_cpp_SOURCE_DIR}/include"
            )
        endif()
    endif()
endif()

# --------------------------------------------------------------
# Test sources & single runner
# --------------------------------------------------------------
set(TEST_SOURCES)

# Runner TU (exactly one translation unit is allowed to implement doctest / provide main)
# Prefer a checked-in tests/test_main.cpp if it exists; otherwise generate one into the build tree.
set(_source_runner    "${CMAKE_SOURCE_DIR}/tests/test_main.cpp")
set(_generated_runner "${CMAKE_CURRENT_BINARY_DIR}/test_main.cpp")
set(_RUNNER_IS_GENERATED OFF)

if(EXISTS "${_source_runner}")
    set(_runner "${_source_runner}")
else()
    set(_runner "${_generated_runner}")
    set(_RUNNER_IS_GENERATED ON)
    file(WRITE "${_runner}" [=[
        #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
        #include <doctest/doctest.h>
    ]=])
endif()

list(APPEND TEST_SOURCES "${_runner}")

# Add all tests (except any old runner or smoke with another main)
file(GLOB_RECURSE _MORE_TESTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tests/*.cpp"
)
list(FILTER _MORE_TESTS EXCLUDE REGEX ".*/(test_)?main\\.cpp$")
# Exclude any 'smoke.cpp' regardless of location to avoid a second main() TU
list(FILTER _MORE_TESTS EXCLUDE REGEX ".*/smoke\\.cpp$")
list(APPEND TEST_SOURCES ${_MORE_TESTS})

# ---------- One test executable ----------
add_executable(colony_tests ${TEST_SOURCES})
target_compile_features(colony_tests PRIVATE cxx_std_23)

# --------------------------------------------------------------
# Guardrail: enforce that ONLY test_main.cpp is the doctest runner/implementation TU.
#
# The failure mode we're preventing:
#   If *any* other test .cpp defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN or DOCTEST_CONFIG_IMPLEMENT,
#   you will get ODR / "multiple definition" linker errors.
#
# This guardrail runs at BUILD TIME (not just configure-time), so it catches the problem
# immediately with a clear error even if someone adds the macro to an existing test file
# without re-running CMake.
# --------------------------------------------------------------
set(_doctest_guard_script "${CMAKE_CURRENT_BINARY_DIR}/doctest_runner_guardrail.cmake")
file(WRITE "${_doctest_guard_script}" [=[
    if(NOT DEFINED TESTS_DIR)
        message(FATAL_ERROR "doctest runner guardrail: TESTS_DIR was not provided")
    endif()
    if(NOT DEFINED RUNNER)
        message(FATAL_ERROR "doctest runner guardrail: RUNNER was not provided")
    endif()

    # Normalize path separators for reliable prefix checks (especially on Windows).
    file(TO_CMAKE_PATH "${TESTS_DIR}" _tests_dir_norm)
    file(TO_CMAKE_PATH "${RUNNER}"   _runner_norm)

    file(GLOB_RECURSE _ALL_CPP "${_tests_dir_norm}/*.cpp")

    set(_offenders "")
    set(_runner_has_impl OFF)

    foreach(_src IN LISTS _ALL_CPP)
        file(READ "${_src}" _contents)

        # Match lines that define either doctest implementation macro.
        if(_contents MATCHES "(^|[\\r\\n])[ \\t]*#[ \\t]*define[ \\t]+DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN" OR
           _contents MATCHES "(^|[\\r\\n])[ \\t]*#[ \\t]*define[ \\t]+DOCTEST_CONFIG_IMPLEMENT([ \\t\\r\\n]|$)")
            if(_src STREQUAL "${_runner_norm}")
                set(_runner_has_impl ON)
            else()
                list(APPEND _offenders "${_src}")
            endif()
        endif()
    endforeach()

    if(_offenders)
        list(JOIN _offenders "\n  - " _pretty)
        message(FATAL_ERROR
            "Doctest runner guardrail failed.\n"
            "Only the dedicated runner is allowed to define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN / DOCTEST_CONFIG_IMPLEMENT.\n"
            "Runner:\n"
            "  ${_runner_norm}\n"
            "\n"
            "Remove those defines from:\n"
            "  - ${_pretty}\n"
        )
    endif()

    # If the runner lives inside the tests source tree, ensure it actually contains a doctest implementation macro.
    # (If the runner is generated in the build tree, we assume it is correct.)
    string(FIND "${_runner_norm}" "${_tests_dir_norm}" _prefix_pos)
    if(_prefix_pos EQUAL 0)
        if(NOT _runner_has_impl)
            message(FATAL_ERROR
                "Doctest runner guardrail failed.\n"
                "The configured runner does not define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN or DOCTEST_CONFIG_IMPLEMENT.\n"
                "Runner:\n"
                "  ${_runner_norm}\n"
            )
        endif()
    endif()
]=])

add_custom_target(colony_doctest_runner_guardrail
    COMMAND "${CMAKE_COMMAND}"
        -DTESTS_DIR:PATH="${CMAKE_SOURCE_DIR}/tests"
        -DRUNNER:FILEPATH="${_runner}"
        -P "${_doctest_guard_script}"
    COMMENT "Checking doctest runner uniqueness (only test_main.cpp may implement doctest)"
    VERBATIM
)
add_dependencies(colony_tests colony_doctest_runner_guardrail)

# --------------------------------------------------------------
# Unity builds:
# The root project may enable UNITY_BUILD globally for speed.
# Tests frequently define helper types with common names across multiple .cpp files.
# When unity builds are enabled, multiple test .cpp files can be merged into one TU,
# causing ODR/redefinition errors (e.g., 'TestGrid' defined in test_jps.cpp and test_nav.cpp).
# Disable unity builds for this test target to keep each test .cpp isolated.
# --------------------------------------------------------------
set_target_properties(colony_tests PROPERTIES UNITY_BUILD OFF)

if(MSVC)
    target_compile_options(colony_tests PRIVATE
        /utf-8
        /W4
        /permissive-
        /Zc:__cplusplus
        /MP

        # --- PATCH (G): harden doctest ODR correctness on MSVC ---
        # If DOCTEST_CONFIG_IMPLEMENT* ever leaks in via /D (directory/target/global definitions,
        # imported targets, toolchain files, etc.), cl.exe would compile doctest implementation
        # into multiple .obj files -> LNK2005 multiple definition.
        # Force-undefine both implementation macros on the command line for ALL test TUs.
        # The runner still defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in-source (#define),
        # which /U cannot undo.
        /UDOCTEST_CONFIG_IMPLEMENT
        /UDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )
else()
    target_compile_options(colony_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(colony_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/worldgen
)

# Link production libs if present
if(TARGET colony_build_options)
    target_link_libraries(colony_tests PRIVATE colony_build_options)
endif()
if(TARGET colony_core)
    target_link_libraries(colony_tests PRIVATE colony_core)
endif()
if(TARGET procgen)
    target_link_libraries(colony_tests PRIVATE procgen)
endif()

# ------------------------------------------------------------------
# Link pathfinding into tests (and also the game exe) if available.
# ------------------------------------------------------------------
if(TARGET Colony::Path)
    target_link_libraries(colony_tests PRIVATE Colony::Path)
elseif(TARGET colony_path)
    target_link_libraries(colony_tests PRIVATE colony_path)
elseif(TARGET colony::path)
    target_link_libraries(colony_tests PRIVATE colony::path)
else()
    # OBJECT fallback for CI/dev when the lib isn't defined as a target yet
    file(GLOB_RECURSE COLONY_TEST_PATH_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/pathfinding/*.cpp"
        "${CMAKE_SOURCE_DIR}/hpa/*.cpp"
    )

    if(COLONY_TEST_PATH_SOURCES)
        # --- EXTRA SAFETY ---
        # Avoid accidentally pulling any *_test.cpp / *_tests.cpp / test_*.cpp sources that may
        # live next to production code. Those sometimes include doctest implementation macros
        # and can cause LNK2005 when linked with the main test runner.
        list(FILTER COLONY_TEST_PATH_SOURCES EXCLUDE REGEX ".*/tests?/.*")
        list(FILTER COLONY_TEST_PATH_SOURCES EXCLUDE REGEX ".*/(test_.*|.*_test|.*_tests)\\.cpp$")

        add_library(colony_path_obj OBJECT ${COLONY_TEST_PATH_SOURCES})
        target_include_directories(colony_path_obj PUBLIC
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/pathfinding"
            "${CMAKE_SOURCE_DIR}/hpa"
        )
        target_compile_features(colony_path_obj PUBLIC cxx_std_20)
        target_sources(colony_tests PRIVATE $<TARGET_OBJECTS:colony_path_obj>)
    endif()
endif()

# Also link the main game executable if it exists (safe, conditional).
if(TARGET ColonyGame)
    if(TARGET Colony::Path)
        target_link_libraries(ColonyGame PRIVATE Colony::Path)
    elseif(TARGET colony_path)
        target_link_libraries(ColonyGame PRIVATE colony_path)
    elseif(TARGET colony::path)
        target_link_libraries(ColonyGame PRIVATE colony::path)
    endif()
elseif(TARGET colony_game)
    if(TARGET Colony::Path)
        target_link_libraries(colony_game PRIVATE Colony::Path)
    elseif(TARGET colony_path)
        target_link_libraries(colony_game PRIVATE colony_path)
    elseif(TARGET colony::path)
        target_link_libraries(colony_game PRIVATE colony::path)
    endif()
endif()

# Header-only deps (publish include dirs transitively via INTERFACE)
target_link_libraries(colony_tests PRIVATE pcg doctest)

# ---- Ensure the upstream doctest header is visible on the test target ----
# If we fetched doctest ourselves, add its repo root to the test's includes so
# <doctest/doctest.h> is always found, even with unusual toolchains.
if(DEFINED doctest_upstream_SOURCE_DIR)
    target_include_directories(colony_tests PRIVATE "${doctest_upstream_SOURCE_DIR}")
endif()

# --------------------------------------------------------------
# Test registration with CTest
# Prefer doctest_discover_tests(); fall back to a single add_test()
# --------------------------------------------------------------
if(_DOCTEST_CMAKE_FOUND AND COMMAND doctest_discover_tests)
    # Enumerate each TEST_CASE into its own CTest entry; no reconfigure needed when tests change.
    doctest_discover_tests(colony_tests
        TEST_PREFIX      "tests."
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DISCOVERY_MODE   POST_BUILD
    )
else()
    add_test(NAME colony_tests COMMAND colony_tests)
    set_tests_properties(colony_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

# --------------------------------------------------------------
# Bridge: if the root didn't call enable_testing(), create a root
# CTestTestfile.cmake that forwards to this tests dir so that:
#   ctest --test-dir build -C Release
# still finds the tests. (No effect if root already has one.)
# --------------------------------------------------------------
if(NOT EXISTS "${CMAKE_BINARY_DIR}/CTestTestfile.cmake")
    file(RELATIVE_PATH _rel_from_root "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    file(WRITE "${CMAKE_BINARY_DIR}/CTestTestfile.cmake" "subdirs(\"${_rel_from_root}\")\n")
endif()
