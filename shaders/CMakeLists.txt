# shaders/CMakeLists.txt
set(SHADERS
  shaders/fullscreen_triangle.hlsl
  shaders/terrain_ps.hlsl
  shaders/terrain_vs.hlsl
)

# Explicit shader model/profile and entry point per file.
# If you rename your HLSL entry functions, just update SHADER_ENTRY_POINTS.
set(SHADER_PROFILES
  vs_6_7   # fullscreen_triangle.hlsl
  ps_6_7   # terrain_ps.hlsl
  vs_6_7   # terrain_vs.hlsl
)

set(SHADER_ENTRY_POINTS
  FullScreenTriangleVS  # fullscreen_triangle.hlsl
  main                  # terrain_ps.hlsl
  main                  # terrain_vs.hlsl
)

set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${OUTPUT_DIR})

find_program(DXC_EXE dxc PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc" NO_DEFAULT_PATH)
if (NOT DXC_EXE) # fall back to PATH
  find_program(DXC_EXE dxc)
endif()

# Iterate over sources, profiles, and entrypoints in parallel (CMake 3.17+).
foreach(SRC PROFILE ENTRY IN ZIP_LISTS SHADERS SHADER_PROFILES SHADER_ENTRY_POINTS)
  get_filename_component(NAME_WE ${SRC} NAME_WE)

  add_custom_command(
    OUTPUT ${OUTPUT_DIR}/${NAME_WE}.dxil
    COMMAND ${DXC_EXE}
            -T ${PROFILE}
            -E ${ENTRY}
            -Fo ${OUTPUT_DIR}/${NAME_WE}.dxil
            ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
    DEPENDS ${SRC}
    COMMENT "Compiling ${SRC} with DXC (${PROFILE}, entry=${ENTRY})"
  )
  list(APPEND DXIL_OUTPUTS ${OUTPUT_DIR}/${NAME_WE}.dxil)
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${DXIL_OUTPUTS})
