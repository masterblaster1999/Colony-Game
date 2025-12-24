// src/slice/SliceAppWin32.h
#pragma once

/*
    SliceAppWin32
    ------------
    Win32 window + message pump extracted from the original monolithic VerticalSlice.cpp.

    This module wires together:
      - SliceSimulation (state + input toggles)
      - SliceRendererD3D11 (D3D resources + rendering)

    Behavior is intentionally kept identical to the original demo.
*/

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

namespace slice {

class SliceSimulation;
class SliceRendererD3D11;

struct SliceAppConfig {
    UINT width = 1280;
    UINT height = 720;
};

// CLI args:  --seed <uint>
uint32_t ParseSeedArg(LPWSTR cmdLine, uint32_t defaultSeed = 1337);

// Runs the Win32 demo loop. Returns process exit code.
int RunSliceApp(HINSTANCE hi, LPWSTR cmdLine, SliceSimulation& sim, SliceRendererD3D11& renderer, const SliceAppConfig& cfg = {});

} // namespace slice
