# cmake/Shaders.cmake
if(NOT WIN32)
  message(FATAL_ERROR "HLSL build is Windows-only in this project.")
endif()

function(colony_add_hlsl OUTVAR)
  cmake_parse_arguments(HLSL "" "OUTDIR" "FILES;INCLUDES;DEFINES" ${ARGN})
  if(NOT HLSL_OUTDIR)
    message(FATAL_ERROR "colony_add_hlsl: OUTDIR is required")
  endif()
  if(NOT HLSL_FILES)
    set(${OUTVAR} "" PARENT_SCOPE)
    return()
  endif()

  # Prepare flags from lists
  set(_def_flags "")
  foreach(d ${HLSL_DEFINES})
    list(APPEND _def_flags "-D${d}")
  endforeach()

  set(_inc_flags "")
  foreach(i ${HLSL_INCLUDES})
    list(APPEND _inc_flags -I "${i}")
  endforeach()

  set(_outputs "")

  # Use Visual Studio’s built-in HLSL handling when available,
  # otherwise fall back to explicit DXC custom commands.
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(src ${HLSL_FILES})
      # Record a nominal output path to make a target dependency
      get_filename_component(_name ${src} NAME_WE)
      get_source_file_property(_type  ${src} VS_SHADER_TYPE)
      get_source_file_property(_model ${src} VS_SHADER_MODEL)
      if(_type STREQUAL "Vertex")
        set(_profile "vs_${_model}")
      elseif(_type STREQUAL "Pixel")
        set(_profile "ps_${_model}")
      elseif(_type STREQUAL "Compute")
        set(_profile "cs_${_model}")
      else()
        set(_profile "bin")
      endif()
      set(_out "${HLSL_OUTDIR}/${_name}.${_profile}.cso")
      add_custom_command(OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E touch "${_out}"
        DEPENDS "${src}"
        COMMENT "VS handles ${_name} (${_profile}); stamping ${_out}"
      )
      list(APPEND _outputs "${_out}")
    endforeach()
  else()
    # Find dxc.exe
    find_program(DXC_EXE NAMES dxc
      HINTS
        "$ENV{DXC_DIR}"
        "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
        "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
        "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin"
        "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin"
      DOC "Path to dxc.exe (DirectX Shader Compiler)"
    )
    if(NOT DXC_EXE)
      message(FATAL_ERROR "dxc.exe not found. Install DXC or set DXC_DIR.")
    endif()

    foreach(src ${HLSL_FILES})
      get_filename_component(_name ${src} NAME_WE)
      get_source_file_property(_type  ${src} VS_SHADER_TYPE)
      get_source_file_property(_model ${src} VS_SHADER_MODEL)
      get_source_file_property(_entry ${src} VS_SHADER_ENTRYPOINT)
      if(NOT _type OR NOT _model OR NOT _entry)
        message(FATAL_ERROR "Set VS_SHADER_TYPE/ENTRYPOINT/MODEL for ${src}")
      endif()

      if(_type STREQUAL "Vertex")
        set(_profile "vs_${_model}")
      elseif(_type STREQUAL "Pixel")
        set(_profile "ps_${_model}")
      elseif(_type STREQUAL "Compute")
        set(_profile "cs_${_model}")
      else()
        message(FATAL_ERROR "Unsupported shader type ${_type} for ${src}")
      endif()

      set(_out "${HLSL_OUTDIR}/${_name}.${_profile}.cso")

      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HLSL_OUTDIR}"
        COMMAND "${DXC_EXE}"
          -T "${_profile}"
          -E "${_entry}"
          -Fo "${_out}"
          $<$<CONFIG:Debug>:-Zi -Qembed_debug>
          $<$<CONFIG:RelWithDebInfo>:-Zi -Qstrip_debug>
          $<$<CONFIG:Release>:-O3 -Qstrip_debug>
          ${_def_flags} ${_inc_flags}
          "${src}"
        DEPENDS "${src}"
        COMMENT "DXC ${_name} (${_profile})"
        VERBATIM
      )
      list(APPEND _outputs "${_out}")
    endforeach()
  endif()

  set(${OUTVAR} ${_outputs} PARENT_SCOPE)
endfunction()
