# core/CMakeLists.txt
# Target: colony_core (colony::core)

set(_CG_CORE_DIRS
  "${CMAKE_SOURCE_DIR}/core"
  "${CMAKE_SOURCE_DIR}/src/core"
)

set(COLONY_CORE_SOURCES "")
foreach(_d IN LISTS _CG_CORE_DIRS)
  if(EXISTS "${_d}")
    file(GLOB_RECURSE _tmp CONFIGURE_DEPENDS
      "${_d}/*.c"
      "${_d}/*.cc"
      "${_d}/*.cpp"
      "${_d}/*.cxx"
    )
    list(APPEND COLONY_CORE_SOURCES ${_tmp})
  endif()
endforeach()
list(REMOVE_DUPLICATES COLONY_CORE_SOURCES)

if(COLONY_CORE_SOURCES)
  add_library(colony_core STATIC)
  target_sources(colony_core PRIVATE ${COLONY_CORE_SOURCES})
else()
  # Allow header-only core if you really have no .cpp in these folders.
  add_library(colony_core INTERFACE)
  message(STATUS "colony_core: no sources found in core/ or src/core/; created INTERFACE target.")
endif()

add_library(colony::core ALIAS colony_core)
set_target_properties(colony_core PROPERTIES FOLDER "libs")

get_target_property(_cg_core_type colony_core TYPE)
if(_cg_core_type STREQUAL "INTERFACE_LIBRARY")
  set(_cg_core_pub INTERFACE)
else()
  set(_cg_core_pub PUBLIC)
endif()

target_compile_features(colony_core ${_cg_core_pub} cxx_std_23)

target_include_directories(colony_core ${_cg_core_pub}
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:${COLONY_INSTALL_INCLUDEDIR}>"
)

# PCH (CMake target_precompile_headers) :contentReference[oaicite:6]{index=6}
if(COLONY_USE_PCH AND NOT _cg_core_type STREQUAL "INTERFACE_LIBRARY")
  target_precompile_headers(colony_core PRIVATE "${COLONY_PCH_HEADER}")
endif()

if(NOT _cg_core_type STREQUAL "INTERFACE_LIBRARY")
  colony_apply_msvc_runtime(colony_core)
  colony_apply_debug_runtime_checks(colony_core)
  colony_apply_seh_for_crashdump(colony_core)
endif()

unset(_cg_core_type)
unset(_cg_core_pub)
