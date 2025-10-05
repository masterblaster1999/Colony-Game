function(compile_hlsl IN OUT PROFILE)
  add_custom_command(
    OUTPUT ${OUT}
    COMMAND $<TARGET_FILE:dxcompiler>                      # from directx-dxc
            -T ${PROFILE} -E main
            $<$<CONFIG:Debug>:-Zi -Od -Qembed_debug>
            $<$<CONFIG:Release>:-O3 -Qstrip_debug>
            -Fo ${OUT} ${IN} -I ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${IN}
    VERBATIM)
endfunction()
