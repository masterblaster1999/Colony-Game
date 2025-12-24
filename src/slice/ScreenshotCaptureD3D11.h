// src/slice/ScreenshotCaptureD3D11.h
#pragma once

/*
    ScreenshotCaptureD3D11
    ---------------------
    Patch4 split: backbuffer readback + BMP write.

    Keeps behavior identical to the original VerticalSlice demo:
      - filename: Screenshot_YYYYMMDD_HHMMSS.bmp
      - BMP: 32bpp BGRA, top-down (negative height)
*/

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

namespace slice {

class ScreenshotCaptureD3D11 {
public:
    ScreenshotCaptureD3D11() = default;

    bool saveBackbufferBMP(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swap);
};

} // namespace slice
