# cmake/ColonyShaders.cmake
#
# Defines a custom target "ColonyShaders" that compiles all runtime HLSL shaders
# using dxc.exe and installs the resulting .cso files.
#
# Assumptions:
#   - This file is included from your top-level CMakeLists.txt or a sub-dir.
#   - You are building on Windows with DXC available (via Windows SDK or vcpkg).
#   - Every shader file listed here has an HLSL entry point called "main".
#   - Include-only HLSL (terrain_splat_common.hlsl, water_tiny.hlsl) are not
#     compiled on their own; they’re just #included by other shaders.

# Root of the source tree (Colony-Game)
set(COLONY_SHADER_ROOT "${CMAKE_SOURCE_DIR}")

# Directory where compiled shaders (.cso) will be placed in the build tree.
set(COLONY_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${COLONY_SHADER_OUTPUT_DIR}")

# HLSL files that are "include-only" / library code. These are *not* compiled
# to .cso on their own, but are tracked as DEPENDS so rebuilds happen when they change.
set(COLONY_SHADER_INCLUDE_ONLY
    "${COLONY_SHADER_ROOT}/shaders/terrain_splat_common.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/water_tiny.hlsl"
)

# Actual entry-point HLSL shaders (only these are compiled to .cso)
# Paths are relative to the repository root layout you showed in the build log.
set(COLONY_SHADER_SOURCES
    "${COLONY_SHADER_ROOT}/renderer/Shaders/AtmosphereSky.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/WaterGerstner.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/VolumetricClouds.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Compute/HydraulicErosion.compute.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/PoissonDisk_cs.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Weather/PrecipitationCS.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Weather/PrecipitationPS.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Weather/PrecipitationVS.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Atmosphere/SkyPS.hlsl"

    "${COLONY_SHADER_ROOT}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/VoxelIso_MT.compute.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Batch2D_vs.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Batch2D_ps.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/CS_GenerateHeight.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/CS_HeightToNormal.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/CaveTriPlanarSplat.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/DeferredDecalProjector.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/mesh_vs.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/mesh_ps.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Clouds/CloudNoiseCS.hlsl"
    "${COLONY_SHADER_ROOT}/renderer/Shaders/Clouds/CloudRaymarchPS.hlsl"

    "${COLONY_SHADER_ROOT}/shaders/raster/FullScreen.vs.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/raster/ToneMap.ps.hlsl"

    "${COLONY_SHADER_ROOT}/shaders/Terrain.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/TerrainSplatExample.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/ThermalApplyCS.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/ThermalOutflowCS.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/Starfield.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/quad_vs.hlsl"
    "${COLONY_SHADER_ROOT}/shaders/quad_ps.hlsl"

    # NOTE: intentionally *not* listing:
    #   shaders/terrain_splat_common.hlsl
    #   shaders/water_tiny.hlsl
    # Those are include-only and live in COLONY_SHADER_INCLUDE_ONLY instead.
)

# Try to find DXC (DirectX Shader Compiler) in typical Windows locations.
# If you use vcpkg's directx-dxc port or have dxc in PATH, this should succeed.
# (You can always set DXC_EXECUTABLE in your toolchain cache if needed.)
find_program(DXC_EXECUTABLE
    NAMES dxc dxc.exe
    HINTS
        "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
        "$ENV{DXSDK_DIR}/Utilities/bin/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
)

if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc.exe not found. Install the Windows 10 SDK or DirectXShaderCompiler and/or add it to PATH.")
endif()

# Internal list of compiled shader outputs (.cso) – populated by the helper function.
set(COLONY_COMPILED_SHADERS "")

# Helper to add a DXC compile step for a given HLSL source.
# It guesses the shader profile from the filename:
#   * *vs.hlsl  -> vs_6_0
#   * *ps.hlsl  -> ps_6_0
#   * *_cs.hlsl or *CS.hlsl or *compute.hlsl -> cs_6_0
#   * anything else -> ps_6_0 (pixel) by default
function(colony_add_hlsl_shader SRC)
    get_filename_component(NAME_WE "${SRC}" NAME_WE)
    set(OUT "${COLONY_SHADER_OUTPUT_DIR}/${NAME_WE}.cso")

    string(TOLOWER "${SRC}" SRC_LOWER)

    # Default profile: pixel shader
    set(PROFILE "ps_6_0")

    if(SRC_LOWER MATCHES "vs\\.hlsl$")
        set(PROFILE "vs_6_0")
    elseif(SRC_LOWER MATCHES "ps\\.hlsl$")
        set(PROFILE "ps_6_0")
    elseif(SRC_LOWER MATCHES "_cs\\.hlsl$"
           OR SRC_LOWER MATCHES "cs\\.hlsl$"
           OR SRC_LOWER MATCHES "compute\\.hlsl$")
        set(PROFILE "cs_6_0")
    endif()

    add_custom_command(
        OUTPUT "${OUT}"
        COMMAND "${DXC_EXECUTABLE}" -nologo
                -E main                 # entry point name – adjust if needed
                -T "${PROFILE}"         # shader model 6.x profile
                -Fo "${OUT}"
                "${SRC}"
        DEPENDS "${SRC}" ${COLONY_SHADER_INCLUDE_ONLY}
        COMMENT "HLSL (DXC): ${SRC} -> ${OUT}"
        VERBATIM
    )

    list(APPEND COLONY_COMPILED_SHADERS "${OUT}")
    set(COLONY_COMPILED_SHADERS "${COLONY_COMPILED_SHADERS}" PARENT_SCOPE)
endfunction()

# Register a compile command for every shader in COLONY_SHADER_SOURCES
foreach(SRC IN LISTS COLONY_SHADER_SOURCES)
    colony_add_hlsl_shader("${SRC}")
endforeach()

# The actual custom target that Visual Studio / MSBuild will see as ColonyShaders.vcxproj.
# Marked ALL so it's built by default as part of the normal build.
add_custom_target(ColonyShaders ALL
    DEPENDS ${COLONY_COMPILED_SHADERS}
)

# Where to install the compiled shaders when you run `cmake --install`.
# This will give you <install-prefix>/shaders/*.cso.
set(COLONY_SHADER_INSTALL_DIR "shaders")

install(
    FILES ${COLONY_COMPILED_SHADERS}
    DESTINATION "${COLONY_SHADER_INSTALL_DIR}"
)
