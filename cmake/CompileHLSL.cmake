# Requires: DXC_EXECUTABLE cache var or find_program
function(compile_hlsl OUTVAR SHADER ENTRY PROFILE)
  get_filename_component(_name "${SHADER}" NAME_WE)
  set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_name}.cso")
  add_custom_command(
    OUTPUT "${_out}"
    COMMAND "${DXC_EXECUTABLE}"
            -T "${PROFILE}"      # e.g., vs_6_7 / ps_6_7
            -E "${ENTRY}"        # e.g., VSMain / PSMain
            -Fo "${_out}"
            -Qembed_debug -Zi    # embed debug info for RenderDoc/PIX
            -all_resources_bound
            "${SHADER}"
    DEPENDS "${SHADER}"
    VERBATIM
  )
  set(${OUTVAR} "${_out}" PARENT_SCOPE)
endfunction()
