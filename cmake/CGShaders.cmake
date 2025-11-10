# Windows-only HLSL build glue with DXC (SM6) or FXC (SM5.x) fallback.
# Place in cmake/ and `include(cmake/CGShaders.cmake)` from top-level CMakeLists.txt.
include_guard(GLOBAL)

# If someone includes this on non-Windows, do nothing gracefully (no build steps).
if(NOT WIN32)
  message(STATUS "CGShaders.cmake: non-Windows host; shader build helpers are inert.")
  set(CG_SHADERS_USE_DXC OFF CACHE BOOL "" FORCE)
  # Provide no-op functions so CMakeLists can still call them without errors.
  function(cg_compile_hlsl) endfunction()
  function(cg_link_shaders_to_target) endfunction()
  function(cg_set_hlsl_properties) endfunction()
  return()
endif()

option(CG_SHADERS_USE_DXC
       "Use DXC to compile HLSL (SM6). If OFF, use FXC (SM5.x)." OFF)
option(CG_SHADERS_WARNINGS_AS_ERRORS
       "Treat shader compiler warnings as errors" OFF)

# Extra, global flags you may want to push into the HLSL compiler (list)
# Example: set(CG_SHADERS_ADDITIONAL_FLAGS "-Od;-Zpr" CACHE STRING "" FORCE)
set(CG_SHADERS_ADDITIONAL_FLAGS "" CACHE STRING
    "Additional flags passed to dxc/fxc (semicolon-separated)")

set(CG_SHADER_DEFAULT_SM "6_7" CACHE STRING "Default Shader Model (DXC)")
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension (cso/dxil)")

# --- find dxc.exe -------------------------------------------------------------
function(_cg_find_dxc OUT_EXE)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _hints
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  list(APPEND _hints "$ENV{DXC_DIR}")
  if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    list(APPEND _hints
      "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
      "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  endif()
  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_hints})
  if(NOT DXC_EXE)
    message(FATAL_ERROR
      "dxc.exe not found. Install vcpkg port 'directx-dxc' (host) or set DXC_DIR / Windows SDK.")
  endif()
  set(${OUT_EXE} "${DXC_EXE}" PARENT_SCOPE)
endfunction()

# --- find fxc.exe -------------------------------------------------------------
function(_cg_find_fxc OUT_EXE)
  set(_fxc_hints
    "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/x64"
  )
  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_fxc_hints})
  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK.")
  endif()
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# --- utilities ----------------------------------------------------------------
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
  if(CG_SHADERS_USE_DXC)
    set(_sm "${CG_SHADER_DEFAULT_SM}")
  else()
    set(_sm "5_0")
  endif()
  set(${OUT_PROFILE} "${_stage}_${_sm}" PARENT_SCOPE)
endfunction()

# Accumulate include/define args for the *global* CG_* lists.
# Uses -I/-D for DXC, /I-/D for FXC.
function(_cg_accumulate_args PREFIX OUT_LIST)
  set(_res "")
  if(CG_SHADERS_USE_DXC)
    set(_IFLAG "-I")
    set(_DFLAG "-D")
  else()
    set(_IFLAG "/I")
    set(_DFLAG "/D")
  endif()
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc) list(APPEND _res "${_IFLAG}" "${inc}") endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def) list(APPEND _res "${_DFLAG}" "${def}") endif()
  endforeach()
  # Warnings as errors?
  if(CG_SHADERS_WARNINGS_AS_ERRORS)
    if(CG_SHADERS_USE_DXC)
      list(APPEND _res "-WX")
    else()
      list(APPEND _res "/WX")
    endif()
  endif()
  # Global extra flags (semicolon-separated)
  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

# Build a dependency list by scanning includes directories for typical include extensions.
function(_cg_collect_header_deps OUT_LIST)
  set(_deps "")
  foreach(inc IN LISTS ARGN)
    if(inc AND EXISTS "${inc}")
      file(GLOB_RECURSE _hdrs CONFIGURE_DEPENDS
        "${inc}/*.hlsli" "${inc}/*.fxh" "${inc}/*.hlslinc" "${inc}/*.h")
      list(APPEND _deps ${_hdrs})
    endif()
  endforeach()
  set(${OUT_LIST} "${_deps}" PARENT_SCOPE)
endfunction()

# Convenience: per-file overrides in CMakeLists:
#   cg_set_hlsl_properties(<file>
#     [ENTRY main] [PROFILE ps_6_7]
#     [DEFINES FOO=1 BAR=2] [INCLUDE_DIRS dir1 dir2] [FLAGS -O3])
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
    # Store as a list on the source
    set_source_files_properties(${FILE} PROPERTIES HLSL_DEFINES "${H_DEFINES}")
  endif()
  if(H_INCLUDE_DIRS)
    set_source_files_properties(${FILE} PROPERTIES HLSL_INCLUDE_DIRS "${H_INCLUDE_DIRS}")
  endif()
  if(H_FLAGS)
    set_source_files_properties(${FILE} PROPERTIES HLSL_FLAGS "${H_FLAGS}")
  endif()
endfunction()

# --- public API ---------------------------------------------------------------
# cg_compile_hlsl(TargetName
#   SHADERS    a.b.hlsl ...
#   [INCLUDE_DIRS ...]
#   [DEFINES ...]
#   [OUTPUT_DIR <dir>]   # default: ${CMAKE_BINARY_DIR}/shaders
#   [EMBED]              # also generate .h from blobs via _BinaryToHeader.cmake
# )
function(cg_compile_hlsl TARGET_NAME)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs SHADERS INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADERS)
    message(FATAL_ERROR "cg_compile_hlsl(${TARGET_NAME}): SHADERS list is required.")
  endif()

  if(CG_SHADERS_USE_DXC)  _cg_find_dxc(DXC_EXE)  else()  _cg_find_fxc(FXC_EXE)  endif()

  if(NOT CG_OUTPUT_DIR)
    set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  _cg_collect_header_deps(_approx_deps ${CG_INCLUDE_DIRS})
  _cg_accumulate_args(CG _extra_args)

  set(_outputs "")
  list(LENGTH CG_SHADERS _hlsl_count)
  message(STATUS "CGShaders: compiling ${_hlsl_count} HLSL file(s) -> ${CG_OUTPUT_DIR}")

  foreach(_src IN LISTS CG_SHADERS)
    get_filename_component(_src_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_src}" NAME_WE)

    # Per-file overrides (optional)
    get_source_file_property(_entry "${_src}" HLSL_ENTRY)
    if(_entry STREQUAL "NOTFOUND" OR NOT _entry)  set(_entry "main")  endif()
    _cg_infer_profile("${_src}" _profile)

    get_source_file_property(_src_defines "${_src}" HLSL_DEFINES)
    if(_src_defines STREQUAL "NOTFOUND")  set(_src_defines "")  endif()
    get_source_file_property(_src_includes "${_src}" HLSL_INCLUDE_DIRS)
    if(_src_includes STREQUAL "NOTFOUND") set(_src_includes "") endif()
    get_source_file_property(_src_flags "${_src}" HLSL_FLAGS)
    if(_src_flags STREQUAL "NOTFOUND") set(_src_flags "") endif()

    # Build per-source args with correct flags for DXC/FXC
    set(_per_src_args "")
    if(CG_SHADERS_USE_DXC)
      set(_IFLAG "-I")  set(_DFLAG "-D")
    else()
      set(_IFLAG "/I")  set(_DFLAG "/D")
    endif()
    foreach(inc IN LISTS _src_includes)
      if(inc) list(APPEND _per_src_args "${_IFLAG}" "${inc}") endif()
    endforeach()
    foreach(def IN LISTS _src_defines)
      if(def) list(APPEND _per_src_args "${_DFLAG}" "${def}") endif()
    endforeach()
    if(_src_flags)
      separate_arguments(_src_flags_list NATIVE_COMMAND "${_src_flags}")
      list(APPEND _per_src_args ${_src_flags_list})
    endif()

    set(_out "${CG_OUTPUT_DIR}/${_base}.${CG_SHADER_OUTPUT_EXT}")

    if(CG_SHADERS_USE_DXC)
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
        COMMAND "${DXC_EXE}" -nologo -T "${_profile}" -E "${_entry}"
                $<$<CONFIG:Debug>:-Zi -Qembed_debug -Od>
                $<$<CONFIG:RelWithDebInfo>:-Zi -Qembed_debug -O3>
                $<$<CONFIG:Release>:-O3 -Qstrip_debug -Qstrip_reflect>
                ${_extra_args} ${_per_src_args} -Fo "${_out}" "${_src_abs}"
        MAIN_DEPENDENCY "${_src_abs}"
        DEPENDS "${_src_abs}" ${_approx_deps}
        COMMENT "DXC ${_profile} ${_base}.hlsl → ${_out}"
        VERBATIM)
    else()
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
        COMMAND "${FXC_EXE}" /nologo /T "${_profile}" /E "${_entry}"
                $<$<CONFIG:Debug>:/Zi /Od>
                $<$<CONFIG:RelWithDebInfo>:/Zi>
                $<$<CONFIG:Release>:/O3>
                ${_extra_args} ${_per_src_args} /Fo "${_out}" "${_src_abs}"
        MAIN_DEPENDENCY "${_src_abs}"
        DEPENDS "${_src_abs}" ${_approx_deps}
        COMMENT "FXC ${_profile} ${_base}.hlsl → ${_out}"
        VERBATIM)
    endif()

    list(APPEND _outputs "${_out}")

    if(CG_EMBED)
      set(_hdr "${_out}.h")
      add_custom_command(
        OUTPUT "${_hdr}"
        COMMAND ${CMAKE_COMMAND} -DINPUT="${_out}" -DOUTPUT="${_hdr}"
                -P "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        DEPENDS "${_out}" "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        COMMENT "Embed ${_out} → ${_hdr}")
      list(APPEND _outputs "${_hdr}")
    endif()
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS "${_outputs}")
endfunction()

# Copy the compiled blobs next to the exe under /renderer/Shaders (matches your repo).
function(cg_link_shaders_to_target SHADER_TARGET RUNTIME_TARGET)
  get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
  if(NOT _outdir)
    message(FATAL_ERROR "cg_link_shaders_to_target: ${SHADER_TARGET} has no CG_SHADER_OUTPUT_DIR")
  endif()
  set(_dest "$<TARGET_FILE_DIR:${RUNTIME_TARGET}>/renderer/Shaders")
  add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "${_dest}"
    COMMENT "Copying shaders to ${_dest}")
  add_dependencies(${RUNTIME_TARGET} ${SHADER_TARGET})
endfunction()
