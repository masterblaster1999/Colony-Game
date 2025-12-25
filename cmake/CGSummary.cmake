# cmake/CGSummary.cmake
#
# Optional configure-time summary printing.
#
# The top-level CMakeLists.txt calls:
#   include(CGSummary)
#   cg_print_config_summary()
#
# Previous iterations of this repo printed the summary at *include time*.
# The project now calls a function instead, so CI/configure scripts can
# control when (and if) the summary is emitted.

include_guard(GLOBAL)

function(cg_print_config_summary)
  # --- HLSL / renderer toolchain summary (if available) ----------------------
  set(_HLSL_TC "n/a")
  if(DEFINED _CG_HLSL_TOOLCHAIN)
    set(_HLSL_TC "${_CG_HLSL_TOOLCHAIN}")
  elseif(DEFINED CG_HLSL_STATUS)
    set(_HLSL_TC "${CG_HLSL_STATUS}")
  endif()

  # Renderer (best-effort; the prototype build is D3D11 today).
  set(_renderer "d3d11")
  if(DEFINED COLONY_RENDERER AND NOT "${COLONY_RENDERER}" STREQUAL "")
    set(_renderer "${COLONY_RENDERER}")
  endif()

  # Shader model (optional; try to infer from profile if available).
  set(_hlsl_sm "")
  if(DEFINED COLONY_HLSL_MODEL AND NOT "${COLONY_HLSL_MODEL}" STREQUAL "")
    set(_hlsl_sm "${COLONY_HLSL_MODEL}")
  elseif(DEFINED COLONY_VS_PROFILE AND "${COLONY_VS_PROFILE}" MATCHES "_([0-9]+_[0-9]+)$")
    string(REGEX REPLACE ".*_([0-9]+_[0-9]+)$" "\\1" _hlsl_sm "${COLONY_VS_PROFILE}")
  endif()

  # Frontend: this repo historically uses "" to mean native Win32.
  set(_frontend "${FRONTEND}")
  if("${_frontend}" STREQUAL "")
    set(_frontend "win32")
  endif()

  # Options (print something even when a cache variable isn't defined).
  set(_pch "OFF")
  if(DEFINED COLONY_USE_PCH AND COLONY_USE_PCH)
    set(_pch "ON")
  endif()

  set(_unity "OFF")
  if(DEFINED COLONY_UNITY_BUILD AND COLONY_UNITY_BUILD)
    set(_unity "ON")
  endif()

  set(_werror_core "OFF")
  if(DEFINED COLONY_WERROR AND COLONY_WERROR)
    set(_werror_core "ON")
  endif()

  set(_werror_global "OFF")
  if(DEFINED COLONY_WARNINGS_AS_ERRORS AND COLONY_WARNINGS_AS_ERRORS)
    set(_werror_global "ON")
  endif()

  set(_lto "OFF")
  if(DEFINED COLONY_LTO AND COLONY_LTO)
    set(_lto "ON")
  elseif(DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE AND CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE)
    set(_lto "ON")
  endif()

  set(_asan "OFF")
  if(DEFINED COLONY_ENABLE_ASAN AND COLONY_ENABLE_ASAN)
    set(_asan "ON")
  elseif(DEFINED COLONY_ASAN AND COLONY_ASAN)
    # Back-compat for older scripts.
    set(_asan "ON")
  endif()

  message(STATUS "")
  message(STATUS "==========================================================")
  message(STATUS "C++ Standard          : C++${CMAKE_CXX_STANDARD}")
  if(NOT "${_hlsl_sm}" STREQUAL "")
    message(STATUS "Renderer              : ${_renderer}  [HLSL SM ${_hlsl_sm}; ${_HLSL_TC}]")
  else()
    message(STATUS "Renderer              : ${_renderer}")
  endif()
  message(STATUS "Frontend              : ${_frontend}")
  message(STATUS "PCH enabled           : ${_pch}")
  message(STATUS "Unity build           : ${_unity}")
  message(STATUS "Warnings as errors    : core=${_werror_core}, global=${_werror_global}")
  message(STATUS "LTO/IPO (Release)     : ${_lto}")
  message(STATUS "ASan (Debug)          : ${_asan}")
  message(STATUS "==========================================================")
endfunction()

# Ensure RelWithDebInfo + Debug produce PDBs (VS); keeps crashdumps useful.
if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:RelWithDebInfo,Debug>:ProgramDatabase>")
endif()
