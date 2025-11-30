# Minimal DXC helper for SM6. Requires Windows + DXC in PATH or vcpkg 'directx-dxc'.
function(colony_add_hlsl OUT_VAR)
  set(options)
  set(oneValueArgs OUTDIR)
  set(multiValueArgs FILES INCLUDES DEFINES)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT HLSL_OUTDIR)
    message(FATAL_ERROR "colony_add_hlsl: OUTDIR is required")
  endif()
  if(NOT HLSL_FILES)
    message(FATAL_ERROR "colony_add_hlsl: FILES is empty")
  endif()
  file(MAKE_DIRECTORY "${HLSL_OUTDIR}")

  # Try find DXC: system PATH first, then vcpkg tool dir.
  find_program(DXC_EXE NAMES dxc dxc.exe)
  if(NOT DXC_EXE AND DEFINED VCPKG_TARGET_TRIPLET AND DEFINED VCPKG_ROOT)
    find_program(DXC_EXE NAMES dxc dxc.exe
      HINTS "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc")
  endif()
  if(NOT DXC_EXE)
    message(FATAL_ERROR "DXC not found. Install 'directx-dxc' (e.g., via vcpkg) or put dxc.exe in PATH.")
  endif()

  set(_outputs)
  foreach(_src IN LISTS HLSL_FILES)
    get_filename_component(_namewe "${_src}" NAME_WE)
    # Read VS properties (you set these in shaders/CMakeLists.txt)
    get_source_file_property(_stype "${_src}" VS_SHADER_TYPE)
    get_source_file_property(_smodel "${_src}" VS_SHADER_MODEL)
    get_source_file_property(_sentry "${_src}" VS_SHADER_ENTRYPOINT)
    if(NOT _stype OR NOT _smodel OR NOT _sentry)
      message(FATAL_ERROR "HLSL ${_src}: VS_SHADER_TYPE/MODEL/ENTRYPOINT must be set")
    endif()

    if(_stype STREQUAL "Vertex")
      set(_profile "vs_${_smodel}")
      set(_suffix  "vs")
    elseif(_stype STREQUAL "Pixel")
      set(_profile "ps_${_smodel}")
      set(_suffix  "ps")
    elseif(_stype STREQUAL "Compute")
      set(_profile "cs_${_smodel}")
      set(_suffix  "cs")
    else()
      message(FATAL_ERROR "Unknown VS_SHADER_TYPE='${_stype}' for ${_src}")
    endif()

    set(_out "${HLSL_OUTDIR}/${_namewe}.${_suffix}.cso")
    # Build command with config-conditional flags (generator expressions)
    set(_cmd "${DXC_EXE}" -nologo
      -T "${_profile}" -E "${_sentry}"
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-Zi>
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-Qembed_debug>
      $<$<CONFIG:Debug>:-Od>
      $<$<CONFIG:Release>:-O3>
    )
    foreach(_def IN LISTS HLSL_DEFINES)
      list(APPEND _cmd -D "${_def}")
    endforeach()
    foreach(_inc IN LISTS HLSL_INCLUDES)
      list(APPEND _cmd -I "${_inc}")
    endforeach()
    list(APPEND _cmd -Fo "${_out}" "${_src}")

    add_custom_command(
      OUTPUT  "${_out}"
      COMMAND ${_cmd}
      DEPENDS "${_src}"
      COMMENT "DXC ${_src} -> ${_out}"
      VERBATIM
    )
    list(APPEND _outputs "${_out}")
  endforeach()

  set(${OUT_VAR} ${_outputs} PARENT_SCOPE)
endfunction()

