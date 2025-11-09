# cmake/CGShaders.cmake
include_guard(GLOBAL)
include(CMakeParseArguments)

# Ensure simple (modern) expansion rules are used; CMake 3.27+ already defaults to NEW.
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

# Optional manual override for CI/debug:
# cmake -DCOLONY_FXC_PATH="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe"
set(COLONY_FXC_PATH "${COLONY_FXC_PATH}" CACHE FILEPATH "Path to fxc.exe (optional override)")

# --- Locate fxc.exe (SM 5.x compiler for D3D11) -----------------------------------------
function(_cg_find_fxc out_var)
  # 0) Explicit override
  if(COLONY_FXC_PATH AND EXISTS "${COLONY_FXC_PATH}")
    set(${out_var} "${COLONY_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_candidates "")

  # 1) SDK dir chosen by VS toolset
  if(DEFINED ENV{WindowsSdkDir})
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _candidates
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64/fxc.exe")
    endif()
    list(APPEND _candidates "$ENV{WindowsSdkDir}/bin/x64/fxc.exe")
  endif()

  # 2) ProgramFiles(x86) — escape parentheses per CMake docs
  #    https://cmake.org/cmake/help/latest/variable/ENV.html
  set(ProgramFiles_x86 "$ENV{ProgramFiles\(x86\)}")
  if(ProgramFiles_x86)
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _candidates
        "${ProgramFiles_x86}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64/fxc.exe"
        "${ProgramFiles_x86}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64/fxc.exe")
    endif()
    list(APPEND _candidates
      "${ProgramFiles_x86}/Windows Kits/10/bin/x64/fxc.exe"
      "${ProgramFiles_x86}/Windows Kits/11/bin/x64/fxc.exe")

    # Probe all installed SDK versions; prefer highest
    if(EXISTS "${ProgramFiles_x86}/Windows Kits/10/bin")
      file(GLOB _ver10 LIST_DIRECTORIES true "${ProgramFiles_x86}/Windows Kits/10/bin/*")
      list(SORT _ver10 COMPARE NATURAL ORDER DESCENDING)
      foreach(_v IN LISTS _ver10)
        list(APPEND _candidates "${_v}/x64/fxc.exe")
      endforeach()
    endif()
    if(EXISTS "${ProgramFiles_x86}/Windows Kits/11/bin")
      file(GLOB _ver11 LIST_DIRECTORIES true "${ProgramFiles_x86}/Windows Kits/11/bin/*")
      list(SORT _ver11 COMPARE NATURAL ORDER DESCENDING)
      foreach(_v IN LISTS _ver11)
        list(APPEND _candidates "${_v}/x64/fxc.exe")
      endforeach()
    endif()
  endif()

  # 3) PATH as last resort
  find_program(_fxc_prog NAMES fxc)
  if(_fxc_prog)
    set(${out_var} "${_fxc_prog}" PARENT_SCOPE)
    return()
  endif()

  foreach(_p IN LISTS _candidates)
    if(EXISTS "${_p}")
      set(${out_var} "${_p}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

# --- Heuristic: infer SM5 profile from filename suffix ----------------------
function(_cg_guess_profile_from_name src out_profile)
  get_filename_component(_name_we "${src}" NAME_WE)
  set(_p "ps_5_0")
  if(_name_we MATCHES "_vs$")   set(_p "vs_5_0") endif()
  if(_name_we MATCHES "_ps$")   set(_p "ps_5_0") endif()
  if(_name_we MATCHES "_cs$")   set(_p "cs_5_0") endif()
  if(_name_we MATCHES "_gs$")   set(_p "gs_5_0") endif()
  if(_name_we MATCHES "_hs$")   set(_p "hs_5_0") endif()
  if(_name_we MATCHES "_ds$")   set(_p "ds_5_0") endif()
  set(${out_profile} "${_p}" PARENT_SCOPE)
endfunction()

# --- Public API: compile HLSL with FXC into .cso blobs ----------------------
# cg_compile_hlsl(
#   <TARGET_NAME>
#   SHADERS <list of .hlsl>
#   [INCLUDE_DIRS <dirs...>]
#   [DEFINES <defs...>]
# )
function(cg_compile_hlsl target_name)
  if(NOT WIN32)
    message(FATAL_ERROR "cg_compile_hlsl is Windows-only")
  endif()

  set(_opts)
  set(_one SHADERS)
  set(_many INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CG_SHADERS)
    message(WARNING "cg_compile_hlsl: no SHADERS specified")
    add_custom_target(${target_name})
    return()
  endif()

  _cg_find_fxc(FXC_EXE)
  if(NOT FXC_EXE)
    message(FATAL_ERROR
      "cg_compile_hlsl: fxc.exe not found. Install the Windows 10/11 SDK (HLSL compiler for SM 5.x).")
  endif()

  set(_outdir "${CMAKE_BINARY_DIR}/shaders")
  file(MAKE_DIRECTORY "${_outdir}")

  set(_outputs)
  foreach(_src IN LISTS CG_SHADERS)
    get_filename_component(_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_abs}" NAME_WE)

    _cg_guess_profile_from_name("${_abs}" _profile)
    set(_entry "main")
    set(_out   "${_outdir}/${_base}.cso")

    # Config-aware flags using generator expressions (works in multi-config IDEs)
    set(_fxc_flags /nologo /T ${_profile} /E ${_entry} /Fo "${_out}"
                   "$<$<CONFIG:Debug>:/Od>" "$<$<CONFIG:Debug>:/Zi>"
                   "$<$<NOT:$<CONFIG:Debug>>:/O3>")

    foreach(_inc IN LISTS CG_INCLUDE_DIRS)
      list(APPEND _fxc_flags /I "${_inc}")
    endforeach()
    foreach(_def IN LISTS CG_DEFINES)
      list(APPEND _fxc_flags /D "${_def}")
    endforeach()

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
      COMMAND "${FXC_EXE}" ${_fxc_flags} "${_abs}"
      MAIN_DEPENDENCY "${_abs}"
      COMMENT "FXC ${_profile}:${_entry} ${_base}.hlsl -> ${_base}.cso"
      VERBATIM
    )
    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${target_name} DEPENDS ${_outputs})
endfunction()

# --- Public API: wire shader build target to the game target ----------------
function(cg_link_shaders_to_target shader_target game_target)
  if(TARGET ${shader_target} AND TARGET ${game_target})
    add_dependencies(${game_target} ${shader_target})
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION "bin/shaders")
  endif()
endfunction()
