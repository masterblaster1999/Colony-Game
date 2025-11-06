# cmake/AddShaders.cmake  (create if needed or adapt your current shaders cmake)
function(add_hlsl OUTCso IN_HLSL)
  get_filename_component(_name "${IN_HLSL}" NAME_WE)
  string(REGEX MATCH "_ps$" _is_ps "${_name}")
  string(REGEX MATCH "_vs$" _is_vs "${_name}")

  if(_is_ps)
    set(_profile ps_6_0)
  elseif(_is_vs)
    set(_profile vs_6_0)
  else()
    message(FATAL_ERROR "Unknown shader stage for ${IN_HLSL}")
  endif()

  add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${_name}.cso
    COMMAND ${DXC_EXECUTABLE}
            -E main -T ${_profile}
            -Fo ${CMAKE_CURRENT_BINARY_DIR}/${_name}.cso
            ${IN_HLSL}
    DEPENDS ${IN_HLSL}
    VERBATIM)
  list(APPEND ${OUTCso} ${CMAKE_CURRENT_BINARY_DIR}/${_name}.cso)
  set(${OUTCso} "${${OUTCso}}" PARENT_SCOPE)
endfunction()
