# cmake/CGShaders.cmake
# Windows-only HLSL build glue with DXC (SM6) or FXC (SM5.x) fallback.
# Place in your repo's cmake/ and `include(cmake/CGShaders.cmake)` in top-level CMakeLists.txt.

include_guard(GLOBAL)

# Toggle: DXC (SM6/DXIL) vs FXC (SM5.x/DXBC).  Default OFF to keep D3D11 happy.
option(CG_SHADERS_USE_DXC "Use DXC to compile HLSL (SM6). If OFF, use FXC (SM5.x)." OFF)
set(CG_SHADER_DEFAULT_SM "6_7" CACHE STRING "Default Shader Model with DXC (e.g., 6_0, 6_6, 6_7)")
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension (e.g., cso or dxil)")

# --- Internal: locate DXC or FXC -------------------------------------------------

function(_cg_find_dxc OUT_EXE)
  # Preferred hint: vcpkg directx-dxc (tools path)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _hints "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc")
    list(APPEND _hints "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  # User override: environment var DXC_DIR can point to folder containing dxc.exe
  list(APPEND _hints "$ENV{DXC_DIR}")
  # As a fallback, VS/Windows SDK often places DXC under Windows Kits bin:
  if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    list(APPEND _hints "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  endif()

  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_hints})
  if(NOT DXC_EXE)
    message(FATAL_ERROR
      "dxc.exe not found. Install the DirectX Shader Compiler (e.g., via vcpkg 'directx-dxc') or set DXC_DIR.")
  endif()
  set(${OUT_EXE} "${DXC_EXE}" PARENT_SCOPE)
endfunction()

function(_cg_find_fxc OUT_EXE)
  # FXC ships with Windows SDK; try common locations
  set(_fxc_hints
    "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/x64"
  )
  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_fxc_hints})
  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK.")
  endif()
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# --- Utilities -------------------------------------------------------------------

# Infer stage from filename: .vs.hlsl / .ps.hlsl / .cs.hlsl / .gs.hlsl / .hs.hlsl / .ds.hlsl
function(_cg_infer_profile SHADER_PATH OUT_PROFILE)
  # Allow per-file override via source properties:
  get_source_file_property(_p "${SHADER_PATH}" HLSL_PROFILE)
  if(NOT _p STREQUAL "NOTFOUND" AND _p)
    set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
    return()
  endif()

  string(REGEX MATCH "\\.(vs|ps|cs|gs|hs|ds)\\." _m "${SHADER_PATH}")
  if(_m)
    string(REGEX REPLACE ".*\\.(vs|ps|cs|gs|hs|ds)\\..*" "\\1" _stage "${SHADER_PATH}")
  else()
    # Fallback if not encoded in filename:
    set(_stage "ps")
  endif()

  if(CG_SHADERS_USE_DXC)
    set(_sm "${CG_SHADER_DEFAULT_SM}")
  else()
    # Safe default for D3D11 FXC
    if(_stage STREQUAL "cs")
      set(_sm "5_0")
    else()
      set(_sm "5_0")
    endif()
  endif()

  set(${OUT_PROFILE} "${_stage}_${_sm}" PARENT_SCOPE)
endfunction()

# Turn include dirs into DXC/FXC -I args and macros into -D/ /D
function(_cg_accumulate_args PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    list(APPEND _res "$<$<BOOL:${inc}>:-I>" "$<$<BOOL:${inc}>:${inc}>")
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(CG_SHADERS_USE_DXC)
      list(APPEND _res "$<$<BOOL:${def}>:-D>" "$<$<BOOL:${def}>:${def}>")
    else()
      list(APPEND _res "$<$<BOOL:${def}>:/D>" "$<$<BOOL:${def}>:${def}>")
    endif()
  endforeach()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

# Optionally approximate include dependencies by globbing headers in include dirs
function(_cg_collect_header_deps OUT_LIST)
  set(_deps "")
  foreach(inc IN LISTS ARGN)
    file(GLOB_RECURSE _hdrs CONFIGURE_DEPENDS
      "${inc}/*.hlsli" "${inc}/*.fxh" "${inc}/*.hlslinc" "${inc}/*.h")
    list(APPEND _deps ${_hdrs})
  endforeach()
  set(${OUT_LIST} "${_deps}" PARENT_SCOPE)
endfunction()

# --- Public API ------------------------------------------------------------------

# cg_compile_hlsl(
#   <ShaderTargetName>
#   SHADERS <list of .hlsl>
#   [INCLUDE_DIRS <dirs...>]
#   [DEFINES <MACROS...>]
#   [OUTPUT_DIR <dir>]            # default: ${CMAKE_BINARY_DIR}/shaders/$<CONFIG>
#   [EMBED]                       # also generate .h headers for each compiled blob
# )
function(cg_compile_hlsl TARGET_NAME)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs SHADERS INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADERS)
    message(FATAL_ERROR "cg_compile_hlsl(${TARGET_NAME}): SHADERS list is required.")
  endif()

  if(CG_SHADERS_USE_DXC)
    _cg_find_dxc(DXC_EXE)
  else()
    _cg_find_fxc(FXC_EXE)
  endif()

  if(NOT CG_OUTPUT_DIR)
    set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>")
  endif()
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

  # Header dep approximation for incremental rebuilds when includes (.hlsli) change:
  _cg_collect_header_deps(_approx_deps ${CG_INCLUDE_DIRS})
  _cg_accumulate_args(CG _extra_args)

  set(_outputs "")
  foreach(_src IN LISTS CG_SHADERS)
    get_filename_component(_src_abs "${_src}" ABSOLUTE)
    get_filename_component(_base    "${_src}" NAME_WE)
    get_source_file_property(_entry "${_src}" HLSL_ENTRY)
    if(_entry STREQUAL "NOTFOUND" OR NOT _entry)
      set(_entry "main")
    endif()
    _cg_infer_profile("${_src}" _profile)

    set(_out "${CG_OUTPUT_DIR}/${_base}.${CG_SHADER_OUTPUT_EXT}")

    if(CG_SHADERS_USE_DXC)
      # DXC flags: -T (profile), -E (entry), -Fo (out)
      # Debug info: -Zi + -Qembed_debug ; Release: -O3 + strip
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
        COMMAND "${DXC_EXE}"
          -nologo
          -T "${_profile}"
          -E "${_entry}"
          $<$<CONFIG:Debug>:-Zi -Qembed_debug -Od>
          $<$<CONFIG:RelWithDebInfo>:-Zi -Qembed_debug -Od>
          $<$<CONFIG:Release>:-O3 -Qstrip_debug -Qstrip_reflect>
          ${_extra_args}
          -Fo "${_out}" "${_src_abs}"
        MAIN_DEPENDENCY "${_src_abs}"
        DEPENDS "${_src_abs}" ${_approx_deps}
        COMMENT "DXC ${_profile} ${_base}.hlsl → ${_out}"
        VERBATIM)
    else()
      # FXC flags: /T (profile), /E (entry), /Fo (out)
      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
        COMMAND "${FXC_EXE}"
          /nologo
          /T "${_profile}"
          /E "${_entry}"
          $<$<CONFIG:Debug>:/Zi /Od>
          $<$<CONFIG:RelWithDebInfo>:/Zi /Od>
          $<$<CONFIG:Release>:/O3>
          ${_extra_args}
          /Fo "${_out}" "${_src_abs}"
        MAIN_DEPENDENCY "${_src_abs}"
        DEPENDS "${_src_abs}" ${_approx_deps}
        COMMENT "FXC ${_profile} ${_base}.hlsl → ${_out}"
        VERBATIM)
    endif()

    list(APPEND _outputs "${_out}")

    # Optional: embed as C header alongside the blob
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
  # Remember output dir/outputs for linking & install:
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS "${_outputs}")
endfunction()

# After shaders are compiled, copy them next to an exe so your launcher can find
# them under "<exe>/renderer/Shaders".
function(cg_link_shaders_to_target SHADER_TARGET RUNTIME_TARGET)
  get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
  if(NOT _outdir)
    message(FATAL_ERROR "cg_link_shaders_to_target: ${SHADER_TARGET} has no CG_SHADER_OUTPUT_DIR property")
  endif()
  set(_dest "$<TARGET_FILE_DIR:${RUNTIME_TARGET}>/renderer/Shaders")
  add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "${_dest}"
    COMMENT "Copying shaders to ${_dest}")
  add_dependencies(${RUNTIME_TARGET} ${SHADER_TARGET})
endfunction()
