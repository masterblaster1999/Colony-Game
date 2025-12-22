# pathfinding/CMakeLists.txt
#
# Builds the pathfinding/navigation library used by the game.
# Also applies a Visual Studio/MSBuild fix to ensure per-source .obj outputs are unique
# without embedding drive letters into $(IntDir) (prevents MSB3191).

set(_cg_nav_root "${CMAKE_SOURCE_DIR}")
set(_cg_nav_install_inc "include")

# Collect sources (keep headers in SOURCES so they show up nicely in IDEs)
file(GLOB_RECURSE COLONY_NAV_SOURCES CONFIGURE_DEPENDS
  "${_cg_nav_root}/pathfinding/*.cpp"
  "${_cg_nav_root}/pathfinding/*.c"
  "${_cg_nav_root}/pathfinding/*.hpp"
  "${_cg_nav_root}/pathfinding/*.h"
  "${_cg_nav_root}/hpa/*.cpp"
  "${_cg_nav_root}/hpa/*.c"
  "${_cg_nav_root}/hpa/*.hpp"
  "${_cg_nav_root}/hpa/*.h"
  "${_cg_nav_root}/src/nav/*.cpp"
  "${_cg_nav_root}/src/nav/*.c"
  "${_cg_nav_root}/src/nav/*.hpp"
  "${_cg_nav_root}/src/nav/*.h"
)

list(REMOVE_DUPLICATES COLONY_NAV_SOURCES)

# ------------------------------------------------------------------------------
# Visual Studio/MSBuild: unique object output names WITHOUT %(RelativeDir)
#
# Why: If ClCompile Include="D:\...\file.cpp" is absolute, MSBuild's %(RelativeDir)
# begins with the drive root (e.g. "D:\..."), and combining that with $(IntDir)
# produces invalid paths like "$(IntDir)D:\...".
#
# We generate an object file path using a repo-relative directory + basename.
# ------------------------------------------------------------------------------
function(colony_msvc_unique_objnames_for_sources sources_var)
  if(NOT (MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio"))
    return()
  endif()

  # VS_SETTINGS per-source is supported well in modern CMake (you are already on 4.x in CI).
  if(NOT CMAKE_VERSION VERSION_GREATER_EQUAL "3.22")
    message(STATUS "MSVC unique obj names: CMake < 3.22; skipping per-source ObjectFileName settings.")
    return()
  endif()

  foreach(_src IN LISTS ${sources_var})
    # Only apply to compilation units.
    if(NOT _src MATCHES "\\.(c|cc|cpp|cxx)$")
      continue()
    endif()

    # Normalize to an absolute real path.
    if(IS_ABSOLUTE "${_src}")
      set(_abs "${_src}")
    else()
      set(_abs "${CMAKE_SOURCE_DIR}/${_src}")
    endif()

    if(EXISTS "${_abs}")
      get_filename_component(_abs "${_abs}" REALPATH)
    endif()

    # Compute path relative to the repo root (stable and no drive letter).
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")

    # If something is outside the repo root, fold it under a stable hashed folder.
    if(_rel MATCHES "^\\.\\.")
      string(MD5 _h "${_abs}")
      get_filename_component(_bn "${_src}" NAME)
      set(_rel "_external/${_h}/${_bn}")
    endif()

    get_filename_component(_subdir "${_rel}" DIRECTORY)
    get_filename_component(_base   "${_src}" NAME_WE)

    # Sanitize for MSBuild paths
    string(REPLACE ":" "_" _subdir "${_subdir}")
    string(REPLACE ".." "__" _subdir "${_subdir}")
    string(REPLACE "/" "\\" _subdir_win "${_subdir}")

    if(_subdir_win STREQUAL "." OR _subdir_win STREQUAL "")
      set(_obj "$(IntDir)${_base}.obj")
    else()
      set(_obj "$(IntDir)${_subdir_win}\\${_base}.obj")
    endif()

    # Per-file MSBuild property.
    set_property(SOURCE "${_src}" PROPERTY VS_SETTINGS "ObjectFileName=${_obj}")
  endforeach()
endfunction()

colony_msvc_unique_objnames_for_sources(COLONY_NAV_SOURCES)

# ------------------------------------------------------------------------------
# Target
# ------------------------------------------------------------------------------
add_library(colony_nav STATIC ${COLONY_NAV_SOURCES})
add_library(colony::nav  ALIAS colony_nav)
add_library(colony::path ALIAS colony_nav)

# Public/private include dirs
set(_cg_nav_pub PUBLIC)
target_include_directories(colony_nav ${_cg_nav_pub}
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/pathfinding>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/hpa>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${_cg_nav_install_inc}>
)

# Link to core (and spdlog if your tree still uses it directly here)
target_link_libraries(colony_nav PUBLIC colony::core)

if(DEFINED COLONY_SPDLOG_TARGET AND NOT "${COLONY_SPDLOG_TARGET}" STREQUAL "")
  target_link_libraries(colony_nav PUBLIC ${COLONY_SPDLOG_TARGET})
elseif(TARGET spdlog::spdlog)
  target_link_libraries(colony_nav PUBLIC spdlog::spdlog)
elseif(TARGET spdlog::spdlog_header_only)
  target_link_libraries(colony_nav PUBLIC spdlog::spdlog_header_only)
endif()

set_target_properties(colony_nav PROPERTIES
  FOLDER "Colony/Pathfinding"
)

target_compile_features(colony_nav PUBLIC cxx_std_23)
