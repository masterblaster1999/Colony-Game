// src/slice/SliceRendererD3D11.cpp

#include "SliceRendererD3D11.h"

#include "SliceSimulation.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#if defined(_DEBUG)
  #include <d3d11sdklayers.h>
#endif

namespace slice {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const wchar_t* kTerrainVS = L"res/shaders/Slice_TerrainVS.hlsl";
static const wchar_t* kTerrainPS = L"res/shaders/Slice_TerrainPS.hlsl";
static const wchar_t* kColorVS = L"res/shaders/Slice_ColorVS.hlsl";
static const wchar_t* kColorPS = L"res/shaders/Slice_ColorPS.hlsl";

#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

// ---- local helpers (file-local, safe) ---------------------------------------
namespace {
    // Checked narrowing: use at D3D11 API boundaries that require UINT.
    inline UINT to_uint_checked(size_t value) {
        assert(value <= static_cast<size_t>(std::numeric_limits<UINT>::max()));
        return static_cast<UINT>(value);
    }

    // Align up to 16 bytes and return as UINT (for constant buffers).
    inline UINT align16_uint_size(size_t value) {
        size_t a = (value + 15u) & ~size_t(15u);
        assert(a <= static_cast<size_t>(std::numeric_limits<UINT>::max()));
        return static_cast<UINT>(a);
    }

    template<class T>
    void UpdateCB(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const T& data) {
        D3D11_MAPPED_SUBRESOURCE ms{};
        HR(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
        memcpy(ms.pData, &data, sizeof(T));
        ctx->Unmap(cb, 0);
    }

    // HLSL compile helper
    ComPtr<ID3DBlob> Compile(const wchar_t* file, const char* entry, const char* target) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    #if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #endif
        ComPtr<ID3DBlob> blob, errs;
        HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, target, flags, 0, blob.GetAddressOf(), errs.GetAddressOf());
        if (FAILED(hr)) {
            if (errs) OutputDebugStringA((const char*)errs->GetBufferPointer());
            HR(hr);
        }
        return blob;
    }

    // CPU value-noise heightmap
    inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
        uint32_t h = x * 0x9E3779B1u + y * 0x85EBCA77u + seed * 0xC2B2AE3Du;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        return h;
    }
    inline float rand01(uint32_t x, uint32_t y, uint32_t seed) {
        return (float)((hash2(x, y, seed) & 0x00FFFFFFu) / double(0x01000000));
    }
    float fade(float t) { return t * t * (3.f - 2.f * t); }

    void makeHeightmap(std::vector<float>& out, int W, int H, uint32_t seed, float scale, int octaves = 4, float persistence = 0.5f) {
        out.resize(size_t(W) * H);
        for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float xf = x / scale, yf = y / scale;
            float amp = 1.f, sum = 0.f, norm = 0.f;
            int xi = int(std::floor(xf)), yi = int(std::floor(yf));
            float tx = xf - xi, ty = yf - yi;
            for (int o = 0; o < octaves; ++o) {
                int step = 1 << o;
                float u = tx / step, v = ty / step;
                int X0 = (xi >> o), Y0 = (yi >> o);
                float v00 = rand01(X0, Y0, seed);
                float v10 = rand01(X0 + 1, Y0, seed);
                float v01 = rand01(X0, Y0 + 1, seed);
                float v11 = rand01(X0 + 1, Y0 + 1, seed);
                float sx = fade(u), sy = fade(v);
                float ix0 = v00 + (v10 - v00) * sx;
                float ix1 = v01 + (v11 - v01) * sx;
                float val = ix0 + (ix1 - ix0) * sy;
                sum += val * amp;
                norm += amp;
                amp *= persistence;
            }
            out[size_t(y) * W + x] = sum / norm; // 0..1
        }
    }

    // Mesh helpers
    Mesh makeGrid(ID3D11Device* dev, int N, float tileWorld) {
        std::vector<Vtx> v; v.reserve(size_t(N) * N);
        std::vector<uint32_t> idx; idx.reserve(size_t(N - 1) * (N - 1) * 6);

        float half = (N - 1) * tileWorld * 0.5f;
        for (int z = 0; z < N; ++z)
        for (int x = 0; x < N; ++x) {
            float wx = x * tileWorld - half;
            float wz = z * tileWorld - half;
            v.push_back({ XMFLOAT3(wx, 0, wz), XMFLOAT2(x / float(N - 1), z / float(N - 1)) });
        }
        for (int z = 0; z < N - 1; ++z)
        for (int x = 0; x < N - 1; ++x) {
            uint32_t i0 = z * N + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + N;
            uint32_t i3 = i2 + 1;
            idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
            idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
        }

        Mesh m{};
        m.indexCount = to_uint_checked(idx.size());

        // VB
        {
            const size_t vbBytes = v.size() * sizeof(Vtx);
            assert(vbBytes <= std::numeric_limits<UINT>::max());
            D3D11_BUFFER_DESC vb{};
            vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vb.ByteWidth = static_cast<UINT>(vbBytes);
            vb.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA sdv{ v.data(), 0, 0 };
            HR(dev->CreateBuffer(&vb, &sdv, m.vbo.GetAddressOf()));
        }
        // IB
        {
            const size_t ibBytes = idx.size() * sizeof(uint32_t);
            assert(ibBytes <= std::numeric_limits<UINT>::max());
            D3D11_BUFFER_DESC ib{};
            ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
            ib.ByteWidth = static_cast<UINT>(ibBytes);
            ib.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA sdi{ idx.data(), 0, 0 };
            HR(dev->CreateBuffer(&ib, &sdi, m.ibo.GetAddressOf()));
        }

        return m;
    }

    Mesh makeCube(ID3D11Device* dev, float s) {
        const float h = s * 0.5f;
        VtxN verts[] = {
            {{ h,-h,-h},{1,0,0}}, {{ h,-h, h},{1,0,0}}, {{ h, h, h},{1,0,0}}, {{ h, h,-h},{1,0,0}},
            {{-h,-h, h},{-1,0,0}},{{-h,-h,-h},{-1,0,0}},{{-h, h,-h},{-1,0,0}},{{-h, h, h},{-1,0,0}},
            {{-h, h,-h},{0,1,0}}, {{ h, h,-h},{0,1,0}}, {{ h, h, h},{0,1,0}}, {{-h, h, h},{0,1,0}},
            {{-h,-h, h},{0,-1,0}},{{ h,-h, h},{0,-1,0}},{{ h,-h,-h},{0,-1,0}},{{-h,-h,-h},{0,-1,0}},
            {{-h,-h, h},{0,0,1}}, {{-h, h, h},{0,0,1}}, {{ h, h, h},{0,0,1}}, {{ h,-h, h},{0,0,1}},
            {{ h,-h,-h},{0,0,-1}},{{ h, h,-h},{0,0,-1}},{{-h, h,-h},{0,0,-1}},{{-h,-h,-1},{0,0,-1}}
        };
        uint16_t idx[] = {
            0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,
            12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23
        };

        Mesh m{};
        m.indexCount = to_uint_checked(_countof(idx));

        D3D11_BUFFER_DESC vb{};
        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb.ByteWidth = to_uint_checked(sizeof(verts));
        vb.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA sdv{ verts, 0, 0 };
        HR(dev->CreateBuffer(&vb, &sdv, m.vbo.GetAddressOf()));

        D3D11_BUFFER_DESC ib{};
        ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ib.ByteWidth = to_uint_checked(sizeof(idx));
        ib.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA sdi{ idx, 0, 0 };
        HR(dev->CreateBuffer(&ib, &sdi, m.ibo.GetAddressOf()));

        return m;
    }

    // Pipeline constant buffers
    struct CameraCB {
        XMFLOAT4X4 World, View, Proj;
        float HeightAmplitude; XMFLOAT2 HeightTexel; float TileWorld; float _pad0;
    };
    struct TerrainCB {
        XMFLOAT3 LightDir; float _pad0;
        XMFLOAT3 BaseColor; float HeightScale;
        XMFLOAT2 HeightTexel; XMFLOAT2 _pad1;
    };
    struct ColorCB {
        XMFLOAT3 LightDir; float _pad0;
        XMFLOAT3 Albedo;   float _pad1;
    };

} // namespace

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
        // Example: filter noisy messages (optional)
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
// HeightTexture
// -----------------------------------------------------------------------------

void HeightTexture::create(ID3D11Device* dev, const std::vector<float>& h, int w, int hgt) {
    W = w;
    H = hgt;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = hgt;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = h.data();
    srd.SysMemPitch = UINT(sizeof(float) * w);

    HR(dev->CreateTexture2D(&td, &srd, tex.GetAddressOf()));

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;

    HR(dev->CreateShaderResourceView(tex.Get(), &sd, srv.GetAddressOf()));
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
// SliceRendererD3D11
// -----------------------------------------------------------------------------

void SliceRendererD3D11::create(HWND hwnd, UINT w, UINT h, const SliceSimulation& sim) {
    d.create(hwnd, w, h);

    // Heightmap + grid
    regenerateHeight(sim);
    grid = makeGrid(d.dev.Get(), sim.HM, sim.TileWorld);

    // Terrain pipeline
    auto vsb = Compile(kTerrainVS, "main", "vs_5_0");
    auto psb = Compile(kTerrainPS, "main", "ps_5_0");
    HR(d.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, terrainVS.GetAddressOf()));
    HR(d.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, terrainPS.GetAddressOf()));

    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0 ,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT   ,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    HR(d.dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), terrainIL.GetAddressOf()));

    // Cube pipeline
    auto vsb2 = Compile(kColorVS, "main", "vs_5_0");
    auto psb2 = Compile(kColorPS, "main", "ps_5_0");
    HR(d.dev->CreateVertexShader(vsb2->GetBufferPointer(), vsb2->GetBufferSize(), nullptr, colorVS.GetAddressOf()));
    HR(d.dev->CreatePixelShader(psb2->GetBufferPointer(), psb2->GetBufferSize(), nullptr, colorPS.GetAddressOf()));

    D3D11_INPUT_ELEMENT_DESC il2[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0 ,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL"  ,0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    HR(d.dev->CreateInputLayout(il2, 2, vsb2->GetBufferPointer(), vsb2->GetBufferSize(), colorIL.GetAddressOf()));

    // CBuffers (align ByteWidth to 16 as required)
    D3D11_BUFFER_DESC cbd{};
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cbd.ByteWidth = align16_uint_size(sizeof(CameraCB));
    HR(d.dev->CreateBuffer(&cbd, nullptr, cbCamera.GetAddressOf()));

    cbd.ByteWidth = align16_uint_size(sizeof(CameraCB));
    HR(d.dev->CreateBuffer(&cbd, nullptr, cbCameraCube.GetAddressOf()));

    D3D11_BUFFER_DESC cbd2 = cbd;
    cbd2.ByteWidth = align16_uint_size(sizeof(TerrainCB));
    HR(d.dev->CreateBuffer(&cbd2, nullptr, cbTerrain.GetAddressOf()));

    D3D11_BUFFER_DESC cbd3 = cbd;
    cbd3.ByteWidth = align16_uint_size(sizeof(ColorCB));
    HR(d.dev->CreateBuffer(&cbd3, nullptr, cbColor.GetAddressOf()));

    // Sampler
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    HR(d.dev->CreateSamplerState(&sd, sampLinear.GetAddressOf()));

    // Cube mesh
    cube = makeCube(d.dev.Get(), 0.5f);

    // Blend (alpha)
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        HR(d.dev->CreateBlendState(&bd, blendAlpha.GetAddressOf()));
    }

    // Rasterizer (solid/wire)
    {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.DepthClipEnable = TRUE;
        HR(d.dev->CreateRasterizerState(&rd, rsSolid.GetAddressOf()));
        rd.FillMode = D3D11_FILL_WIREFRAME;
        HR(d.dev->CreateRasterizerState(&rd, rsWire.GetAddressOf()));
    }

    // GPU timers
    timerFrame.init(d.dev.Get());
    timerTerrain.init(d.dev.Get());
    timerCube.init(d.dev.Get());
    timerOrbital.init(d.dev.Get());

    // Orbital renderer
    if (!orender.Initialize(d.dev.Get(), L"res\\shaders")) {
        assert(false);
        ExitProcess(1);
    }
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
    std::vector<float> hm;
    makeHeightmap(hm, sim.HM, sim.HM, sim.seed, sim.hmScale, sim.hmOctaves, sim.hmPersistence);
    heightTex.create(d.dev.Get(), hm, sim.HM, sim.HM);
}

void SliceRendererD3D11::reloadOrbitalRenderer() {
    orender.Shutdown();
    if (!orender.Initialize(d.dev.Get(), L"res\\shaders")) {
        assert(false);
        ExitProcess(1);
    }
}

void SliceRendererD3D11::renderFrame(const SliceSimulation& sim) {
    timerFrame.begin(d.ctx.Get());

    // Raster state (wireframe toggle)
    d.ctx->RSSetState(sim.wireframe ? rsWire.Get() : rsSolid.Get());

    // Build view/proj
    XMMATRIX V = (sim.camMode == SliceSimulation::CamMode::Orbit) ? sim.orbitCam.view() : sim.freeCam.view();
    XMMATRIX P = XMMatrixPerspectiveFovLH(XMConvertToRadians(sim.fovDeg), d.width / float(d.height), 0.1f, 500.f);

    // --- Terrain ---
    timerTerrain.begin(d.ctx.Get());

    CameraCB cam{};
    XMStoreFloat4x4(&cam.World, XMMatrixIdentity());
    XMStoreFloat4x4(&cam.View, V);
    XMStoreFloat4x4(&cam.Proj, P);
    cam.HeightAmplitude = sim.HeightAmp;
    cam.HeightTexel = XMFLOAT2(1.f / sim.HM, 1.f / sim.HM);
    cam.TileWorld = sim.TileWorld;
    UpdateCB(d.ctx.Get(), cbCamera.Get(), cam);

    TerrainCB tcb{};
    tcb.LightDir = sim.lightDir;
    tcb.BaseColor = XMFLOAT3(0.32f, 0.58f, 0.32f);
    tcb.HeightScale = sim.HeightAmp / sim.TileWorld;
    tcb.HeightTexel = XMFLOAT2(1.f / sim.HM, 1.f / sim.HM);
    UpdateCB(d.ctx.Get(), cbTerrain.Get(), tcb);

    const UINT stride = static_cast<UINT>(sizeof(Vtx));
    const UINT offs = 0;
    d.ctx->IASetVertexBuffers(0, 1, grid.vbo.GetAddressOf(), &stride, &offs);
    d.ctx->IASetIndexBuffer(grid.ibo.Get(), DXGI_FORMAT_R32_UINT, 0);
    d.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d.ctx->IASetInputLayout(terrainIL.Get());
    d.ctx->VSSetShader(terrainVS.Get(), nullptr, 0);
    d.ctx->VSSetConstantBuffers(0, 1, cbCamera.GetAddressOf());

    ID3D11ShaderResourceView* srvs[1] = { heightTex.srv.Get() };
    d.ctx->VSSetShaderResources(0, 1, srvs);
    d.ctx->VSSetSamplers(0, 1, sampLinear.GetAddressOf());

    d.ctx->PSSetShader(terrainPS.Get(), nullptr, 0);
    d.ctx->PSSetConstantBuffers(1, 1, cbTerrain.GetAddressOf());
    d.ctx->PSSetShaderResources(0, 1, srvs);
    d.ctx->PSSetSamplers(0, 1, sampLinear.GetAddressOf());

    d.ctx->DrawIndexed(grid.indexCount, 0, 0);

    timerTerrain.end(d.ctx.Get());

    // --- Cube prop ---
    timerCube.begin(d.ctx.Get());

    if (sim.drawCube) {
        CameraCB camCube = cam;
        XMStoreFloat4x4(&camCube.World, XMMatrixTranslation(0, 0.5f, 0));
        UpdateCB(d.ctx.Get(), cbCameraCube.Get(), camCube);

        ColorCB ccb{};
        ccb.LightDir = sim.lightDir;
        ccb.Albedo = XMFLOAT3(0.7f, 0.2f, 0.2f);
        UpdateCB(d.ctx.Get(), cbColor.Get(), ccb);

        const UINT stride2 = static_cast<UINT>(sizeof(VtxN));
        const UINT offs2 = 0;
        d.ctx->IASetVertexBuffers(0, 1, cube.vbo.GetAddressOf(), &stride2, &offs2);
        d.ctx->IASetIndexBuffer(cube.ibo.Get(), DXGI_FORMAT_R16_UINT, 0);
        d.ctx->IASetInputLayout(colorIL.Get());
        d.ctx->VSSetShader(colorVS.Get(), nullptr, 0);
        d.ctx->VSSetConstantBuffers(0, 1, cbCameraCube.GetAddressOf());
        d.ctx->PSSetShader(colorPS.Get(), nullptr, 0);
        d.ctx->PSSetConstantBuffers(1, 1, cbColor.GetAddressOf());
        d.ctx->DrawIndexed(cube.indexCount, 0, 0);
    }

    timerCube.end(d.ctx.Get());

    // --- Orbital system ---
    timerOrbital.begin(d.ctx.Get());

    {
        // Lift system to avoid ground intersection; if FreeCam, keep as-is
        XMMATRIX V_orb = XMMatrixTranslation(0.0f, -6.0f, 0.0f) * V;

        float blendFactor[4] = { 0,0,0,0 };
        d.ctx->OMSetBlendState(sim.orbitBlend ? blendAlpha.Get() : nullptr, blendFactor, 0xFFFFFFFF);
        orender.Render(d.ctx.Get(), sim.orbital, V_orb, P, sim.orbOpts);
        d.ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    }

    timerOrbital.end(d.ctx.Get());

    timerFrame.end(d.ctx.Get());

    // Resolve queries from previous frames (non-blocking; may lag a few frames)
    timerFrame.resolve(d.ctx.Get());
    timerTerrain.resolve(d.ctx.Get());
    timerCube.resolve(d.ctx.Get());
    timerOrbital.resolve(d.ctx.Get());
}

bool SliceRendererD3D11::saveScreenshotBMP() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t name[256];
    swprintf_s(name, L"Screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    ComPtr<ID3D11Texture2D> back;
    HR(d.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()));

    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    HR(d.dev->CreateTexture2D(&sdesc, nullptr, staging.GetAddressOf()));

    d.ctx->CopyResource(staging.Get(), back.Get());

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(d.ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;

    std::ofstream f(name, std::ios::binary);
    if (!f) {
        d.ctx->Unmap(staging.Get(), 0);
        return false;
    }

    const UINT W = desc.Width;
    const UINT H = desc.Height;
    const UINT rowSize = W * 4;

#pragma pack(push,1)
    struct BMPFileHeader {
        uint16_t bfType{ 0x4D42 };
        uint32_t bfSize;
        uint16_t r1{ 0 };
        uint16_t r2{ 0 };
        uint32_t bfOffBits{ 54 };
    };
    struct BMPInfoHeader {
        uint32_t biSize{ 40 };
        int32_t biWidth;
        int32_t biHeight;
        uint16_t biPlanes{ 1 };
        uint16_t biBitCount{ 32 };
        uint32_t biCompression{ 0 };
        uint32_t biSizeImage;
        int32_t biXPelsPerMeter{ 2835 };
        int32_t biYPelsPerMeter{ 2835 };
        uint32_t biClrUsed{ 0 };
        uint32_t biClrImportant{ 0 };
    };
#pragma pack(pop)

    BMPFileHeader fh{};
    fh.bfSize = 54 + rowSize * H;

    BMPInfoHeader ih{};
    ih.biWidth = (int32_t)W;
    ih.biHeight = -(int32_t)H; // top-down
    ih.biSizeImage = rowSize * H;

    f.write(reinterpret_cast<char*>(&fh), sizeof(fh));
    f.write(reinterpret_cast<char*>(&ih), sizeof(ih));

    for (UINT y = 0; y < H; ++y) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>((const uint8_t*)ms.pData + y * ms.RowPitch);

        std::vector<uint8_t> row(rowSize);
        for (UINT x = 0; x < W; ++x) {
            row[x * 4 + 0] = src[x * 4 + 2]; // B
            row[x * 4 + 1] = src[x * 4 + 1]; // G
            row[x * 4 + 2] = src[x * 4 + 0]; // R
            row[x * 4 + 3] = src[x * 4 + 3]; // A
        }
        f.write((char*)row.data(), rowSize);
    }

    d.ctx->Unmap(staging.Get(), 0);
    return true;
}

} // namespace slice
