# cmake/AddShaders.cmake  (D3D11-safe: use FXC for SM5)
function(add_hlsl OUT_CSOS IN_HLSL)
  get_filename_component(_name "${IN_HLSL}" NAME_WE)
  string(REGEX MATCH "_ps$" _is_ps "${_name}")
  string(REGEX MATCH "_vs$" _is_vs "${_name}")
  if(_is_ps)
    set(_profile ps_5_0) # D3D11-friendly
  elseif(_is_vs)
    set(_profile vs_5_0)
  else()
    message(FATAL_ERROR "Unknown shader stage for ${IN_HLSL}")
  endif()

  # Locate fxc.exe from the installed Windows SDKs on the runner/dev box
  if(NOT DEFINED FXC_EXECUTABLE)
    # Typical SDK paths (x64 host)
    set(_fxc_candidates
      "$ENV{WindowsSdkDir}/bin/x64/fxc.exe"
      "$ENV{WindowsSdkDir}/bin/10.0.19041.0/x64/fxc.exe"
      "C:/Program Files (x86)/Windows Kits/10/bin/x64/fxc.exe"
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/fxc.exe")
    foreach(_c IN LISTS _fxc_candidates)
      if(EXISTS "${_c}")
        set(FXC_EXECUTABLE "${_c}")
        break()
      endif()
    endforeach()
    find_program(FXC_EXECUTABLE NAMES fxc.exe HINTS "$ENV{WindowsSdkDir}/bin/x64")
  endif()
  if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "fxc.exe not found; install Windows 10/11 SDK.")
  endif()

  set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_name}.cso")
  add_custom_command(
    OUTPUT  "${_out}"
    COMMAND "${FXC_EXECUTABLE}"
      /nologo /T ${_profile} /E main /Ges /Gis /Zi /Qstrip_reflect
      /Fo "${_out}" "${IN_HLSL}"
    DEPENDS "${IN_HLSL}"
    VERBATIM)
  list(APPEND ${OUT_CSOS} "${_out}")
  set(${OUT_CSOS} "${${OUT_CSOS}}" PARENT_SCOPE)
endfunction()
