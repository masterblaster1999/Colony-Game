# --- Vertical Slice demo ----------------------------------------------------
add_executable(ColonySlice
    src/slice/VerticalSlice.cpp
)

target_compile_features(ColonySlice PRIVATE cxx_std_17)
# Link the DX11 stuff that ships with the Windows SDK
target_link_libraries(ColonySlice PRIVATE d3d11 dxgi d3dcompiler)
