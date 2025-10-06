# demos/erosion/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

if (NOT WIN32)
  message(FATAL_ERROR "ColonyComputeErosion is Windows-only (D3D11).")
endif()

add_executable(ColonyComputeErosion
  ErosionDemo.cpp
  ErosionCS.hlsl
)

target_compile_features(ColonyComputeErosion PRIVATE cxx_std_20)

# Link Windows SDK libs
target_link_libraries(ColonyComputeErosion PRIVATE d3d11 dxgi d3dcompiler)

# Keep runtime alongside other binaries
set_target_properties(ColonyComputeErosion PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stage/bin"
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/stage/bin"
)

# ---------------------------------------------------------------------------
# Visual Studio HLSL integration for D3D11 (cs_5_0 via FXC)
# ---------------------------------------------------------------------------
set_source_files_properties(ErosionCS.hlsl PROPERTIES
  VS_SHADER_TYPE "Compute"
  VS_SHADER_MODEL "5.0"
  VS_SHADER_ENTRYPOINT "CSMain"
  VS_SHADER_OBJECT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/ErosionCS.cso"
  VS_SHADER_DISABLE_OPTIMIZATIONS $<CONFIG:Debug>
  VS_SHADER_ENABLE_DEBUG $<CONFIG:Debug>
)

# Copy the compiled shader next to the executable so the demo can load it.
add_custom_command(TARGET ColonyComputeErosion POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_BINARY_DIR}/ErosionCS.cso"
          "$<TARGET_FILE_DIR:ColonyComputeErosion>/ErosionCS.cso"
  VERBATIM
)
