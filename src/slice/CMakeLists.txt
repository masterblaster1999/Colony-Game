# --- Vertical Slice demo ----------------------------------------------------
#
# NOTE:
# - This target isn't wired into the root build by default, but this file is kept
#   correct so you can `add_subdirectory(src/slice)` (or build it standalone)
#   without hand-editing source lists.

add_executable(ColonySlice WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/VerticalSlice.cpp

    # Split modules (extracted from VerticalSlice.cpp)
    ${CMAKE_CURRENT_SOURCE_DIR}/SliceAppWin32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SliceSimulation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SliceRendererD3D11.cpp

    # Patch4: split the heavy renderer implementation further
    ${CMAKE_CURRENT_SOURCE_DIR}/TerrainRendererD3D11.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OrbitalRendererAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScreenshotCaptureD3D11.cpp

    # (Optional) list headers for IDE browsing
    ${CMAKE_CURRENT_SOURCE_DIR}/SliceAppWin32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SliceSimulation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SliceRendererD3D11.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TerrainRendererD3D11.h
    ${CMAKE_CURRENT_SOURCE_DIR}/OrbitalRendererAdapter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ScreenshotCaptureD3D11.h

    ${CMAKE_CURRENT_SOURCE_DIR}/ObjectiveTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ObjectiveTypes.h

    # Dependencies used by VerticalSlice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../space/OrbitalSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../render/OrbitalRenderer.cpp
)

target_compile_features(ColonySlice PRIVATE cxx_std_17)

target_include_directories(ColonySlice PRIVATE
    # Works both when this directory is added from the repo root and when
    # configuring `src/slice` as a standalone project.
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link the DX11 stuff that ships with the Windows SDK
target_link_libraries(ColonySlice PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dxguid
    shell32
)

# Keep it as a normal multi-TU build (unity builds reduce incremental iteration quality)
set_target_properties(ColonySlice PROPERTIES UNITY_BUILD OFF)
