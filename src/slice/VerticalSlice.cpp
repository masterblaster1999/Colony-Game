// VerticalSlice.cpp - build as "ColonySlice"
// Compile: MSVC, C++17. Link: d3d11.lib dxgi.lib d3dcompiler.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const wchar_t* kTerrainVS = L"res/shaders/Slice_TerrainVS.hlsl";
static const wchar_t* kTerrainPS = L"res/shaders/Slice_TerrainPS.hlsl";
static const wchar_t* kColorVS   = L"res/shaders/Slice_ColorVS.hlsl";
static const wchar_t* kColorPS   = L"res/shaders/Slice_ColorPS.hlsl";

// Simple HRESULT check
#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

// --------------------------------------------------------------------------------------
// Minimal argument parsing: --seed <uint>
// --------------------------------------------------------------------------------------
static uint32_t gSeed = 1337;

static void ParseArgs(LPWSTR cmdLine)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) return;
    for (int i = 0; i + 1 < argc; ++i)
    {
        if (wcscmp(argv[i], L"--seed") == 0) {
            gSeed = (uint32_t)_wtoi(argv[i + 1]);
            ++i;
        }
    }
    LocalFree(argv);
}

// --------------------------------------------------------------------------------------
// Window plumbing
// --------------------------------------------------------------------------------------
struct Device {
    HWND hwnd{};
    UINT width{1280}, height{720};
    ComPtr<ID3D11Device>           dev;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<IDXGISwapChain>         swap;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Texture2D>        dsTex;

    void create(HWND w, UINT ww, UINT hh) {
        hwnd = w; width = ww; height = hh;

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width  = width;
        sd.BufferDesc.Height = height;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL fl;
        HR(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, swap.GetAddressOf(), dev.GetAddressOf(), &fl, ctx.GetAddressOf()));

        recreateRT();
    }

    void recreateRT() {
        // Create RTV from back buffer
        ComPtr<ID3D11Texture2D> bb;
        HR(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.GetAddressOf()));
        HR(dev->CreateRenderTargetView(bb.Get(), nullptr, rtv.GetAddressOf()));

        // Depth
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width; td.Height = height;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        HR(dev->CreateTexture2D(&td, nullptr, dsTex.GetAddressOf()));
        HR(dev->CreateDepthStencilView(dsTex.Get(), nullptr, dsv.GetAddressOf()));

        D3D11_VIEWPORT vp{};
        vp.Width  = (FLOAT)width; vp.Height = (FLOAT)height;
        vp.MinDepth = 0; vp.MaxDepth = 1;
        ctx->RSSetViewports(1, &vp);
    }

    void beginFrame(float rgba[4]) {
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
        ctx->ClearRenderTargetView(rtv.Get(), rgba);
        ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    void present() { swap->Present(1, 0); }
};

// --------------------------------------------------------------------------------------
// Shader helpers
// --------------------------------------------------------------------------------------
static ComPtr<ID3DBlob> Compile(const wchar_t* file, const char* entry, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG;
#endif
    ComPtr<ID3DBlob> blob, errs;
    HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, target, flags, 0, blob.GetAddressOf(), errs.GetAddressOf());
    if (FAILED(hr)) {
        if (errs) OutputDebugStringA((const char*)errs->GetBufferPointer());
        HR(hr);
    }
    return blob;
}

// --------------------------------------------------------------------------------------
// CPU value-noise heightmap (deterministic by seed)
// --------------------------------------------------------------------------------------
static inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x9E3779B1u + y * 0x85EBCA77u + seed * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}
static inline float rand01(uint32_t x, uint32_t y, uint32_t seed) {
    return (hash2(x, y, seed) & 0x00FFFFFFu) / float(0x01000000);
}

static float fade(float t) { return t*t*(3.f - 2.f*t); }

static void makeHeightmap(std::vector<float>& out, int W, int H, uint32_t seed, float scale, int octaves=4, float persistence=0.5f)
{
    out.resize(size_t(W)*H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float xf = x / scale, yf = y / scale;
            float amp = 1.f, sum = 0.f, norm = 0.f;
            int xi = int(std::floor(xf)), yi = int(std::floor(yf));
            float tx = xf - xi, ty = yf - yi;
            for (int o = 0; o < octaves; ++o) {
                // lattice coordinates per octave
                int step = 1 << o;
                float u = tx / step, v = ty / step;
                int X0 = (xi >> o), Y0 = (yi >> o);
                float v00 = rand01(X0,   Y0,   seed);
                float v10 = rand01(X0+1, Y0,   seed);
                float v01 = rand01(X0,   Y0+1, seed);
                float v11 = rand01(X0+1, Y0+1, seed);
                float sx = fade(u), sy = fade(v);
                float ix0 = v00 + (v10 - v00) * sx;
                float ix1 = v01 + (v11 - v01) * sx;
                float val = ix0 + (ix1 - ix0) * sy;
                sum  += val * amp;
                norm += amp;
                amp  *= persistence;
            }
            out[size_t(y)*W + x] = sum / norm; // 0..1
        }
    }
}

// Upload float heightmap to GPU as R32_FLOAT + SRV
struct HeightTexture {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    int W{}, H{};
    void create(ID3D11Device* dev, const std::vector<float>& h, int w, int hgt)
    {
        W = w; H = hgt;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = hgt;
        td.MipLevels = 1; td.ArraySize = 1;
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
};

// --------------------------------------------------------------------------------------
// Grid mesh + cube
// --------------------------------------------------------------------------------------
struct Vtx { XMFLOAT3 pos; XMFLOAT2 uv; };
struct VtxN { XMFLOAT3 pos; XMFLOAT3 nrm; };

struct Mesh {
    ComPtr<ID3D11Buffer> vbo, ibo;
    UINT indexCount{};
};

static Mesh makeGrid(ID3D11Device* dev, int N, float tileWorld)
{
    std::vector<Vtx> v; v.reserve(size_t(N)*N);
    std::vector<uint32_t> idx; idx.reserve(size_t(N-1)*(N-1)*6);

    float half = (N-1) * tileWorld * 0.5f;
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float wx = x*tileWorld - half;
            float wz = z*tileWorld - half;
            v.push_back({ XMFLOAT3(wx, 0, wz), XMFLOAT2(x / float(N-1), z / float(N-1)) });
        }
    }
    for (int z = 0; z < N-1; ++z) {
        for (int x = 0; x < N-1; ++x) {
            uint32_t i0 = z*N + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + N;
            uint32_t i3 = i2 + 1;
            idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
            idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
        }
    }

    Mesh m; m.indexCount = (UINT)idx.size();
    D3D11_BUFFER_DESC vb{}; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth = UINT(v.size()*sizeof(Vtx)); vb.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA sdv{ v.data(), 0, 0 };
    HR(dev->CreateBuffer(&vb, &sdv, m.vbo.GetAddressOf()));

    D3D11_BUFFER_DESC ib{}; ib.BindFlags = D3D11_BIND_INDEX_BUFFER; ib.ByteWidth = UINT(idx.size()*sizeof(uint32_t)); ib.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA sdi{ idx.data(), 0, 0 };
    HR(dev->CreateBuffer(&ib, &sdi, m.ibo.GetAddressOf()));
    return m;
}

static Mesh makeCube(ID3D11Device* dev, float s)
{
    // Simple cube centered at origin
    const float h = s * 0.5f;
    VtxN verts[] = {
        // +X
        {{ h,-h,-h},{1,0,0}}, {{ h,-h, h},{1,0,0}}, {{ h, h, h},{1,0,0}}, {{ h, h,-h},{1,0,0}},
        // -X
        {{-h,-h, h},{-1,0,0}},{{-h,-h,-h},{-1,0,0}},{{-h, h,-h},{-1,0,0}},{{-h, h, h},{-1,0,0}},
        // +Y
        {{-h, h,-h},{0,1,0}}, {{ h, h,-h},{0,1,0}}, {{ h, h, h},{0,1,0}}, {{-h, h, h},{0,1,0}},
        // -Y
        {{-h,-h, h},{0,-1,0}},{{ h,-h, h},{0,-1,0}},{{ h,-h,-h},{0,-1,0}},{{-h,-h,-h},{0,-1,0}},
        // +Z
        {{-h,-h, h},{0,0,1}}, {{-h, h, h},{0,0,1}}, {{ h, h, h},{0,0,1}}, {{ h,-h, h},{0,0,1}},
        // -Z
        {{ h,-h,-h},{0,0,-1}},{{ h, h,-h},{0,0,-1}},{{-h, h,-h},{0,0,-1}},{{-h,-h,-h},{0,0,-1}}
    };
    uint16_t idx[] = {
        0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,
        12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23
    };

    Mesh m; m.indexCount = _countof(idx);
    D3D11_BUFFER_DESC vb{}; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth = sizeof(verts); vb.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA sdv{ verts, 0, 0 };
    HR(dev->CreateBuffer(&vb, &sdv, m.vbo.GetAddressOf()));

    D3D11_BUFFER_DESC ib{}; ib.BindFlags = D3D11_BIND_INDEX_BUFFER; ib.ByteWidth = sizeof(idx); ib.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA sdi{ idx, 0, 0 };
    HR(dev->CreateBuffer(&ib, &sdi, m.ibo.GetAddressOf()));
    return m;
}

// --------------------------------------------------------------------------------------
// Pipeline objects
// --------------------------------------------------------------------------------------
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

struct Slice {
    Device* d{};
    // Terrain
    Mesh grid{};
    HeightTexture heightTex{};
    ComPtr<ID3D11VertexShader> terrainVS;
    ComPtr<ID3D11PixelShader>  terrainPS;
    ComPtr<ID3D11InputLayout>  terrainIL;
    ComPtr<ID3D11Buffer>       cbCamera;
    ComPtr<ID3D11Buffer>       cbTerrain;
    ComPtr<ID3D11SamplerState> sampLinear;

    // Cube
    Mesh cube{};
    ComPtr<ID3D11VertexShader> colorVS;
    ComPtr<ID3D11PixelShader>  colorPS;
    ComPtr<ID3D11InputLayout>  colorIL;
    ComPtr<ID3D11Buffer>       cbCameraCube;
    ComPtr<ID3D11Buffer>       cbColor;

    // Params
    int HM = 128;
    float TileWorld = 0.5f;     // world units per tile
    float HeightAmp = 6.0f;     // max height in world units
    XMFLOAT3 lightDir = XMFLOAT3(0.3f, 0.8f, 0.5f);

    void create(Device& dev) {
        d = &dev;

        // Heightmap
        std::vector<float> hm;
        makeHeightmap(hm, HM, HM, gSeed, 24.0f /*scale*/);
        heightTex.create(d->dev.Get(), hm, HM, HM);

        // Grid mesh
        grid = makeGrid(d->dev.Get(), HM, TileWorld);

        // Terrain pipeline
        auto vsb = Compile(kTerrainVS, "main", "vs_5_0");
        auto psb = Compile(kTerrainPS, "main", "ps_5_0");
        HR(d->dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, terrainVS.GetAddressOf()));
        HR(d->dev->CreatePixelShader (psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, terrainPS.GetAddressOf()));
        D3D11_INPUT_ELEMENT_DESC il[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0  ,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT   ,0,12 ,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        HR(d->dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), terrainIL.GetAddressOf()));

        // Cube pipeline
        auto vsb2 = Compile(kColorVS, "main", "vs_5_0");
        auto psb2 = Compile(kColorPS, "main", "ps_5_0");
        HR(d->dev->CreateVertexShader(vsb2->GetBufferPointer(), vsb2->GetBufferSize(), nullptr, colorVS.GetAddressOf()));
        HR(d->dev->CreatePixelShader (psb2->GetBufferPointer(), psb2->GetBufferSize(), nullptr, colorPS.GetAddressOf()));
        D3D11_INPUT_ELEMENT_DESC il2[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0  ,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL"  ,0,DXGI_FORMAT_R32G32B32_FLOAT,0,12 ,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        HR(d->dev->CreateInputLayout(il2, 2, vsb2->GetBufferPointer(), vsb2->GetBufferSize(), colorIL.GetAddressOf()));

        // CBuffers
        D3D11_BUFFER_DESC cbd{}; cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cbd.ByteWidth=sizeof(CameraCB); cbd.Usage=D3D11_USAGE_DYNAMIC; cbd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        HR(d->dev->CreateBuffer(&cbd, nullptr, cbCamera.GetAddressOf()));
        HR(d->dev->CreateBuffer(&cbd, nullptr, cbCameraCube.GetAddressOf()));
        D3D11_BUFFER_DESC cbd2=cbd; cbd2.ByteWidth=sizeof(TerrainCB);
        HR(d->dev->CreateBuffer(&cbd2, nullptr, cbTerrain.GetAddressOf()));
        D3D11_BUFFER_DESC cbd3=cbd; cbd3.ByteWidth=sizeof(ColorCB);
        HR(d->dev->CreateBuffer(&cbd3, nullptr, cbColor.GetAddressOf()));

        // Sampler
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        HR(d->dev->CreateSamplerState(&sd, sampLinear.GetAddressOf()));

        // Cube
        cube = makeCube(d->dev.Get(), 0.5f);
    }

    void updateAndDraw(float dt)
    {
        // Camera
        static float t = 0.f; t += dt * 0.25f;
        XMVECTOR eye = XMVectorSet(12.f * std::cos(t), 8.f, -12.f * std::sin(t), 0.f);
        XMVECTOR at  = XMVectorSet(0, 0, 0, 0);
        XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
        XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
        XMMATRIX P = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), d->width / float(d->height), 0.1f, 200.f); // 

        // Terrain constants
        CameraCB cam{};
        XMStoreFloat4x4(&cam.World, XMMatrixIdentity());
        XMStoreFloat4x4(&cam.View , V);
        XMStoreFloat4x4(&cam.Proj , P);
        cam.HeightAmplitude = HeightAmp;
        cam.HeightTexel = XMFLOAT2(1.f / HM, 1.f / HM);
        cam.TileWorld = TileWorld;

        D3D11_MAPPED_SUBRESOURCE ms{};
        HR(d->ctx->Map(cbCamera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
        memcpy(ms.pData, &cam, sizeof(cam));
        d->ctx->Unmap(cbCamera.Get(), 0);

        TerrainCB tcb{};
        tcb.LightDir = lightDir;
        tcb.BaseColor = XMFLOAT3(0.32f, 0.58f, 0.32f);
        tcb.HeightScale = HeightAmp / TileWorld;
        tcb.HeightTexel = XMFLOAT2(1.f / HM, 1.f / HM);
        HR(d->ctx->Map(cbTerrain.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
        memcpy(ms.pData, &tcb, sizeof(tcb));
        d->ctx->Unmap(cbTerrain.Get(), 0);

        // Draw terrain
        UINT stride = sizeof(Vtx), offs = 0;
        d->ctx->IASetVertexBuffers(0, 1, grid.vbo.GetAddressOf(), &stride, &offs);
        d->ctx->IASetIndexBuffer(grid.ibo.Get(), DXGI_FORMAT_R32_UINT, 0);
        d->ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d->ctx->IASetInputLayout(terrainIL.Get());
        d->ctx->VSSetShader(terrainVS.Get(), nullptr, 0);
        d->ctx->VSSetConstantBuffers(0, 1, cbCamera.GetAddressOf());
        // Bind the height texture to the VS (supported in D3D11) 
        ID3D11ShaderResourceView* srvs[1] = { heightTex.srv.Get() };
        d->ctx->VSSetShaderResources(0, 1, srvs);
        d->ctx->VSSetSamplers(0, 1, sampLinear.GetAddressOf());
        d->ctx->PSSetShader(terrainPS.Get(), nullptr, 0);
        d->ctx->PSSetConstantBuffers(1, 1, cbTerrain.GetAddressOf());
        d->ctx->PSSetShaderResources(0, 1, srvs);
        d->ctx->PSSetSamplers(0, 1, sampLinear.GetAddressOf());
        d->ctx->DrawIndexed(grid.indexCount, 0, 0);

        // Draw cube at origin (slightly above ground)
        CameraCB camCube = cam;
        XMStoreFloat4x4(&camCube.World, XMMatrixTranslation(0, 0.5f, 0));
        HR(d->ctx->Map(cbCameraCube.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
        memcpy(ms.pData, &camCube, sizeof(camCube));
        d->ctx->Unmap(cbCameraCube.Get(), 0);

        ColorCB ccb{};
        ccb.LightDir = lightDir;
        ccb.Albedo   = XMFLOAT3(0.7f, 0.2f, 0.2f);
        HR(d->ctx->Map(cbColor.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
        memcpy(ms.pData, &ccb, sizeof(ccb));
        d->ctx->Unmap(cbColor.Get(), 0);

        UINT stride2 = sizeof(VtxN), offs2 = 0;
        d->ctx->IASetVertexBuffers(0, 1, cube.vbo.GetAddressOf(), &stride2, &offs2);
        d->ctx->IASetIndexBuffer(cube.ibo.Get(), DXGI_FORMAT_R16_UINT, 0);
        d->ctx->IASetInputLayout(colorIL.Get());
        d->ctx->VSSetShader(colorVS.Get(), nullptr, 0);
        d->ctx->VSSetConstantBuffers(0, 1, cbCameraCube.GetAddressOf());
        d->ctx->PSSetShader(colorPS.Get(), nullptr, 0);
        d->ctx->PSSetConstantBuffers(1, 1, cbColor.GetAddressOf());
        d->ctx->DrawIndexed(cube.indexCount, 0, 0);
    }
};

// --------------------------------------------------------------------------------------
// App + WndProc
// --------------------------------------------------------------------------------------
static Device gDev;
static Slice  gSlice;
static bool   gRunning = true;

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_SIZE:
        if (gDev.dev) {
            gDev.width  = LOWORD(l);
            gDev.height = HIWORD(l);
            gDev.rtv.Reset(); gDev.dsv.Reset(); gDev.dsTex.Reset();
            HR(gDev.swap->ResizeBuffers(0, gDev.width, gDev.height, DXGI_FORMAT_UNKNOWN, 0));
            gDev.recreateRT();
        }
        return 0;
    case WM_DESTROY: gRunning = false; PostQuitMessage(0); return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) { DestroyWindow(h); } return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cl, int)
{
    ParseArgs(cl);

    WNDCLASSW wc{}; wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"SliceWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Colony Vertical Slice", WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, hi, nullptr);

    gDev.create(hwnd, 1280, 720);
    gSlice.create(gDev);

    auto t0 = std::chrono::high_resolution_clock::now();
    MSG msg{};
    while (gRunning) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        auto t1 = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;

        float clear[4] = { 0.08f, 0.11f, 0.14f, 1.f };
        gDev.beginFrame(clear);
        gSlice.updateAndDraw(dt);
        gDev.present();
    }
    return 0;
}
