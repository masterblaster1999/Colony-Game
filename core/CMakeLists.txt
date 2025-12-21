# core/CMakeLists.txt
# Target: colony_core (colony::core)

if(NOT WIN32)
  message(FATAL_ERROR "colony_core is Windows-only in this project configuration.")
endif()

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

# -----------------------------------------------------------------------------
# Prevent MSB8027: MSBuild/VS object output collisions for duplicate basenames.
#
# Your repo currently includes both:
#   core/Application.cpp
#   src/core/Application.cpp
# (and potentially other duplicates).
#
# Strategy:
#   1) Prefer files under core/ over src/core/ when basenames match.
#   2) After that, enforce unique basenames in the final source list.
# -----------------------------------------------------------------------------
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" _cg_root)
set(_cg_core_prefix     "${_cg_root}/core/")
set(_cg_src_core_prefix "${_cg_root}/src/core/")

# Collect basenames that exist in preferred override dir (core/)
set(_cg_core_basenames "")
foreach(_s IN LISTS COLONY_CORE_SOURCES)
  string(FIND "${_s}" "${_cg_core_prefix}" _pos_core)
  if(_pos_core EQUAL 0)
    get_filename_component(_base "${_s}" NAME)
    list(APPEND _cg_core_basenames "${_base}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _cg_core_basenames)

# Build final list while skipping overridden src/core duplicates, and
# hard-error on any remaining duplicate basenames.
set(_cg_final_sources "")
set(_cg_used_basenames "")

foreach(_s IN LISTS COLONY_CORE_SOURCES)
  get_filename_component(_base "${_s}" NAME)

  # Prefer core/ over src/core/ when basenames match.
  string(FIND "${_s}" "${_cg_src_core_prefix}" _pos_src)
  if(_pos_src EQUAL 0)
    list(FIND _cg_core_basenames "${_base}" _idx)
    if(_idx GREATER -1)
      message(STATUS "colony_core: skipping duplicate '${_s}' (overridden by core/${_base})")
      continue()
    endif()
  endif()

  # Enforce unique basenames in the final list.
  list(FIND _cg_used_basenames "${_base}" _seen)
  if(_seen GREATER -1)
    message(FATAL_ERROR
      "colony_core: duplicate source basename '${_base}' detected.\n"
      "This breaks MSBuild outputs (MSB8027).\n"
      "Rename one file or adjust core/CMakeLists.txt.\n"
      "Offending path: ${_s}"
    )
  endif()

  list(APPEND _cg_used_basenames "${_base}")
  list(APPEND _cg_final_sources "${_s}")
endforeach()

set(COLONY_CORE_SOURCES "${_cg_final_sources}")

unset(_cg_root)
unset(_cg_core_prefix)
unset(_cg_src_core_prefix)
unset(_cg_core_basenames)
unset(_cg_final_sources)
unset(_cg_used_basenames)

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
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>"
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:${COLONY_INSTALL_INCLUDEDIR}>"
)

# PCH (CMake target_precompile_headers)
if(COLONY_USE_PCH AND NOT _cg_core_type STREQUAL "INTERFACE_LIBRARY")
  target_precompile_headers(colony_core PRIVATE "${COLONY_PCH_HEADER}")
endif()

if(NOT _cg_core_type STREQUAL "INTERFACE_LIBRARY")
  colony_apply_msvc_runtime(colony_core)
  colony_apply_debug_runtime_checks(colony_core)
  colony_apply_seh_for_crashdump(colony_core)

  # Optional but recommended: keep app entry / win32-heavy files out of unity builds.
  if(EXISTS "${CMAKE_SOURCE_DIR}/core/Application.cpp")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/core/Application.cpp"
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/core/Application.cpp")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/core/Application.cpp"
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
endif()

unset(_cg_core_type)
unset(_cg_core_pub)
