// src/slice/SliceRendererD3D11.cpp

#include "SliceRendererD3D11.h"

#include "SliceSimulation.h"

#include "TerrainRendererD3D11.h"
#include "OrbitalRendererAdapter.h"
#include "ScreenshotCaptureD3D11.h"

#include <DirectXMath.h>

#include <cassert>
#include <limits>

#if defined(_DEBUG)
  #include <d3d11sdklayers.h>
#endif

namespace slice {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

namespace {
    // Checked narrowing: use at D3D11 API boundaries that require UINT.
    inline UINT to_uint_checked(size_t value) {
        assert(value <= static_cast<size_t>(std::numeric_limits<UINT>::max()));
        return static_cast<UINT>(value);
    }
}

// -----------------------------------------------------------------------------
// Device
// -----------------------------------------------------------------------------

void Device::create(HWND w, UINT ww, UINT hh) {
    hwnd = w;
    width = ww;
    height = hh;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // gamma-correct backbuffer
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl{};

    // Robust creation: try hardware, then WARP as fallback (helps in CI/VMs)
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &sd,
        swap.GetAddressOf(),
        dev.GetAddressOf(),
        &fl,
        ctx.GetAddressOf());

    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &sd,
            swap.GetAddressOf(),
            dev.GetAddressOf(),
            &fl,
            ctx.GetAddressOf());
    }

    HR(hr);

#if defined(_DEBUG)
    // Debug InfoQueue: break on ERROR/CORRUPTION if available
    ComPtr<ID3D11InfoQueue> q;
    if (SUCCEEDED(dev.As(&q))) {
        q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        // Filter a noisy message (optional)
        D3D11_MESSAGE_ID hide[] = { D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS };
        D3D11_INFO_QUEUE_FILTER f{};
        f.DenyList.NumIDs = _countof(hide);
        f.DenyList.pIDList = hide;
        q->AddStorageFilterEntries(&f);
    }
#endif

    recreateRT();
}

void Device::recreateRT() {
    rtv.Reset();
    dsv.Reset();
    dsTex.Reset();

    ComPtr<ID3D11Texture2D> bb;
    HR(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.GetAddressOf()));
    HR(dev->CreateRenderTargetView(bb.Get(), nullptr, rtv.GetAddressOf()));

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    HR(dev->CreateTexture2D(&td, nullptr, dsTex.GetAddressOf()));
    HR(dev->CreateDepthStencilView(dsTex.Get(), nullptr, dsv.GetAddressOf()));

    D3D11_VIEWPORT vp{};
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    ctx->RSSetViewports(1, &vp);
}

void Device::beginFrame(const float rgba[4]) {
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
    ctx->ClearRenderTargetView(rtv.Get(), rgba); // values are linear; hardware converts for sRGB RTV
    ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void Device::present(bool vsync) {
    swap->Present(vsync ? 1 : 0, 0);
}

void Device::toggleFullscreen() {
    fullscreen = !fullscreen;
    HR(swap->SetFullscreenState(fullscreen, nullptr));
    // In real apps, consider ResizeTarget for specific modes before switching.
}

// -----------------------------------------------------------------------------
// GPUTimer
// -----------------------------------------------------------------------------

void GPUTimer::init(ID3D11Device* dev, int bufferedFrames) {
    sets.resize(bufferedFrames);
    for (auto& s : sets) {
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        HR(dev->CreateQuery(&qd, s.disjoint.GetAddressOf()));
        qd.Query = D3D11_QUERY_TIMESTAMP;
        HR(dev->CreateQuery(&qd, s.start.GetAddressOf()));
        qd.Query = D3D11_QUERY_TIMESTAMP;
        HR(dev->CreateQuery(&qd, s.end.GetAddressOf()));
    }
}

void GPUTimer::begin(ID3D11DeviceContext* ctx) {
    auto& s = sets[cur];
    ctx->Begin(s.disjoint.Get());
    ctx->End(s.start.Get());
}

void GPUTimer::end(ID3D11DeviceContext* ctx) {
    auto& s = sets[cur];
    ctx->End(s.end.Get());
    ctx->End(s.disjoint.Get());
    const UINT ring = to_uint_checked(sets.size());
    cur = (cur + 1u) % ring;
}

bool GPUTimer::resolve(ID3D11DeviceContext* ctx) {
    assert(!sets.empty());
    const UINT ring = to_uint_checked(sets.size());
    const UINT prev = (cur + ring - 1u) % ring;
    auto& s = sets[prev];

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (ctx->GetData(s.disjoint.Get(), &dj, sizeof(dj), 0) != S_OK) return false;
    if (dj.Disjoint) return false;

    UINT64 t0 = 0, t1 = 0;
    if (ctx->GetData(s.start.Get(), &t0, sizeof(t0), 0) != S_OK) return false;
    if (ctx->GetData(s.end.Get(), &t1, sizeof(t1), 0) != S_OK) return false;

    lastMs = double(t1 - t0) / double(dj.Frequency) * 1000.0;
    return true;
}

// -----------------------------------------------------------------------------
// SliceRendererD3D11 (facade)
// -----------------------------------------------------------------------------

SliceRendererD3D11::SliceRendererD3D11()
    : terrain_(std::make_unique<TerrainRendererD3D11>())
    , orbital_(std::make_unique<OrbitalRendererAdapter>())
    , screenshot_(std::make_unique<ScreenshotCaptureD3D11>())
{
}

SliceRendererD3D11::~SliceRendererD3D11() = default;

void SliceRendererD3D11::create(HWND hwnd, UINT w, UINT h, const SliceSimulation& sim) {
    d.create(hwnd, w, h);

    // Common rasterizer states (wireframe toggle affects all passes)
    {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.DepthClipEnable = TRUE;
        HR(d.dev->CreateRasterizerState(&rd, rsSolid.GetAddressOf()));
        rd.FillMode = D3D11_FILL_WIREFRAME;
        HR(d.dev->CreateRasterizerState(&rd, rsWire.GetAddressOf()));
    }

    // Sub-renderers
    terrain_->create(d.dev.Get(), d.ctx.Get(), sim);
    orbital_->create(d.dev.Get());

    // GPU timers
    timerFrame.init(d.dev.Get());
    timerTerrain.init(d.dev.Get());
    timerCube.init(d.dev.Get());
    timerOrbital.init(d.dev.Get());
}

void SliceRendererD3D11::resize(UINT w, UINT h) {
    if (!d.dev) return;
    if (w == 0 || h == 0) return; // minimized

    d.width = w;
    d.height = h;

    HR(d.swap->ResizeBuffers(0, d.width, d.height, DXGI_FORMAT_UNKNOWN, 0));
    d.recreateRT();
}

void SliceRendererD3D11::regenerateHeight(const SliceSimulation& sim) {
    terrain_->regenerateHeight(d.dev.Get(), sim);
}

void SliceRendererD3D11::reloadOrbitalRenderer() {
    orbital_->reload(d.dev.Get());
}

void SliceRendererD3D11::renderFrame(const SliceSimulation& sim) {
    timerFrame.begin(d.ctx.Get());

    // Raster state (wireframe toggle)
    d.ctx->RSSetState(sim.wireframe ? rsWire.Get() : rsSolid.Get());

    // Build view/proj once and feed to sub-renderers
    XMMATRIX V = (sim.camMode == SliceSimulation::CamMode::Orbit) ? sim.orbitCam.view() : sim.freeCam.view();
    XMMATRIX P = XMMatrixPerspectiveFovLH(XMConvertToRadians(sim.fovDeg), d.width / float(d.height), 0.1f, 500.f);

    // --- Terrain ---
    timerTerrain.begin(d.ctx.Get());
    terrain_->drawTerrain(d.ctx.Get(), sim, V, P);
    timerTerrain.end(d.ctx.Get());

    // --- Cube prop ---
    timerCube.begin(d.ctx.Get());
    terrain_->drawCube(d.ctx.Get(), sim, V, P);
    timerCube.end(d.ctx.Get());

    // --- Orbital system ---
    timerOrbital.begin(d.ctx.Get());
    orbital_->draw(d.ctx.Get(), sim, V, P);
    timerOrbital.end(d.ctx.Get());

    timerFrame.end(d.ctx.Get());

    // Resolve queries from previous frames (non-blocking; may lag a few frames)
    timerFrame.resolve(d.ctx.Get());
    timerTerrain.resolve(d.ctx.Get());
    timerCube.resolve(d.ctx.Get());
    timerOrbital.resolve(d.ctx.Get());
}

bool SliceRendererD3D11::saveScreenshotBMP() {
    return screenshot_->saveBackbufferBMP(d.dev.Get(), d.ctx.Get(), d.swap.Get());
}

} // namespace slice
