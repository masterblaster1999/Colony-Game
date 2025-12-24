# cmake/ColonyBuildHelpers.cmake
# --------------------------------------------------------------------------------------------------
# Small helper commands that multiple subdirectories expect (colony_*).
#
# Keep this file lightweight and Windows/MSVC-friendly.
# --------------------------------------------------------------------------------------------------

include_guard(GLOBAL)

# Enable a consistent warnings set (MSVC-focused).
function(colony_enable_warnings target)
  if(MSVC)
    if(COMMAND enable_msvc_warnings)
      enable_msvc_warnings(${target})
    else()
      target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()

    if(DEFINED COLONY_WARNINGS_AS_ERRORS AND COLONY_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  endif()
endfunction()

# Apply the MSVC runtime library selection in a centralized way.
# Default: dynamic CRT (/MD, /MDd), matching vcpkg's x64-windows triplet defaults.
#
# Note: This relies on CMake policy CMP0091 being NEW before the first project()/enable_language().
# See: https://cmake.org/cmake/help/latest/policy/CMP0091.html
function(colony_apply_msvc_runtime target)
  if(NOT MSVC)
    return()
  endif()

  # Equivalent to /MD (Release) and /MDd (Debug).
  set_property(TARGET ${target} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endfunction()

# Thin wrapper for target_precompile_headers() with a nicer call site.
function(colony_enable_pch target visibility header)
  if(NOT EXISTS "${header}")
    message(WARNING "colony_enable_pch: PCH header not found: ${header} (target: ${target})")
    return()
  endif()

  target_precompile_headers(${target} ${visibility} "${header}")
endfunction()

# CrashDumpWin.cpp often needs /EHa on MSVC to reliably catch SEH exceptions around MiniDump creation.
# Apply it (and disable PCH/unity for that TU) if it exists in the target.
function(colony_apply_seh_for_crashdump target)
  if(NOT MSVC)
    return()
  endif()

  get_target_property(_cg_sources ${target} SOURCES)
  if(NOT _cg_sources)
    return()
  endif()

  foreach(_cg_src IN LISTS _cg_sources)
    get_filename_component(_cg_name "${_cg_src}" NAME)
    if(_cg_name STREQUAL "CrashDumpWin.cpp")
      set_property(SOURCE "${_cg_src}" APPEND PROPERTY COMPILE_OPTIONS "/EHa")
      set_source_files_properties("${_cg_src}"
        PROPERTIES
          SKIP_PRECOMPILE_HEADERS ON
          SKIP_UNITY_BUILD_INCLUSION ON
      )
    endif()
  endforeach()
endfunction()
