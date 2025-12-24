// src/slice/SliceRendererD3D11.h
#pragma once

/*
    SliceRendererD3D11
    -----------------
    D3D11 renderer/resources extracted from the original monolithic VerticalSlice.cpp.

    This module owns:
      - D3D11 device/swapchain/RT/DS
      - Shaders, meshes, constant buffers
      - GPU timestamp profiling queries
      - OrbitalRenderer instance (GPU resources)
      - Screenshot capture

    It consumes SliceSimulation state (cameras, toggles, orbital system, etc.).
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
#include <DirectXMath.h>

#include <cstdint>
#include <vector>

#include "render/OrbitalRenderer.h"

namespace slice {

class SliceSimulation;

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

// Height texture (R32_FLOAT)
struct HeightTexture {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    int W{}, H{};

    void create(ID3D11Device* dev, const std::vector<float>& h, int w, int hgt);
};

struct Vtx { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; };
struct VtxN { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 nrm; };

struct Mesh {
    Microsoft::WRL::ComPtr<ID3D11Buffer> vbo;
    Microsoft::WRL::ComPtr<ID3D11Buffer> ibo;
    UINT indexCount{};
};

struct FPSCounter;

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

    // Terrain
    Mesh grid{};
    HeightTexture heightTex{};
    Microsoft::WRL::ComPtr<ID3D11VertexShader> terrainVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  terrainPS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  terrainIL;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbCamera;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbTerrain;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampLinear;

    // Cube
    Mesh cube{};
    Microsoft::WRL::ComPtr<ID3D11VertexShader> colorVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  colorPS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  colorIL;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbCameraCube;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbColor;

    // States
    Microsoft::WRL::ComPtr<ID3D11BlendState>      blendAlpha;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rsSolid;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rsWire;

    // Orbital renderer (owns D3D resources)
    colony::space::OrbitalRenderer orender;

    // Profiling
    GPUTimer timerFrame, timerTerrain, timerCube, timerOrbital;

public:
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
};

} // namespace slice
