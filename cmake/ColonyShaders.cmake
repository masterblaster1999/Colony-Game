# cmake/ColonyShaders.cmake
# Windows-only helper to compile HLSL with DXC (fallback to FXC if DXC not found).
function(colony_compile_hlsl target)
  set(options)
  set(oneValueArgs OUTPUT_DIR PROFILE ENTRY)
  set(multiValueArgs SOURCES DEFINES INCLUDES)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT WIN32)
    message(FATAL_ERROR "colony_compile_hlsl: Windows-only")
  endif()
  if (NOT HLSL_SOURCES)
    return()
  endif()

  # Try to locate dxc.exe; allow vcpkg or system installs.
  find_program(DXC_EXE NAMES dxc HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/dxc")
  if (NOT DXC_EXE)
    # Fallback to fxc (older HLSL compiler from Windows SDK). You can remove this if you use DXC only.
    find_program(FXC_EXE NAMES fxc)
  endif()

  file(MAKE_DIRECTORY "${HLSL_OUTPUT_DIR}")

  set(_outputs)
  foreach(_src IN LISTS HLSL_SOURCES)
    get_filename_component(_name "${_src}" NAME_WE)
    set(_out "${HLSL_OUTPUT_DIR}/${_name}.cso")

    if (DXC_EXE)
      # DXC (Shader Model 6.x)
      set(_cmd "${DXC_EXE}" -nologo -T "${HLSL_PROFILE}" -E "${HLSL_ENTRY}" -Fo "${_out}" "${_src}")
      foreach(_def IN LISTS HLSL_DEFINES)
        list(APPEND _cmd -D "${_def}")
      endforeach()
      foreach(_inc IN LISTS HLSL_INCLUDES)
        list(APPEND _cmd -I "${_inc}")
      endforeach()
    elseif(FXC_EXE)
      # FXC (Shader Model 5.x). Adjust /T profile accordingly if you use D3D11.
      set(_cmd "${FXC_EXE}" /nologo /T "${HLSL_PROFILE}" /E "${HLSL_ENTRY}" /Fo "${_out}" "${_src}")
      foreach(_def IN LISTS HLSL_DEFINES)
        list(APPEND _cmd /D "${_def}")
      endforeach()
      foreach(_inc IN LISTS HLSL_INCLUDES)
        list(APPEND _cmd /I "${_inc}")
      endforeach()
    else()
      message(FATAL_ERROR "Neither dxc nor fxc found; install DirectXShaderCompiler or Windows SDK tools")
    endif()

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${_cmd}
      DEPENDS "${_src}"
      COMMENT "Compiling HLSL: ${_src} -> ${_out}"
      VERBATIM
    )
    list(APPEND _outputs "${_out}")
  endforeach()

  # A phony target that builds all shader outputs:
  add_custom_target(${target}_shaders DEPENDS ${_outputs})

  # Ensure the main target builds shaders first:
  add_dependencies(${target} ${target}_shaders)

  # Optional: copy compiled shaders next to the exe after linking (same directory is OK here).
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HLSL_OUTPUT_DIR}" "$<TARGET_FILE_DIR:${target}>/shaders"
    COMMENT "Copying shaders to runtime folder"
  )
endfunction()
