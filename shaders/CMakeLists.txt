# shaders/CMakeLists.txt
set(SHADERS
  shaders/fullscreen_triangle.hlsl
  shaders/terrain_ps.hlsl
  shaders/terrain_vs.hlsl
)
set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${OUTPUT_DIR})

find_program(DXC_EXE dxc PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc" NO_DEFAULT_PATH)
if (NOT DXC_EXE) # fall back to PATH
  find_program(DXC_EXE dxc)
endif()

foreach(SRC ${SHADERS})
  get_filename_component(NAME_WE ${SRC} NAME_WE)
  # Detect profile from filename convention, or set explicitly
  set(PROFILE ps_6_7)
  if (NAME_WE MATCHES "_vs$")  set(PROFILE vs_6_7) endif()
  if (NAME_WE MATCHES "_cs$")  set(PROFILE cs_6_7) endif()

  add_custom_command(
    OUTPUT ${OUTPUT_DIR}/${NAME_WE}.dxil
    COMMAND ${DXC_EXE} -T ${PROFILE} -E main -Fo ${OUTPUT_DIR}/${NAME_WE}.dxil ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
    DEPENDS ${SRC}
    COMMENT "Compiling ${SRC} with DXC"
  )
  list(APPEND DXIL_OUTPUTS ${OUTPUT_DIR}/${NAME_WE}.dxil)
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${DXIL_OUTPUTS})
