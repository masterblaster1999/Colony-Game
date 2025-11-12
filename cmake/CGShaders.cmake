# cmake/CGShaders.cmake
# Windows-only HLSL build glue (DXC-first with FXC fallback). Requires CMake >= 3.20.
# Exposes:
#   cg_compile_hlsl(Tgt SHADERS ... [INCLUDE_DIRS ...] [DEFINES ...] [OUTPUT_DIR <dir>] [EMBED])
#   cg_link_shaders_to_target(ShaderTarget ExecutableTarget)
#   cg_set_hlsl_properties(file.hlsl ENTRY main PROFILE ps_5_0 [DEFINES ...] [INCLUDE_DIRS ...] [FLAGS ...])

include_guard(GLOBAL)
include(CMakeParseArguments)

if(NOT WIN32)
  message(STATUS "CGShaders.cmake: non-Windows host; shader build helpers are inert.")
  function(cg_compile_hlsl) endfunction()
  function(cg_link_shaders_to_target) endfunction()
  function(cg_set_hlsl_properties) endfunction()
  return()
endif()

# --- Options -------------------------------------------------------------------
option(CG_SHADERS_WARNINGS_AS_ERRORS "Treat shader compiler warnings as errors" OFF)
set(CG_SHADERS_ADDITIONAL_FLAGS "" CACHE STRING "Extra flags for the shader compiler (semicolon-separated)")
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension")
set(CG_SHADERS_RUNTIME_SUBDIR "renderer/Shaders" CACHE STRING "Subfolder next to the EXE for compiled shaders")

set(CG_FXC_PATH "" CACHE FILEPATH "Full path to fxc.exe (optional)")
set(CG_DXC_PATH "" CACHE FILEPATH "Full path to dxc.exe (optional)")

set(COLONY_HLSL_COMPILER "AUTO" CACHE STRING "HLSL compiler: AUTO (prefer DXC), DXC, FXC")
set_property(CACHE COLONY_HLSL_COMPILER PROPERTY STRINGS "AUTO" "DXC" "FXC")

# --- Tool discovery ------------------------------------------------------------
function(_cg_find_dxc OUT_EXE)
  if(CG_DXC_PATH AND EXISTS "${CG_DXC_PATH}")
    set(${OUT_EXE} "${CG_DXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_hints "")
  foreach(_root IN ITEMS "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_INSTALLATION_ROOT}")
    if(NOT "${_root}" STREQUAL "")
      list(APPEND _hints
        "${_root}/installed/x64-windows/tools/directx-dxc"
        "${_root}/installed/x86-windows/tools/directx-dxc"
        "${_root}/installed/arm64-windows/tools/directx-dxc")
    endif()
  endforeach()

  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_hints})
  if(DXC_EXE)
    message(STATUS "CGShaders: DXC = ${DXC_EXE}")
    set(${OUT_EXE} "${DXC_EXE}" PARENT_SCOPE)
  else()
    set(${OUT_EXE} "" PARENT_SCOPE) # Optional; we'll fallback to FXC.
  endif()
endfunction()

function(_cg_find_fxc OUT_EXE)
  if(CG_FXC_PATH AND EXISTS "${CG_FXC_PATH}")
    set(${OUT_EXE} "${CG_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_fxc_hints "")

  if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "")
      list(APPEND _fxc_hints "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
    endif()
    list(APPEND _fxc_hints "$ENV{WindowsSdkDir}/bin/x64")
  endif()

  if(DEFINED ENV{ProgramFiles\(x86\)} AND NOT "$ENV{ProgramFiles\(x86\)}" STREQUAL "")
    set(_PF86 "$ENV{ProgramFiles\(x86\)}")
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "")
      list(APPEND _fxc_hints
        "${_PF86}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "${_PF86}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
    endif()
    list(APPEND _fxc_hints
      "${_PF86}/Windows Kits/10/bin/x64"
      "${_PF86}/Windows Kits/11/bin/x64")
  endif()

  if(DEFINED ENV{ProgramFiles} AND NOT "$ENV{ProgramFiles}" STREQUAL "")
    set(_PF "$ENV{ProgramFiles}")
    list(APPEND _fxc_hints
      "${_PF}/Windows Kits/10/bin/x64"
      "${_PF}/Windows Kits/11/bin/x64")
  endif()

  if(DEFINED ENV{WindowsSdkVerBinPath} AND NOT "$ENV{WindowsSdkVerBinPath}" STREQUAL "")
    list(APPEND _fxc_hints "$ENV{WindowsSdkVerBinPath}/x64")
  endif()

  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_fxc_hints} PATH_SUFFIXES x64)
  if(NOT FXC_EXE)
    find_program(FXC_EXE NAMES fxc fxc.exe)
  endif()

  if(NOT FXC_EXE)
    message(FATAL_ERROR
      "fxc.exe not found.\n"
      "Hints searched:\n ${_fxc_hints}\n"
      "Install the Windows 10/11 SDK or set CG_FXC_PATH to fxc.exe.")
  endif()

  message(STATUS "CGShaders: FXC = ${FXC_EXE}")
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# --- Utilities -----------------------------------------------------------------
function(_cg_infer_profile SHADER_PATH OUT_PROFILE)
  # Allow per-file override via source property first:
  get_source_file_property(_p "${SHADER_PATH}" HLSL_PROFILE)
  if(NOT _p STREQUAL "NOTFOUND" AND _p)
    set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_name "${SHADER_PATH}" NAME)
  get_filename_component(_stem "${SHADER_PATH}" NAME_WE)
  string(TOLOWER "${_name}" _n)
  string(TOLOWER "${_stem}" _s)

  set(_stage "")
  if(_n MATCHES "\\.vs\\.hlsl$")      set(_stage "vs")
  elseif(_n MATCHES "\\.ps\\.hlsl$")  set(_stage "ps")
  elseif(_n MATCHES "\\.(cs|compute)\\.hlsl$") set(_stage "cs")
  elseif(_n MATCHES "\\.gs\\.hlsl$")  set(_stage "gs")
  elseif(_n MATCHES "\\.hs\\.hlsl$")  set(_stage "hs")
  elseif(_n MATCHES "\\.ds\\.hlsl$")  set(_stage "ds")
  endif()

  if(_stage STREQUAL "")
    if(_s MATCHES "^(vs)[_-]" OR _s MATCHES "([._-])vs([._-])" OR _s MATCHES "vs$") set(_stage "vs")
    elseif(_s MATCHES "^(ps)[_-]" OR _s MATCHES "([._-])ps([._-])" OR _s MATCHES "(fragment|frag)" OR _s MATCHES "ps$") set(_stage "ps")
    elseif(_s MATCHES "^(cs)[_-]" OR _s MATCHES "([._-])cs([._-])" OR _s MATCHES "(compute)" OR _s MATCHES "cs$") set(_stage "cs")
    elseif(_s MATCHES "^(gs)[_-]" OR _s MATCHES "([._-])gs([._-])" OR _s MATCHES "(geometry)" OR _s MATCHES "gs$") set(_stage "gs")
    elseif(_s MATCHES "^(hs)[_-]" OR _s MATCHES "([._-])hs([._-])" OR _s MATCHES "(hull)" OR _s MATCHES "hs$") set(_stage "hs")
    elseif(_s MATCHES "^(ds)[_-]" OR _s MATCHES "([._-])ds([._-])" OR _s MATCHES "(domain)" OR _s MATCHES "ds$") set(_stage "ds")
    endif()
  endif()

  if(_stage STREQUAL "")
    set(_stage "ps") # conservative default
  endif()

  set(${OUT_PROFILE} "${_stage}_5_0" PARENT_SCOPE) # D3D11 default
endfunction()

function(_cg_accumulate_args PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc) list(APPEND _res "/I" "${inc}") endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def) list(APPEND _res "/D" "${def}") endif()
  endforeach()
  if(CG_SHADERS_WARNINGS_AS_ERRORS) list(APPEND _res "/WX") endif()
  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

function(_cg_accumulate_args_dxc PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc) list(APPEND _res "-I" "${inc}") endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def) list(APPEND _res "-D" "${def}") endif()
  endforeach()
  if(CG_SHADERS_WARNINGS_AS_ERRORS) list(APPEND _res "-WX") endif()
  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

function(_cg_collect_header_deps OUT_LIST)
  set(_deps "")
  foreach(inc IN LISTS ARGN)
    if(inc AND EXISTS "${inc}")
      file(GLOB_RECURSE _hdrs CONFIGURE_DEPENDS
        "${inc}/*.hlsli" "${inc}/*.fxh" "${inc}/*.hlslinc" "${inc}/*.h")
      if(_hdrs)
        list(REMOVE_DUPLICATES _hdrs)
        list(APPEND _deps ${_hdrs})
      endif()
    endif()
  endforeach()
  set(${OUT_LIST} "${_deps}" PARENT_SCOPE)
endfunction()

function(cg_set_hlsl_properties FILE)
  set(oneValueArgs ENTRY PROFILE)
  set(multiValueArgs DEFINES INCLUDE_DIRS FLAGS)
  cmake_parse_arguments(H "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(H_ENTRY)         set_source_files_properties(${FILE} PROPERTIES HLSL_ENTRY         "${H_ENTRY}")         endif()
  if(H_PROFILE)       set_source_files_properties(${FILE} PROPERTIES HLSL_PROFILE       "${H_PROFILE}")       endif()
  if(H_DEFINES)       set_source_files_properties(${FILE} PROPERTIES HLSL_DEFINES       "${H_DEFINES}")       endif()
  if(H_INCLUDE_DIRS)  set_source_files_properties(${FILE} PROPERTIES HLSL_INCLUDE_DIRS  "${H_INCLUDE_DIRS}")  endif()
  if(H_FLAGS)         set_source_files_properties(${FILE} PROPERTIES HLSL_FLAGS         "${H_FLAGS}")         endif()
endfunction()

# --- Public API ----------------------------------------------------------------
function(cg_compile_hlsl TARGET_NAME)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs SHADERS INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADERS)
    message(FATAL_ERROR "cg_compile_hlsl(${TARGET_NAME}): SHADERS list is required.")
  endif()

  # Choose compiler: DXC if available (or forced), else FXC.
  set(_use_dxc FALSE)
  if(COLONY_HLSL_COMPILER STREQUAL "DXC")
    set(_use_dxc TRUE)
  elseif(COLONY_HLSL_COMPILER STREQUAL "AUTO")
    _cg_find_dxc(DXC_EXE)
    if(DXC_EXE) set(_use_dxc TRUE) endif()
  endif()
  if(_use_dxc)
    if(NOT DEFINED DXC_EXE OR DXC_EXE STREQUAL "") _cg_find_dxc(DXC_EXE) endif()
    if(NOT DXC_EXE)
      message(STATUS "CGShaders: DXC not found; falling back to FXC.")
      set(_use_dxc FALSE)
    endif()
  endif()
  if(NOT _use_dxc)
    _cg_find_fxc(FXC_EXE)
  endif()

  # Configure-time output dir (no generator expressions here).
  if(CG_OUTPUT_DIR)
    set(_base_out "${CG_OUTPUT_DIR}")
  else()
    set(_base_out "${CMAKE_BINARY_DIR}/shaders")
  endif()

  file(MAKE_DIRECTORY "${_base_out}")
  set(_outputs "")
  _cg_collect_header_deps(_header_deps ${CG_INCLUDE_DIRS})

  # Build per .hlsl file
  foreach(_src IN LISTS CG_SHADERS)
    if(NOT EXISTS "${_src}")
      message(FATAL_ERROR "Shader not found: ${_src}")
    endif()

    get_source_file_property(_entry   "${_src}" HLSL_ENTRY)
    if(NOT _entry OR _entry STREQUAL "NOTFOUND") set(_entry "main") endif()
    _cg_infer_profile("${_src}" _profile)

    get_filename_component(_stem "${_src}" NAME_WE)
    set(_out "${_base_out}/${_stem}.${CG_SHADER_OUTPUT_EXT}")

    get_source_file_property(_perflags "${_src}" HLSL_FLAGS)
    get_source_file_property(_perdefs  "${_src}" HLSL_DEFINES)
    get_source_file_property(_perincs  "${_src}" HLSL_INCLUDE_DIRS)

    set(_defs "${CG_DEFINES};${_perdefs}")
    set(_incs "${CG_INCLUDE_DIRS};${_perincs}")

    if(_use_dxc)
      _cg_accumulate_args_dxc(CG _args)
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${DXC_EXE}" -nologo -T "${_profile}" -E "${_entry}"
                -Fo "${_out}" "${_src}" ${_args} ${_perflags}
        MAIN_DEPENDENCY "${_src}"
        DEPENDS ${_header_deps}
        COMMENT "HLSL (DXC): ${_src} -> ${_out}"
        VERBATIM
      )
    else()
      _cg_accumulate_args(CG _args)
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${FXC_EXE}" /nologo /T "${_profile}" /E "${_entry}"
                /Fo "${_out}" "${_src}" ${_args} ${_perflags}
        MAIN_DEPENDENCY "${_src}"
        DEPENDS ${_header_deps}
        COMMENT "HLSL (FXC): ${_src} -> ${_out}"
        VERBATIM
      )
    endif()

    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})

  if(CG_EMBED)
    # Optional: convert binaries to headers (only if you add cmake/_BinaryToHeader.cmake)
    # include(${CMAKE_SOURCE_DIR}/cmake/_BinaryToHeader.cmake OPTIONAL)
    # cg_embed_binaries_to_headers(${TARGET_NAME} ${_outputs})
  endif()
endfunction()

function(cg_link_shaders_to_target SHADERS_TARGET EXECUTABLE_TARGET)
  if(NOT TARGET ${SHADERS_TARGET})
    message(FATAL_ERROR "cg_link_shaders_to_target: shader target ${SHADERS_TARGET} not found")
  endif()
  if(NOT TARGET ${EXECUTABLE_TARGET})
    message(FATAL_ERROR "cg_link_shaders_to_target: executable target ${EXECUTABLE_TARGET} not found")
  endif()

  # Discover outputs via the custom target’s dependencies:
  add_custom_command(TARGET ${EXECUTABLE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${EXECUTABLE_TARGET}>/${CG_SHADERS_RUNTIME_SUBDIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/shaders"
            "$<TARGET_FILE_DIR:${EXECUTABLE_TARGET}>/${CG_SHADERS_RUNTIME_SUBDIR}"
    COMMENT "Copying compiled shaders next to ${EXECUTABLE_TARGET}"
    VERBATIM
  )
  add_dependencies(${EXECUTABLE_TARGET} ${SHADERS_TARGET})
endfunction()
