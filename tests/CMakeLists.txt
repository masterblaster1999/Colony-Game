# tests/CMakeLists.txt
#
# Builds colony_tests (Windows/MSVC-focused).
#
# Fixes:
#  - CMake 4.x + CMP0169: avoids FetchContent_Populate(<name>) single-arg form
#  - Visual Studio/MSBuild MSB8027: duplicate basenames -> .obj collisions
#  - Visual Studio/MSBuild MSB3191: invalid intermediate dirs when %(RelativeDir) expands to an absolute path
#  - Broken object-library injection ("$" -> proper $<TARGET_OBJECTS:...>)

include(FetchContent)

# ------------------------------------------------------------------------------
# Dependency: doctest (header-only, avoids doctest's own CMakeLists constraints)
# ------------------------------------------------------------------------------
if(NOT TARGET doctest::doctest)
  # NOTE:
  #  We intentionally do NOT call FetchContent_MakeAvailable(doctest)
  #  to avoid bringing doctest's CMake project into ours.
  #
  #  We also intentionally do NOT call FetchContent_Populate(doctest) with ONLY
  #  the name, because under CMP0169 NEW that form is a hard error. We use the
  #  "full details" form, which remains supported.
  set(_doctest_src "${CMAKE_BINARY_DIR}/_deps/doctest-src")
  set(_doctest_bin "${CMAKE_BINARY_DIR}/_deps/doctest-subbuild")

  FetchContent_Populate(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    SOURCE_DIR     "${_doctest_src}"
    BINARY_DIR     "${_doctest_bin}"
  )

  add_library(doctest INTERFACE)
  add_library(doctest::doctest ALIAS doctest)

  # doctest is header-only; include its repo root.
  target_include_directories(doctest INTERFACE "${doctest_SOURCE_DIR}")
endif()

# ------------------------------------------------------------------------------
# Helper: MSB8027 + MSB3191 safe object file naming for Visual Studio generators
#
# Why:
#  - MSB8027 happens when two .cpp files share the same basename and therefore
#    default to the same .obj name in $(IntDir).
#  - A common workaround is ObjectFileName=$(IntDir)%(RelativeDir) ...
#    But if a source is *absolute*, %(RelativeDir) can become "D:\...".
#    That produces invalid paths like "$(IntDir)D:\..." -> MSB3191.
#
# Fix:
#  - For each source, compute a *repo-relative* subdir using CMake, and set
#    ObjectFileName explicitly to "$(IntDir)<repo-relative-dir>\<basename>.obj".
#  - If the file is outside the repo root, put it under a stable hashed folder
#    inside IntDir to avoid ".." escaping.
# ------------------------------------------------------------------------------
function(colony_msvc_unique_objnames)
  if(NOT (MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio"))
    return()
  endif()

  foreach(_src IN LISTS ARGN)
    if(NOT _src MATCHES "\\.(c|cc|cpp|cxx)$")
      continue()
    endif()

    get_filename_component(_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_abs}" NAME_WE)

    # Compute path relative to repo root
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")

    # Normalize to forward slashes for checks
    string(REPLACE "\\" "/" _rel "${_rel}")

    # If outside repo, use a hashed folder under IntDir to keep outputs contained
    if(_rel MATCHES "^\\.\\./" OR _rel MATCHES "^\\.\\.$" OR _rel MATCHES "^\\.\\.")
      string(MD5 _hash "${_abs}")
      set(_subdir "_external/${_hash}")
    else()
      get_filename_component(_subdir "${_rel}" DIRECTORY)
    endif()

    # Sanitize just in case
    string(REPLACE ":" "_" _subdir "${_subdir}")
    string(REPLACE ".." "__" _subdir "${_subdir}")

    # Back to Windows slashes for MSBuild
    string(REPLACE "/" "\\" _subdir_win "${_subdir}")

    if(_subdir_win STREQUAL "" OR _subdir_win STREQUAL ".")
      set(_obj "$(IntDir)${_base}.obj")
    else()
      set(_obj "$(IntDir)${_subdir_win}\\${_base}.obj")
    endif()

    # CMake 3.22+ honors VS_SETTINGS for C/C++ files
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.22")
      set_property(SOURCE "${_src}" PROPERTY VS_SETTINGS "ObjectFileName=${_obj}")
    else()
      # Fallback: explicit /Fo path (rarely needed for you, but safe)
      set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS "/Fo${_obj}")
    endif()
  endforeach()
endfunction()

# ------------------------------------------------------------------------------
# Test sources
# ------------------------------------------------------------------------------
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
)

if(NOT TEST_SOURCES)
  message(FATAL_ERROR "colony_tests: no test sources found in ${CMAKE_CURRENT_LIST_DIR}")
endif()

# Apply unique obj naming to test sources
colony_msvc_unique_objnames(${TEST_SOURCES})

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

# ------------------------------------------------------------------------------
# Worldgen implementation fallback (fixes unresolved externals in tests)
# ------------------------------------------------------------------------------
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
  # Apply unique obj naming to worldgen sources too (prevents MSB8027 in OBJECT lib)
  colony_msvc_unique_objnames(${COLONY_WORLDGEN_SOURCES})

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

  # Inject the compiled object files into the test executable
  target_sources(colony_tests PRIVATE
    $<TARGET_OBJECTS:colony_worldgen_obj>
  )
endif()

# Optional repo helper hooks (only if defined)
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(colony_tests)
endif()
if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(colony_tests)
endif()
