# renderer/CMakeLists.txt
#
# Windows-only renderer build + "single-owner" platform static lib.
# - Only colony_platform_win compiles DeviceResources.*
# - Renderer and the rest of the app link to that one library.
# - DXC is used to compile HLSL (SM 6.x).
#
cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Windows-only guard
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "This project configuration builds Windows-only (Direct3D 12).")
endif()

# ---------------------------------------------------------------------------
# Find DXC (DirectX Shader Compiler)
# ---------------------------------------------------------------------------
# Try PATH first, then common vcpkg locations.
find_program(DXC_EXECUTABLE
    NAMES dxc
    HINTS
        "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/dxc"
        "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/tools/dxc"
)

if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "DXC not found. Please install Microsoft DirectX Shader Compiler (dxc).")
endif()

# ---------------------------------------------------------------------------
# Helper: compile_hlsl(out_var src entry profile [INCLUDE_DIRS ...] [DEFINES ...])
# Writes dxil outputs into ${CMAKE_CURRENT_BINARY_DIR}/shaders/
# ---------------------------------------------------------------------------
function(compile_hlsl OUT_VAR SRC_FILE ENTRY PROFILE)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs INCLUDE_DIRS DEFINES)
    cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Build include and define args
    set(INC_ARGS "")
    foreach(inc IN LISTS HLSL_INCLUDE_DIRS)
        list(APPEND INC_ARGS -I "${inc}")
    endforeach()

    set(DEF_ARGS "")
    foreach(def IN LISTS HLSL_DEFINES)
        list(APPEND DEF_ARGS -D "${def}")
    endforeach()

    # Output file name: <basename>.<profile>.dxil
    get_filename_component(_src_abs "${SRC_FILE}" ABSOLUTE)
    get_filename_component(_name_we "${_src_abs}" NAME_WE)
    string(REPLACE ":" "_" _profile_tag "${PROFILE}")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_name_we}.${_profile_tag}.dxil")

    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${DXC_EXECUTABLE}"
                -T ${PROFILE}
                -E ${ENTRY}
                $<$<CONFIG:Debug>:-Od -Zi -Qembed_debug>
                $<$<CONFIG:RelWithDebInfo>:-O3 -Zi -Qembed_debug>
                $<$<CONFIG:Release>:-O3>
                ${INC_ARGS} ${DEF_ARGS}
                -Fo "${_out}"
                "${_src_abs}"
        DEPENDS "${_src_abs}"
        COMMENT "DXC ${SRC_FILE} (${ENTRY}/${PROFILE})"
        VERBATIM
    )

    set(${OUT_VAR} "${_out}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# PATCH B: Single-owner Windows platform static lib
#   Only this target compiles DeviceResources.* to avoid LNK4006 duplicates.
#   Other targets link to it; they do NOT list DeviceResources.cpp themselves.
# ---------------------------------------------------------------------------
add_library(colony_platform_win STATIC
    "${CMAKE_SOURCE_DIR}/platform/win/DeviceResources.cpp"
    "${CMAKE_SOURCE_DIR}/platform/win/DeviceResources.h"
)
target_compile_features(colony_platform_win PUBLIC cxx_std_20)
target_include_directories(colony_platform_win PUBLIC "${CMAKE_SOURCE_DIR}/platform/win")
target_link_libraries(colony_platform_win PUBLIC d3d12 dxgi dxguid)
add_library(Colony::PlatformWin ALIAS colony_platform_win)

# Optional: If your renderer target already exists, make it consume the platform lib.
if(TARGET colony_renderer)
    target_link_libraries(colony_renderer PUBLIC Colony::PlatformWin)
endif()

# Optional: If your main exe target exists, link platform once here too.
if(TARGET colony_exe)
    target_link_libraries(colony_exe PRIVATE Colony::PlatformWin)
endif()

# ---------------------------------------------------------------------------
# SHADERS — from your snippet (paths anchored at ${CMAKE_SOURCE_DIR})
# Adjust ENTRY names if your HLSL uses different entry points.
# ---------------------------------------------------------------------------

# List of HLSL shaders (from your error log / repo)
set(COLONY_HLSL_SOURCES
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/raster/FullScreen.vs.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/WaterGerstner.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
)

# Compile each shader with the correct entry/profile.
set(COMPILED_SHADERS)

# Atmosphere sky – likely a full-screen PS
compile_hlsl(SH_ATMOSPHERE_SKY
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"
    PSMain
    ps_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_ATMOSPHERE_SKY}")

# FullScreen VS
compile_hlsl(SH_FULLSCREEN_VS
    "${CMAKE_SOURCE_DIR}/shaders/raster/FullScreen.vs.hlsl"
    VSMain
    vs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders/raster"
)
list(APPEND COMPILED_SHADERS "${SH_FULLSCREEN_VS}")

# FullScreenTriangle VS
compile_hlsl(SH_FULLSCREEN_TRIANGLE_VS
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    VSMain
    vs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common"
)
list(APPEND COMPILED_SHADERS "${SH_FULLSCREEN_TRIANGLE_VS}")

# 2D batch PS
compile_hlsl(SH_BATCH2D_PS
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"
    PSMain
    ps_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_BATCH2D_PS}")

# Water / Gerstner waves (adjust entry if your shader is VS/CS)
compile_hlsl(SH_WATER_GERSTNER
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/WaterGerstner.hlsl"
    PSMain
    ps_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_WATER_GERSTNER}")

# Erosion compute shaders
compile_hlsl(SH_EROSION_THERMAL_APPLY
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
    CSMain
    cs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_EROSION_THERMAL_APPLY}")

compile_hlsl(SH_EROSION_THERMAL_FLOW
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
    CSMain
    cs_6_0
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders"
)
list(APPEND COMPILED_SHADERS "${SH_EROSION_THERMAL_FLOW}")

# Group all compiled shaders into a dedicated custom target (shows up as a VS project).
add_custom_target(ColonyShaders ALL
    DEPENDS ${COMPILED_SHADERS}
)

# If renderer is a real target, ensure it builds after shaders
if(TARGET colony_renderer)
    add_dependencies(colony_renderer ColonyShaders)
endif()
