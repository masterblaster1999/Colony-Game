# cmake/InstallRules.cmake
include(GNUInstallDirs)

# Collect executable and shared/library targets automatically.
function(cg_collect_runtime_targets out_var)
  get_property(_all DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_runtimes)
  foreach(t IN LISTS _all)
    get_target_property(_type ${t} TYPE)
    if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
      list(APPEND _runtimes ${t})
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _runtimes)
  set(${out_var} "${_runtimes}" PARENT_SCOPE)
endfunction()

# Version-aware install that works on old & new CMake.
function(cg_install_runtime_targets)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs TARGETS)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_TARGETS)
    message(FATAL_ERROR "cg_install_runtime_targets: pass TARGETS <t1;t2;...>")
  endif()

  if(CMAKE_VERSION VERSION_LESS 3.21)
    # Old CMake: basic install + fix DLLs with BundleUtilities at install time
    install(TARGETS ${CG_TARGETS}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    if(WIN32)
      # Copy and fix up dependent DLLs into the install/bin folder.
      # IMPORTANT: keep ${CMAKE_INSTALL_PREFIX} unexpanded so it works at install-time.
      install(CODE [==[
        include(BundleUtilities)  # ships with CMake
        set(BU_CHMOD_BUNDLE_ITEMS ON)
        file(GLOB _exes "$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/*.exe")
        foreach(_exe IN LISTS _exes)
          message(STATUS "Fixing runtime DLLs for: ${_exe}")
          fixup_bundle("${_exe}" "" "$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin")
        endforeach()
      ]==])
    endif()
  else()
    # Modern CMake: let CMake find/copy runtime DLLs for you
    install(TARGETS ${CG_TARGETS}
      RUNTIME_DEPENDENCIES
        # Skip Windows "API set" shims and system32 DLLs
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
        POST_EXCLUDE_REGEXES ".*/system32/.*\\.dll"
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()
endfunction()
