function(add_dxc_shader TARGET SRC PROFILE ENTRY)
    get_filename_component(NAME_WE "${SRC}" NAME_WE)
    set(OUT "${CMAKE_BINARY_DIR}/shaders/${NAME_WE}.cso")

    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${DXC_EXECUTABLE}
                /T ${PROFILE}
                /E ${ENTRY}
                /Fo ${OUT}
                "${CMAKE_SOURCE_DIR}/${SRC}"
        DEPENDS "${CMAKE_SOURCE_DIR}/${SRC}"
        COMMENT "HLSL (DXC): ${SRC} -> ${OUT}"
        VERBATIM
    )

    # Attach compiled shader to a utility target so it actually gets built:
    set_property(TARGET ${TARGET} APPEND PROPERTY SOURCES ${OUT})
endfunction()
