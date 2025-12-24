// src/slice/SliceRendererD3D11.h
#pragma once

/*
    SliceRendererD3D11
    -----------------
    Thin facade for the Vertical Slice D3D11 renderer.

    Patch4 refactor goal:
      - keep the public API stable (SliceAppWin32 / SliceSimulation do not change)
      - split the heavy renderer implementation into:
          * TerrainRendererD3D11.*      (grid + height texture + terrain/cube draw)
          * OrbitalRendererAdapter.*    (glue around colony::space::OrbitalRenderer)
          * ScreenshotCaptureD3D11.*    (backbuffer readback + BMP write)

    This file keeps only:
      - the swapchain/device wrapper (Device)
      - GPU timers exposed for the title bar
      - a small forwarding API used by the app loop
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
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace slice {

class SliceSimulation;

// Internal split modules (opaque here; owned via unique_ptr)
class TerrainRendererD3D11;
class OrbitalRendererAdapter;
class ScreenshotCaptureD3D11;

struct Device {
    HWND hwnd{};
    UINT width{1280}, height{720};
    bool fullscreen{false};

    Microsoft::WRL::ComPtr<ID3D11Device>           dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    ctx;
    Microsoft::WRL::ComPtr<IDXGISwapChain>         swap;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        dsTex;

    void create(HWND w, UINT ww, UINT hh);
    void recreateRT();

    void beginFrame(const float rgba[4]);
    void present(bool vsync);

    void toggleFullscreen();
};

struct GPUTimer {
    struct Set {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        Microsoft::WRL::ComPtr<ID3D11Query> start;
        Microsoft::WRL::ComPtr<ID3D11Query> end;
    };

    std::vector<Set> sets;
    UINT cur = 0;
    double lastMs = 0.0;

    void init(ID3D11Device* dev, int bufferedFrames = 4);
    void begin(ID3D11DeviceContext* ctx);
    void end(ID3D11DeviceContext* ctx);
    bool resolve(ID3D11DeviceContext* ctx);
};

class SliceRendererD3D11 {
public:
    // Device / swapchain wrapper
    Device d;

    // Profiling (read by SliceAppWin32 for window title text)
    GPUTimer timerFrame, timerTerrain, timerCube, timerOrbital;

public:
    SliceRendererD3D11();
    ~SliceRendererD3D11();

    // Non-copyable (owns D3D resources)
    SliceRendererD3D11(const SliceRendererD3D11&) = delete;
    SliceRendererD3D11& operator=(const SliceRendererD3D11&) = delete;

    void create(HWND hwnd, UINT w, UINT h, const SliceSimulation& sim);
    void resize(UINT w, UINT h);

    void beginFrame(const float rgba[4]) { d.beginFrame(rgba); }
    void present(bool vsync) { d.present(vsync); }
    void toggleFullscreen() { d.toggleFullscreen(); }

    // Renderer-side hot actions requested by the sim.
    void regenerateHeight(const SliceSimulation& sim);
    void reloadOrbitalRenderer();

    // Main draw
    void renderFrame(const SliceSimulation& sim);

    // Screenshot (BMP, 32bpp BGRA, top-down)
    bool saveScreenshotBMP();

private:
    // Common render states used across sub-renderers
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rsSolid;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rsWire;

    // Split implementation modules
    std::unique_ptr<TerrainRendererD3D11> terrain_;
    std::unique_ptr<OrbitalRendererAdapter> orbital_;
    std::unique_ptr<ScreenshotCaptureD3D11> screenshot_;
};

} // namespace slice
