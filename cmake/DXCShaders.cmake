# cmake/DXCShaders.cmake
# Tiny helper to compile one HLSL shader with DXC and produce a .cso next to other build artifacts.
# Requires: dxc.exe available in PATH (Windows SDK) or explicitly pointed to by DXC_EXE.
# Flags follow Microsoft's DXC guidance: -E (entry), -T (profile), -Fo (output), -Zi/-Fd (debug), -HV (language).  [1]
#
# [1] https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll

function(add_dxc_compute_shader OUT_VAR)
  # Usage:
  #   add_dxc_compute_shader( OUT_VAR
  #     SOURCE  <path/to/shader.hlsl>
  #     OUTPUT  <name.cso>
  #     ENTRY   <main>         # optional, default main
  #     TARGET  <cs_6_7>       # optional, default cs_6_7
  #     INCLUDES <dir1> <dir2> # optional include dirs
  #     DEFINES  <K=V> <NAME>  # optional macro defs (no leading -D)
  #     DEPENDS  <file1> ...   # extra dependencies (e.g., .hlsli)
  #     SUBDIR   <shaders>     # output subdir under binary dir; default "shaders"
  #   )

  set(options)
  set(oneValue SOURCE OUTPUT ENTRY TARGET SUBDIR)
  set(multiValue INCLUDES DEFINES DEPENDS)
  cmake_parse_arguments(DXC "${options}" "${oneValue}" "${multiValue}" ${ARGN})

  if(NOT DXC_SOURCE)
    message(FATAL_ERROR "add_dxc_compute_shader: SOURCE is required")
  endif()

  if(NOT DXC_OUTPUT)
    get_filename_component(_name "${DXC_SOURCE}" NAME_WE)
    set(DXC_OUTPUT "${_name}.cso")
  endif()

  if(NOT DXC_ENTRY)
    set(DXC_ENTRY "main")
  endif()

  if(NOT DXC_TARGET)
    set(DXC_TARGET "cs_6_7")
  endif()

  if(NOT DXC_SUBDIR)
    set(DXC_SUBDIR "shaders")
  endif()

  # Find DXC (Windows SDK typically puts dxc.exe on PATH; if not, let users set DXC_EXE cache var)
  if(NOT DEFINED DXC_EXE)
    find_program(DXC_EXE NAMES dxc.exe dxc)
  endif()
  if(NOT DXC_EXE)
    message(FATAL_ERROR "dxc.exe not found. Install the Windows SDK or add DXC_EXE to your CMake cache.")
  endif()

  # Convert INCLUDES/DEFINES to DXC args
  set(_inc_args)
  foreach(dir IN LISTS DXC_INCLUDES)
    list(APPEND _inc_args -I "$<IF:$<BOOL:${dir}>,${dir},.>")
  endforeach()

  set(_def_args)
  foreach(def IN LISTS DXC_DEFINES)
    list(APPEND _def_args -D "${def}")
  endforeach()

  # Where to place the compiled object
  set(_out "${CMAKE_BINARY_DIR}/${DXC_SUBDIR}/${DXC_OUTPUT}")
  get_filename_component(_out_dir "${_out}" DIRECTORY)

  add_custom_command(
    OUTPUT "${_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
    COMMAND "${DXC_EXE}"
            -nologo
            -T ${DXC_TARGET}
            -E ${DXC_ENTRY}
            -HV 2021
            $<$<CONFIG:Debug>:-Zi -Fd "${_out}.pdb" -Qembed_debug -Od>
            $<$<NOT:$<CONFIG:Debug>>:-O3>
            ${_inc_args}
            ${_def_args}
            -Fo "${_out}"
            "${DXC_SOURCE}"
    MAIN_DEPENDENCY "${DXC_SOURCE}"
    DEPENDS ${DXC_DEPENDS}
    COMMENT "DXC ${DXC_TARGET} ${DXC_SOURCE} -> ${_out}"
    VERBATIM
    COMMAND_EXPAND_LISTS
  )

  # Return the path to the compiled shader
  set(${OUT_VAR} "${_out}" PARENT_SCOPE)
endfunction()
