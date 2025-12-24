// VerticalSlice.cpp - build as "ColonySlice" (WIN32)
//
// This file is intentionally kept *thin*: the original monolithic demo has been
// split into coherent modules:
//   - SliceAppWin32.*       : window + message loop
//   - SliceSimulation.*     : orbital sim + gameplay state + input toggles
//   - SliceRendererD3D11.*  : D3D11 device, shaders, meshes, timers, rendering
//
// Behavior should match the original VerticalSlice.cpp.

#include "slice/SliceAppWin32.h"
#include "slice/SliceRendererD3D11.h"
#include "slice/SliceSimulation.h"

// Keep these pragmas for users who build the slice directly in Visual Studio
// without the CMake target_link_libraries wiring.
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "shell32.lib") // CommandLineToArgvW / LocalFree

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmdLine, int) {
    slice::SliceSimulation sim;
    slice::SliceRendererD3D11 renderer;
    return slice::RunSliceApp(hi, cmdLine, sim, renderer);
}
