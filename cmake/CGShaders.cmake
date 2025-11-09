# cmake/CGShaders.cmake
include_guard(GLOBAL)
include(CMakeParseArguments)

# Modern expansion/escaping (needed for $ENV{ProgramFiles(x86)}).
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

# Options / tool overrides (helpful for CI and local overrides)
option(CG_SHADERS_USE_DXC "Use DXC (SM6/DXIL). If OFF, use FXC (SM5.x/DXBC)" OFF)
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension")
set(COLONY_FXC_PATH "" CACHE FILEPATH "Full path to fxc.exe (optional)")
set(COLONY_DXC_PATH "" CACHE FILEPATH "Full path to dxc.exe (optional)")

# Normalize common Windows paths
set(_WSDK "")
if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
  file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" _WSDK)
endif()

set(_PF86 "")
if(DEFINED ENV{ProgramFiles(x86)})
  # Escape parentheses as documented by CMake (requires CMP0053 NEW).
  set(_PF86 "$ENV{ProgramFiles\(x86\)}")
  file(TO_CMAKE_PATH "${_PF86}" _PF86)
endif()

# Try to infer an SDK version for versioned bin paths
set(_WINSDK_VER "")
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
  set(_WINSDK_VER "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
elseif(_WSDK)
  file(GLOB _sdk_bins LIST_DIRECTORIES TRUE "${_WSDK}/bin/*")
  list(SORT _sdk_bins COMPARE NATURAL ORDER DESCENDING)
  if(_sdk_bins)
    list(GET _sdk_bins 0 _sdk_bin_top)
    get_filename_component(_WINSDK_VER "${_sdk_bin_top}" NAME)
  endif()
endif()

# ---------- Tool discovery ----------
function(_cg_find_fxc OUT_EXE)
  if(NOT WIN32)
    message(FATAL_ERROR "FXC is Windows-only")
  endif()
  if(COLONY_FXC_PATH AND EXISTS "${COLONY_FXC_PATH}")
    set(${OUT_EXE} "${COLONY_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()
  set(_hints "")
  if(_WSDK)
    if(_WINSDK_VER) list(APPEND _hints "${_WSDK}/bin/${_WINSDK_VER}/x64") endif()
    list(APPEND _hints "${_WSDK}/bin/x64")
  endif()
  if(_PF86)
    if(_WINSDK_VER)
      list(APPEND _hints "${_PF86}/Windows Kits/10/bin/${_WINSDK_VER}/x64"
                         "${_PF86}/Windows Kits/11/bin/${_WINSDK_VER}/x64")
    endif()
    list(APPEND _hints "${_PF86}/Windows Kits/10/bin/x64"
                       "${_PF86}/Windows Kits/11/bin/x64")
  endif()
  list(APPEND _hints "C:/Program Files (x86)/Windows Kits/10/bin/x64")
  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_hints} PATH_SUFFIXES x64)
  if(NOT FXC_EXE)
    find_program(FXC_EXE NAMES fxc fxc.exe) # PATH last
  endif()
  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install Windows 10/11 SDK.")
  endif()
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

function(_cg_find_dxc OUT_EXE)
  if(COLONY_DXC_PATH AND EXISTS "${COLONY_DXC_PATH}")
    set(${OUT_EXE} "${COLONY_DXC_PATH}" PARENT_SCOPE)
    return()
  endif()
  set(_hints "")
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _hints
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(DEFINED ENV{DXC_DIR} AND NOT "$ENV{DXC_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{DXC_DIR}" _DXC_DIR)
    list(APPEND _hints "${_DXC_DIR}")
  endif()
  if(_WSDK)
    if(_WINSDK_VER) list(APPEND _hints "${_WSDK}/bin/${_WINSDK_VER}/x64") endif()
    list(APPEND _hints "${_WSDK}/bin/x64")
  endif()
  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_hints} PATH_SUFFIXES x64)
  if(NOT DXC_EXE)
    find_program(DXC_EXE NAMES dxc dxc.exe) # PATH last
  endif()
  if(NOT DXC_EXE)
    message(STATUS "dxc.exe not found; using FXC for SM 5.x.")
  endif()
  set(${OUT_EXE} "${DXC_EXE}" PARENT_SCOPE)
endfunction()

# ---------- Helpers ----------
function(_cg_guess_profile_from_name SRC OUT_PROFILE)
  get_filename_component(_name "${SRC}" NAME_WE)
  set(_stage "ps")
  if(_name MATCHES "_vs$") set(_stage "vs") endif()
  if(_name MATCHES "_ps$") set(_stage "ps") endif()
  if(_name MATCHES "_cs$") set(_stage "cs") endif()
  if(_name MATCHES "_gs$") set(_stage "gs") endif()
  if(_name MATCHES "_hs$") set(_stage "hs") endif()
  if(_name MATCHES "_ds$") set(_stage "ds") endif()
  if(CG_SHADERS_USE_DXC)  # SM6+ for future DX12 work
    set(_sm "6_6")
  else()                  # SM5.x for D3D11
    set(_sm "5_0")
  endif()
  set(${OUT_PROFILE} "${_stage}_${_sm}" PARENT_SCOPE)
endfunction()

function(_cg_build_args PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(CG_SHADERS_USE_DXC) list(APPEND _res "-I" "${inc}")
    else()                  list(APPEND _res "/I${inc}")
    endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(CG_SHADERS_USE_DXC) list(APPEND _res "-D" "${def}")
    else()                  list(APPEND _res "/D${def}")
    endif()
  endforeach()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

function(_cg_collect_header_deps OUT_LIST)
  set(_deps "")
  foreach(inc IN LISTS ARGN)
    file(GLOB_RECURSE _h CONFIGURE_DEPENDS
      "${inc}/*.hlsli" "${inc}/*.fxh" "${inc}/*.hlslinc" "${inc}/*.h")
    list(APPEND _deps ${_h})
  endforeach()
  set(${OUT_LIST} "${_deps}" PARENT_SCOPE)
endfunction()

# ---------- Public API ----------
# cg_compile_hlsl(Target  SHADERS <.hlsl ...>
#                 [INCLUDE_DIRS <dir...>] [DEFINES <def...>] [OUTPUT_DIR <dir>])
function(cg_compile_hlsl TARGET_NAME)
  set(_opts)
  set(_one SHADERS OUTPUT_DIR)
  set(_many INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CG_SHADERS)
    message(WARNING "cg_compile_hlsl: no SHADERS specified")
    add_custom_target(${TARGET_NAME})
    return()
  endif()

  if(CG_SHADERS_USE_DXC)
    _cg_find_dxc(DXC_EXE)
    if(NOT DXC_EXE) message(FATAL_ERROR "DXC requested but not found") endif()
  else()
    _cg_find_fxc(FXC_EXE)
  endif()

  if(NOT CG_OUTPUT_DIR)
    set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  _cg_build_args(CG _extra_args)
  _cg_collect_header_deps(_hdrs ${CG_INCLUDE_DIRS})

  set(_outputs)
  foreach(_src IN LISTS CG_SHADERS)
    get_filename_component(_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_src}" NAME_WE)
    get_source_file_property(_entry_prop "${_src}" HLSL_ENTRY)
    set(_entry "main")
    if(NOT _entry_prop STREQUAL "NOTFOUND" AND _entry_prop)
      set(_entry "${_entry_prop}")
    endif()
    _cg_guess_profile_from_name("${_abs}" _profile)
    set(_out "${CG_OUTPUT_DIR}/${_base}.${CG_SHADER_OUTPUT_EXT}")

    if(CG_SHADERS_USE_DXC)
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
        COMMAND "${DXC_EXE}"
                -nologo -T "${_profile}" -E "${_entry}"
                "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-Qembed_debug>" "$<$<CONFIG:Debug>:-Od>"
                "$<$<NOT:$<CONFIG:Debug>>:-O3>" "$<$<NOT:$<CONFIG:Debug>>:-Qstrip_debug>" "$<$<NOT:$<CONFIG:Debug>>:-Qstrip_reflect>"
                ${_extra_args} -Fo "${_out}" "${_abs}"
        MAIN_DEPENDENCY "${_abs}" DEPENDS ${_hdrs}
        VERBATIM
        COMMENT "DXC ${_profile} ${_base}.hlsl -> ${_base}.${CG_SHADER_OUTPUT_EXT}")
    else()
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
        COMMAND "${FXC_EXE}"
                /nologo /T "${_profile}" /E "${_entry}"
                "$<$<CONFIG:Debug>:/Zi>" "$<$<CONFIG:Debug>:/Od>"
                "$<$<NOT:$<CONFIG:Debug>>:/O3>"
                ${_extra_args} /Fo "${_out}" "${_abs}"
        MAIN_DEPENDENCY "${_abs}" DEPENDS ${_hdrs}
        VERBATIM
        COMMENT "FXC ${_profile} ${_base}.hlsl -> ${_base}.${CG_SHADER_OUTPUT_EXT}")
    endif()

    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS "${_outputs}")
endfunction()

function(cg_link_shaders_to_target SHADER_TARGET RUNTIME_TARGET)
  if(TARGET ${SHADER_TARGET} AND TARGET ${RUNTIME_TARGET})
    get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
    if(NOT _outdir)
      message(FATAL_ERROR "cg_link_shaders_to_target: ${SHADER_TARGET} has no CG_SHADER_OUTPUT_DIR")
    endif()
    add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${RUNTIME_TARGET}>/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "$<TARGET_FILE_DIR:${RUNTIME_TARGET}>/shaders"
      COMMENT "Copying shaders to runtime directory" VERBATIM)
    install(DIRECTORY "${_outdir}/" DESTINATION "bin/shaders")
    add_dependencies(${RUNTIME_TARGET} ${SHADER_TARGET})
  endif()
endfunction()
