# pathfinding/CMakeLists.txt
# Target: colony_nav (colony::nav, colony::path)
# Sources come from pathfinding/, hpa/, and src/nav (Navigator / ClusterGrid).

set(COLONY_NAV_SOURCES "")

if(EXISTS "${CMAKE_SOURCE_DIR}/pathfinding")
  file(GLOB_RECURSE _pf CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/pathfinding/*.c"
    "${CMAKE_SOURCE_DIR}/pathfinding/*.cpp"
    "${CMAKE_SOURCE_DIR}/pathfinding/*.cc"
    "${CMAKE_SOURCE_DIR}/pathfinding/*.cxx"
  )
  list(APPEND COLONY_NAV_SOURCES ${_pf})
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/hpa")
  file(GLOB_RECURSE _hpa CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/hpa/*.c"
    "${CMAKE_SOURCE_DIR}/hpa/*.cpp"
    "${CMAKE_SOURCE_DIR}/hpa/*.cc"
    "${CMAKE_SOURCE_DIR}/hpa/*.cxx"
  )
  list(APPEND COLONY_NAV_SOURCES ${_hpa})
endif()

# IMPORTANT: The repo also has src/nav/* which contains colony::nav implementations
# used by tests (Navigator, ClusterGrid, etc.).
if(EXISTS "${CMAKE_SOURCE_DIR}/src/nav")
  file(GLOB_RECURSE _src_nav CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/nav/*.c"
    "${CMAKE_SOURCE_DIR}/src/nav/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/nav/*.cc"
    "${CMAKE_SOURCE_DIR}/src/nav/*.cxx"
  )
  list(APPEND COLONY_NAV_SOURCES ${_src_nav})
endif()

list(REMOVE_DUPLICATES COLONY_NAV_SOURCES)

# MSB8027 fix (VS/MSVC): unique object output directory per source file.
# MSBuild recommendation: ObjectFileName = $(IntDir)%(RelativeDir)
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
  foreach(_src IN LISTS COLONY_NAV_SOURCES)
    if(_src MATCHES "\\.(c|cc|cpp|cxx)$")
      set_property(SOURCE "${_src}" PROPERTY VS_SETTINGS "ObjectFileName=$(IntDir)%(RelativeDir)")
    endif()
  endforeach()
endif()

if(COLONY_NAV_SOURCES)
  add_library(colony_nav STATIC ${COLONY_NAV_SOURCES})
else()
  add_library(colony_nav INTERFACE)
  message(STATUS "colony_nav: no sources found; created INTERFACE target.")
endif()

add_library(colony::nav ALIAS colony_nav)
add_library(colony::path ALIAS colony_nav)

set_target_properties(colony_nav PROPERTIES FOLDER "libs")

get_target_property(_cg_nav_type colony_nav TYPE)
if(_cg_nav_type STREQUAL "INTERFACE_LIBRARY")
  set(_cg_nav_pub INTERFACE)
else()
  set(_cg_nav_pub PUBLIC)
endif()

target_compile_features(colony_nav ${_cg_nav_pub} cxx_std_23)

target_include_directories(colony_nav ${_cg_nav_pub}
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/pathfinding>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/hpa>"
  "$<INSTALL_INTERFACE:${COLONY_INSTALL_INCLUDEDIR}>"
)

if(TARGET colony::core)
  target_link_libraries(colony_nav ${_cg_nav_pub} colony::core)
elseif(TARGET colony_core)
  target_link_libraries(colony_nav ${_cg_nav_pub} colony_core)
endif()

# Optional PCH (safe path handling)
if(COLONY_USE_PCH AND NOT _cg_nav_type STREQUAL "INTERFACE_LIBRARY")
  if(DEFINED COLONY_PCH_HEADER)
    if(IS_ABSOLUTE "${COLONY_PCH_HEADER}")
      set(_cg_pch "${COLONY_PCH_HEADER}")
    else()
      set(_cg_pch "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
    endif()

    if(EXISTS "${_cg_pch}")
      target_precompile_headers(colony_nav PRIVATE "${_cg_pch}")
    endif()
  endif()
endif()

if(NOT _cg_nav_type STREQUAL "INTERFACE_LIBRARY")
  if(COMMAND colony_apply_msvc_runtime)
    colony_apply_msvc_runtime(colony_nav)
  endif()
  if(COMMAND colony_apply_debug_runtime_checks)
    colony_apply_debug_runtime_checks(colony_nav)
  endif()
endif()

unset(_cg_nav_type)
unset(_cg_nav_pub)
unset(_cg_pch)
