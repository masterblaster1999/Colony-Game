# cmake/CGShaders.cmake
# Windows-only HLSL build glue (FXC/SM5.x). Minimal, robust, and CI-friendly.
# Requires CMake >= 3.8 for COMMAND_EXPAND_LISTS.
# Docs: add_custom_command + generator expressions:
#   - https://cmake.org/cmake/help/latest/command/add_custom_command.html
#   - https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html

include_guard(GLOBAL)
include(CMakeParseArguments)

# --------------------------------------------------------------------------
# No-ops on non-Windows so include() remains harmless on other hosts.
# --------------------------------------------------------------------------
if(NOT WIN32)
  message(STATUS "CGShaders.cmake: non-Windows host; shader build helpers are inert.")
  function(cg_compile_hlsl)
  endfunction()
  function(cg_link_shaders_to_target)
  endfunction()
  function(cg_set_hlsl_properties)
  endfunction()
  return()
endif()

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(CG_SHADERS_WARNINGS_AS_ERRORS "Treat shader compiler warnings as errors" OFF)

# Extra flags you want to push into FXC (semicolon-separated), e.g. "/Zpr;/Ges"
set(CG_SHADERS_ADDITIONAL_FLAGS "" CACHE STRING "Additional flags passed to fxc.exe (semicolon-separated)")

# File extension for compiled shader blobs
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension (usually 'cso')")

# Where compiled shaders are copied beside the runtime target
set(CG_SHADERS_RUNTIME_SUBDIR "renderer/Shaders" CACHE STRING "Subfolder next to the EXE for compiled shaders")

# Optional manual override for compiler location
set(CG_FXC_PATH "" CACHE FILEPATH "Full path to fxc.exe (optional override)")

# --------------------------------------------------------------------------
# Locate FXC (Windows SDK)
# --------------------------------------------------------------------------
function(_cg_find_fxc OUT_EXE)
  if(CG_FXC_PATH AND EXISTS "${CG_FXC_PATH}")
    set(${OUT_EXE} "${CG_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_fxc_hints "")
  if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND
     NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "")
    list(APPEND _fxc_hints
      "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
      "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
      "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  endif()
  list(APPEND _fxc_hints
    "$ENV{WindowsSdkDir}/bin/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/x64")

  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_fxc_hints})
  if(NOT FXC_EXE)
    find_program(FXC_EXE NAMES fxc fxc.exe)
  endif()

  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK or set CG_FXC_PATH.")
  endif()

  message(STATUS "CGShaders: using FXC at: ${FXC_EXE}")
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# --------------------------------------------------------------------------
# Utilities
# --------------------------------------------------------------------------
# Infer profile: use per-file override if set; otherwise infer by suffix and SM 5.0.
function(_cg_infer_profile SHADER_PATH OUT_PROFILE)
  get_source_file_property(_p "${SHADER_PATH}" HLSL_PROFILE)
  if(NOT _p STREQUAL "NOTFOUND" AND _p)
    set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_base "${SHADER_PATH}" NAME)
  string(REGEX MATCH "([\\._-])(vs|ps|cs|gs|hs|ds)([\\._-])" _m "${_base}")
  if(_m)
    string(REGEX REPLACE ".*([\\._-])(vs|ps|cs|gs|hs|ds)([\\._-]).*" "\\2" _stage "${_base}")
  else()
    set(_stage "ps")
  endif()

  set(${OUT_PROFILE} "${_stage}_5_0" PARENT_SCOPE)
endfunction()

# Build aggregated include/define args; each flag/value is a separate argv item.
function(_cg_accumulate_args PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc)
      list(APPEND _res "/I" "${inc}")
    endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def)
      list(APPEND _res "/D" "${def}")
    endif()
  endforeach()
  if(CG_SHADERS_WARNINGS_AS_ERRORS)
    list(APPEND _res "/WX")
  endif()
  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

# Build a conservative dependency list by scanning include directories
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

# Per-file overrides (entry/profile/defines/includes/flags)
function(cg_set_hlsl_properties FILE)
  set(oneValueArgs ENTRY PROFILE)
  set(multiValueArgs DEFINES INCLUDE_DIRS FLAGS)
  cmake_parse_arguments(H "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(H_ENTRY)
    set_source_files_properties(${FILE} PROPERTIES HLSL_ENTRY "${H_ENTRY}")
  endif()
  if(H_PROFILE)
    set_source_files_properties(${FILE} PROPERTIES HLSL_PROFILE "${H_PROFILE}")
  endif()
  if(H_DEFINES)
    set_source_files_properties(${FILE} PROPERTIES HLSL_DEFINES "${H_DEFINES}")
  endif()
  if(H_INCLUDE_DIRS)
    set_source_files_properties(${FILE} PROPERTIES HLSL_INCLUDE_DIRS "${H_INCLUDE_DIRS}")
  endif()
  if(H_FLAGS)
    set_source_files_properties(${FILE} PROPERTIES HLSL_FLAGS "${H_FLAGS}")
  endif()
endfunction()

# --------------------------------------------------------------------------
# Public API
# --------------------------------------------------------------------------
# cg_compile_hlsl(TargetName
#   SHADERS a.hlsl b.hlsl ...
#   [INCLUDE_DIRS ...]
#   [DEFINES ...]
#   [OUTPUT_DIR <dir>]        # default: ${CMAKE_BINARY_DIR}/shaders[/ $<CONFIG>]
#   [EMBED]                   # also generate .h from blobs via _BinaryToHeader.cmake
# )
function(cg_compile_hlsl TARGET_NAME)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs SHADERS INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADERS)
    message(FATAL_ERROR "cg_compile_hlsl(${TARGET_NAME}): SHADERS list is required.")
  endif()

  _cg_find_fxc(FXC_EXE)

  if(NOT CG_OUTPUT_DIR)
    if(CMAKE_CONFIGURATION_TYPES)
      set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>")
    else()
      set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  _cg_collect_header_deps(_approx_deps ${CG_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")
  _cg_accumulate_args(CG _extra_args)

  list(LENGTH CG_SHADERS _hlsl_count)
  message(STATUS "CGShaders: compiling ${_hlsl_count} HLSL file(s) -> ${CG_OUTPUT_DIR}")

  set(_outputs "")
  foreach(_src IN LISTS CG_SHADERS)
    # Skip headers/includes if they were accidentally listed
    if(_src MATCHES "\\.(hlsli|fxh|hlslinc)$")
      continue()
    endif()

    get_filename_component(_src_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_src}" NAME_WE)

    # Per-file overrides (optional)
    get_source_file_property(_entry "${_src}" HLSL_ENTRY)
    if(_entry STREQUAL "NOTFOUND" OR NOT _entry)
      set(_entry "main")
    endif()

    _cg_infer_profile("${_src}" _profile)

    get_source_file_property(_src_defines  "${_src}" HLSL_DEFINES)
    if(_src_defines STREQUAL "NOTFOUND")
      set(_src_defines "")
    endif()
    get_source_file_property(_src_includes "${_src}" HLSL_INCLUDE_DIRS)
    if(_src_includes STREQUAL "NOTFOUND")
      set(_src_includes "")
    endif()
    get_source_file_property(_src_flags "${_src}" HLSL_FLAGS)
    if(_src_flags STREQUAL "NOTFOUND")
      set(_src_flags "")
    endif()

    # Build per-source args
    set(_per_src_args "")
    foreach(inc IN LISTS _src_includes)
      if(inc)
        list(APPEND _per_src_args "/I" "${inc}")
      endif()
    endforeach()
    foreach(def IN LISTS _src_defines)
      if(def)
        list(APPEND _per_src_args "/D" "${def}")
      endif()
    endforeach()
    if(_src_flags)
      separate_arguments(_src_flags_list NATIVE_COMMAND "${_src_flags}")
      list(APPEND _per_src_args ${_src_flags_list})
    endif()

    # Decide extension & output file
    set(_out_ext "${CG_SHADER_OUTPUT_EXT}")
    if(NOT _out_ext)
      set(_out_ext "cso")
    endif()
    set(_out "${CG_OUTPUT_DIR}/${_base}.${_out_ext}")

    add_custom_command(
      OUTPUT  "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
      COMMAND "${FXC_EXE}"
        /nologo
        /T "${_profile}"
        /E "${_entry}"
        $<$<CONFIG:Debug>:/Zi;/Od>
        $<$<CONFIG:RelWithDebInfo>:/Zi;/O2>
        $<$<CONFIG:Release>:/O3>
        ${_extra_args}
        ${_per_src_args}
        /Fo "${_out}" "${_src_abs}"
      MAIN_DEPENDENCY "${_src_abs}"
      DEPENDS "${_src_abs}" ${_approx_deps}
      COMMENT "FXC ${_profile} ${_base}.hlsl -> ${_out}"
      VERBATIM
      COMMAND_EXPAND_LISTS
    )
    list(APPEND _outputs "${_out}")

    if(CG_EMBED)
      set(_hdr "${_out}.h")
      add_custom_command(
        OUTPUT "${_hdr}"
        COMMAND ${CMAKE_COMMAND}
          -DINPUT="${_out}"
          -DOUTPUT="${_hdr}"
          -P "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        DEPENDS "${_out}" "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        COMMENT "Embed ${_out} -> ${_hdr}"
        VERBATIM
      )
      list(APPEND _outputs "${_hdr}")
    endif()
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS    "${_outputs}")
endfunction()

# Copy the compiled blobs next to the exe under /renderer/Shaders by default.
function(cg_link_shaders_to_target SHADER_TARGET RUNTIME_TARGET)
  get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
  if(NOT _outdir)
    message(FATAL_ERROR "cg_link_shaders_to_target: ${SHADER_TARGET} has no CG_SHADER_OUTPUT_DIR")
  endif()

  if(NOT DEFINED CG_SHADERS_RUNTIME_SUBDIR OR CG_SHADERS_RUNTIME_SUBDIR STREQUAL "")
    set(_subdir "renderer/Shaders")
  else()
    set(_subdir "${CG_SHADERS_RUNTIME_SUBDIR}")
  endif()

  set(_dest "$<TARGET_FILE_DIR:${RUNTIME_TARGET}>/${_subdir}")

  add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "${_dest}"
    COMMENT "Copying shaders to ${_dest}"
    VERBATIM
  )

  # Optional install step (puts them under bin/)
  install(DIRECTORY "${_outdir}/" DESTINATION "bin/${_subdir}")

  add_dependencies(${RUNTIME_TARGET} ${SHADER_TARGET})
endfunction()
