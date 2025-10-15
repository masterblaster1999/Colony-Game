# cmake/CGSummary.cmake
include_guard(GLOBAL)

message(STATUS "================ ColonyGame Configuration ================")
message(STATUS "C++ Standard          : C++${CMAKE_CXX_STANDARD}")
if(WIN32)
  # From CGShaders, if called
  if(DEFINED _CG_HLSL_TOOLCHAIN)
    set(_HLSL_TC "${_CG_HLSL_TOOLCHAIN}")
  elseif(DEFINED CG_HLSL_STATUS)
    set(_HLSL_TC "${CG_HLSL_STATUS}")
  else()
    set(_HLSL_TC "n/a")
  endif()
  message(STATUS "Renderer              : ${COLONY_RENDERER}  [HLSL SM ${COLONY_HLSL_MODEL}; ${_HLSL_TC}]")
else()
  message(STATUS "Renderer              : ${COLONY_RENDERER}")
endif()
message(STATUS "Frontend              : ${FRONTEND}")
message(STATUS "PCH enabled           : ${COLONY_USE_PCH}")
message(STATUS "Unity build           : ${COLONY_UNITY_BUILD}")
message(STATUS "Warnings as errors    : ${COLONY_WARNINGS_AS_ERRORS}")
message(STATUS "LTO/IPO (Release)     : ${CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE}")
message(STATUS "ASan (Debug)          : ${COLONY_ASAN}")
# Keep final line tidy:
message(STATUS "==========================================================")

# Optional: reiterate MSVC PDB control if using MSVC
if (MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:RelWithDebInfo,Debug>:ProgramDatabase>")
endif()
