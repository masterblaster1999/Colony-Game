# cmake/CGSummary.cmake
include_guard(GLOBAL)

# Print a small, stable configuration summary.
# This is called explicitly from the top-level CMakeLists.txt.
function(cg_print_config_summary)
  message(STATUS "================ ColonyGame Configuration ================")
  message(STATUS "C++ Standard          : C++${CMAKE_CXX_STANDARD}")

  # Renderer / shader toolchain info (some variables are defined only when the shader
  # toolchain module runs, so we guard them to avoid empty / confusing output).
  if(DEFINED COLONY_RENDERER AND NOT COLONY_RENDERER STREQUAL "")
    set(_CG_RENDERER "${COLONY_RENDERER}")
  else()
    set(_CG_RENDERER "n/a")
  endif()

  if(DEFINED COLONY_HLSL_MODEL AND NOT COLONY_HLSL_MODEL STREQUAL "")
    set(_CG_HLSL_MODEL "${COLONY_HLSL_MODEL}")
  else()
    set(_CG_HLSL_MODEL "n/a")
  endif()

  if(WIN32)
    if(DEFINED _CG_HLSL_TOOLCHAIN)
      set(_CG_HLSL_TC "${_CG_HLSL_TOOLCHAIN}")
    elseif(DEFINED CG_HLSL_STATUS)
      set(_CG_HLSL_TC "${CG_HLSL_STATUS}")
    else()
      set(_CG_HLSL_TC "n/a")
    endif()
    message(STATUS "Renderer              : ${_CG_RENDERER}  [HLSL SM ${_CG_HLSL_MODEL}; ${_CG_HLSL_TC}]")
  else()
    message(STATUS "Renderer              : ${_CG_RENDERER}")
  endif()

  if(DEFINED FRONTEND AND NOT FRONTEND STREQUAL "")
    message(STATUS "Frontend              : ${FRONTEND}")
  else()
    message(STATUS "Frontend              : Win32")
  endif()

  if(DEFINED COLONY_USE_PCH)
    message(STATUS "PCH enabled           : ${COLONY_USE_PCH}")
  else()
    message(STATUS "PCH enabled           : OFF")
  endif()

  if(DEFINED COLONY_UNITY_BUILD)
    message(STATUS "Unity build           : ${COLONY_UNITY_BUILD}")
  else()
    message(STATUS "Unity build           : OFF")
  endif()

  if(DEFINED COLONY_WERROR)
    message(STATUS "Warnings as errors    : ${COLONY_WERROR}")
  elseif(DEFINED COLONY_WARNINGS_AS_ERRORS)
    message(STATUS "Warnings as errors    : ${COLONY_WARNINGS_AS_ERRORS}")
  else()
    message(STATUS "Warnings as errors    : OFF")
  endif()

  message(STATUS "LTO/IPO (Release)     : ${CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE}")

  if(DEFINED COLONY_ASAN)
    message(STATUS "ASan (Debug)          : ${COLONY_ASAN}")
  else()
    message(STATUS "ASan (Debug)          : OFF")
  endif()

  message(STATUS "==========================================================")
endfunction()
