# renderer/CMakeLists.txt (snippet)

# List of HLSL shaders (paths from your error log)
set(COLONY_HLSL_SOURCES
    renderer/Shaders/AtmosphereSky.hlsl
    shaders/raster/FullScreen.vs.hlsl
    renderer/Shaders/Common/FullScreenTriangleVS.hlsl
    renderer/Shaders/Batch2D_ps.hlsl
    renderer/Shaders/WaterGerstner.hlsl
    renderer/Shaders/erosion_thermal_apply_cs.hlsl
    renderer/Shaders/erosion_thermal_flow_cs.hlsl
)

# Compile each shader with the correct entry/profile.
# NOTE: Adjust ENTRY names to match your actual HLSL files.
set(COMPILED_SHADERS)

# Atmosphere sky – likely a pixel shader full-screen pass
compile_hlsl(SH_ATMOSPHERE_SKY
    "renderer/Shaders/AtmosphereSky.hlsl"
    PSMain          # <-- change if your entry is named differently
    ps_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_ATMOSPHERE_SKY}")

# FullScreen VS
compile_hlsl(SH_FULLSCREEN_VS
    "shaders/raster/FullScreen.vs.hlsl"
    VSMain          # or 'main', adjust as needed
    vs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders/raster"
)
list(APPEND COMPILED_SHADERS "${SH_FULLSCREEN_VS}")

# FullScreenTriangle VS
compile_hlsl(SH_FULLSCREEN_TRIANGLE_VS
    "renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    VSMain
    vs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common"
)
list(APPEND COMPILED_SHADERS "${SH_FULLSCREEN_TRIANGLE_VS}")

# 2D batch PS
compile_hlsl(SH_BATCH2D_PS
    "renderer/Shaders/Batch2D_ps.hlsl"
    PSMain          # or whatever your pixel entry is called
    ps_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_BATCH2D_PS}")

# Water / Gerstner waves
compile_hlsl(SH_WATER_GERSTNER
    "renderer/Shaders/WaterGerstner.hlsl"
    PSMain          # or VSMain/CSMain depending on how you wrote it
    ps_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_WATER_GERSTNER}")

# Erosion compute shaders
compile_hlsl(SH_EROSION_THERMAL_APPLY
    "renderer/Shaders/erosion_thermal_apply_cs.hlsl"
    CSMain          # adjust to your actual entry
    cs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_EROSION_THERMAL_APPLY}")

compile_hlsl(SH_EROSION_THERMAL_FLOW
    "renderer/Shaders/erosion_thermal_flow_cs.hlsl"
    CSMain          # adjust to your actual entry
    cs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_EROSION_THERMAL_FLOW}")

# Group all compiled shaders into a dedicated custom target.
# This will generate ColonyShaders.vcxproj in Visual Studio.
add_custom_target(ColonyShaders ALL
    DEPENDS ${COMPILED_SHADERS}
)
