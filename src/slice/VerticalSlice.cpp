// VerticalSlice.cpp - build as "ColonySlice"
// Compile: MSVC, C++17. Link: d3d11.lib dxgi.lib d3dcompiler.lib
//
// Upgrades included:
// - sRGB backbuffer for gamma-correct presentation
// - GPU timers (terrain/cube/orbital/frame) using timestamp queries
// - Alt+Enter fullscreen toggle; VSync toggle
// - Orbit & Free-fly cameras; follow selected body; body selection cycling
// - Pause, single-step, time-scale; heightmap regenerate; FOV control
// - D3D11 debug InfoQueue (debug builds)
// - On-title stats incl. FPS, GPU ms per section, bodies, VSync, time-scale, seed
// - Objective tracker glue (debug hotkeys + title progress):
//      Y=build, U=craft, J=spawn colonist, K=colonist death
//
// NOTE: D3D11 has fixed line width == 1.0. Thick/AA lines require custom geometry
// (quads/GS) if desired.
//
// Requires the orbital system files provided earlier:
//   "space/OrbitalSystem.h", "space/OrbitalSystem.cpp"
//   "render/OrbitalRenderer.h", "render/OrbitalRenderer.cpp"
// and HLSL files:
//   Slice_TerrainVS.hlsl, Slice_TerrainPS.hlsl, Slice_ColorVS.hlsl, Slice_ColorPS.hlsl
//   OrbitalSphereVS.hlsl, OrbitalSpherePS.hlsl, OrbitLineVS.hlsl, OrbitLinePS.hlsl
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
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
#include <fstream>
#include <sstream>
#if defined(_DEBUG)
  #include <d3d11sdklayers.h>
#endif

#include "space/OrbitalSystem.h"
#include "render/OrbitalRenderer.h"

// >>> Added: vertical-slice objective system glue
#include "slice/ObjectiveTracker.h"
// <<<

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const wchar_t* kTerrainVS = L"res/shaders/Slice_TerrainVS.hlsl";
static const wchar_t* kTerrainPS = L"res/shaders/Slice_TerrainPS.hlsl";
static const wchar_t* kColorVS   = L"res/shaders/Slice_ColorVS.hlsl";
static const wchar_t* kColorPS   = L"res/shaders/Slice_ColorPS.hlsl";

#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

template<class T>
static void UpdateCB(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const T& data) {
    D3D11_MAPPED_SUBRESOURCE ms{};
    HR(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
    memcpy(ms.pData, &data, sizeof(T));
    ctx->Unmap(cb, 0);
}
static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n ? n-1 : 0), L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// --------------------------------------------------------------------------------------
// CLI args:  --seed <uint>
// --------------------------------------------------------------------------------------
static uint32_t gSeed = 1337;
static void ParseArgs(LPWSTR cmdLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) return;
    for (int i = 0; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--seed") == 0) {
            gSeed = (uint32_t)_wtoi(argv[i + 1]);
            ++i;
        }
    }
    LocalFree(argv);
}

// --------------------------------------------------------------------------------------
// Device / Swapchain
// --------------------------------------------------------------------------------------
struct Device {
    HWND hwnd{};
    UINT width{1280}, height{720};
    bool fullscreen{false};

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
        D3D_FEATURE_LEVEL fl;
        HR(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, swap.GetAddressOf(), dev.GetAddressOf(), &fl, ctx.GetAddressOf()));

    #if defined(_DEBUG)
        // Debug InfoQueue: break on ERROR/CORRUPTION if available
        ComPtr<ID3D11InfoQueue> q;
        if (SUCCEEDED(dev.As(&q))) {
            q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
            // Example: filter noisy messages (optional)
            D3D11_MESSAGE_ID hide[] = { D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS };
            D3D11_INFO_QUEUE_FILTER f{}; f.DenyList.NumIDs = _countof(hide); f.DenyList.pIDList = hide;
            q->AddStorageFilterEntries(&f);
        }
    #endif
        recreateRT();
    }

    void recreateRT() {
        rtv.Reset(); dsv.Reset(); dsTex.Reset();

        ComPtr<ID3D11Texture2D> bb;
        HR(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.GetAddressOf()));
        HR(dev->CreateRenderTargetView(bb.Get(), nullptr, rtv.GetAddressOf()));

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
        ctx->ClearRenderTargetView(rtv.Get(), rgba); // values are linear; hardware converts for sRGB RTV
        ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    void present(bool vsync) { swap->Present(vsync ? 1 : 0, 0); }

    void toggleFullscreen() {
        fullscreen = !fullscreen;
        HR(swap->SetFullscreenState(fullscreen, nullptr));
        // In real apps, consider ResizeTarget for specific modes before switching.
    }
};

// --------------------------------------------------------------------------------------
// HLSL compile helper
// --------------------------------------------------------------------------------------
static ComPtr<ID3DBlob> Compile(const wchar_t* file, const char* entry, const char* target) {
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
// CPU value-noise heightmap
// --------------------------------------------------------------------------------------
static inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x9E3779B1u + y * 0x85EBCA77u + seed * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}
static inline float rand01(uint32_t x, uint32_t y, uint32_t seed) {
    return (float)((hash2(x, y, seed) & 0x00FFFFFFu) / double(0x01000000));
}
static float fade(float t) { return t*t*(3.f - 2.f*t); }

static void makeHeightmap(std::vector<float>& out, int W, int H, uint32_t seed, float scale, int octaves=4, float persistence=0.5f)
{
    out.resize(size_t(W)*H);
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

// Height texture (R32_FLOAT)
struct HeightTexture {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    int W{}, H{};
    void create(ID3D11Device* dev, const std::vector<float>& h, int w, int hgt) {
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
// Mesh helpers
// --------------------------------------------------------------------------------------
struct Vtx { XMFLOAT3 pos; XMFLOAT2 uv; };
struct VtxN { XMFLOAT3 pos; XMFLOAT3 nrm; };
struct Mesh { ComPtr<ID3D11Buffer> vbo, ibo; UINT indexCount{}; };

static Mesh makeGrid(ID3D11Device* dev, int N, float tileWorld) {
    std::vector<Vtx> v; v.reserve(size_t(N)*N);
    std::vector<uint32_t> idx; idx.reserve(size_t(N-1)*(N-1)*6);

    float half = (N-1) * tileWorld * 0.5f;
    for (int z = 0; z < N; ++z)
    for (int x = 0; x < N; ++x) {
        float wx = x*tileWorld - half;
        float wz = z*tileWorld - half;
        v.push_back({ XMFLOAT3(wx, 0, wz), XMFLOAT2(x / float(N-1), z / float(N-1)) });
    }
    for (int z = 0; z < N-1; ++z)
    for (int x = 0; x < N-1; ++x) {
        uint32_t i0 = z*N + x;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = i0 + N;
        uint32_t i3 = i2 + 1;
        idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
        idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
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
static Mesh makeCube(ID3D11Device* dev, float s) {
    const float h = s * 0.5f;
    VtxN verts[] = {
        {{ h,-h,-h},{1,0,0}}, {{ h,-h, h},{1,0,0}}, {{ h, h, h},{1,0,0}}, {{ h, h,-h},{1,0,0}},
        {{-h,-h, h},{-1,0,0}},{{-h,-h,-h},{-1,0,0}},{{-h, h,-h},{-1,0,0}},{{-h, h, h},{-1,0,0}},
        {{-h, h,-h},{0,1,0}}, {{ h, h,-h},{0,1,0}}, {{ h, h, h},{0,1,0}}, {{-h, h, h},{0,1,0}},
        {{-h,-h, h},{0,-1,0}},{{ h,-h, h},{0,-1,0}},{{ h,-h,-h},{0,-1,0}},{{-h,-h,-h},{0,-1,0}},
        {{-h,-h, h},{0,0,1}}, {{-h, h, h},{0,0,1}}, {{ h, h, h},{0,0,1}}, {{ h,-h, h},{0,0,1}},
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
// Pipeline constant buffers
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

// --------------------------------------------------------------------------------------
// FPS & GPU profiler
// --------------------------------------------------------------------------------------
struct FPSCounter {
    double acc = 0.0;
    int frames = 0;
    double fps = 0.0, ms = 0.0;
    void tick(double dt) {
        acc += dt; frames++;
        if (acc >= 0.5) { fps = frames / acc; ms = 1000.0 * acc / frames; acc = 0.0; frames = 0; }
    }
};

struct GPUTimer {
    struct Set { ComPtr<ID3D11Query> disjoint, start, end; };
    std::vector<Set> sets;
    UINT cur = 0;
    double lastMs = 0.0;

    void init(ID3D11Device* dev, int bufferedFrames = 4) {
        sets.resize(bufferedFrames);
        for (auto& s : sets) {
            D3D11_QUERY_DESC qd{};
            qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; HR(dev->CreateQuery(&qd, s.disjoint.GetAddressOf()));
            qd.Query = D3D11_QUERY_TIMESTAMP;         HR(dev->CreateQuery(&qd, s.start.GetAddressOf()));
            qd.Query = D3D11_QUERY_TIMESTAMP;         HR(dev->CreateQuery(&qd, s.end.GetAddressOf()));
        }
    }
    void begin(ID3D11DeviceContext* ctx) {
        auto& s = sets[cur];
        ctx->Begin(s.disjoint.Get());
        ctx->End(s.start.Get());
    }
    void end(ID3D11DeviceContext* ctx) {
        auto& s = sets[cur];
        ctx->End(s.end.Get());
        ctx->End(s.disjoint.Get());
        cur = (cur + 1) % sets.size();
    }
    bool resolve(ID3D11DeviceContext* ctx) {
        UINT prev = (cur + sets.size() - 1) % sets.size();
        auto& s = sets[prev];
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx->GetData(s.disjoint.Get(), &dj, sizeof(dj), 0) != S_OK) return false;
        if (dj.Disjoint) return false;
        UINT64 t0=0, t1=0;
        if (ctx->GetData(s.start.Get(), &t0, sizeof(t0), 0) != S_OK) return false;
        if (ctx->GetData(s.end.Get(),   &t1, sizeof(t1), 0) != S_OK) return false;
        lastMs = double(t1 - t0) / double(dj.Frequency) * 1000.0;
        return true;
    }
};

// --------------------------------------------------------------------------------------
// Cameras
// --------------------------------------------------------------------------------------
static float ToRad(float deg) { return deg * (float)XM_PI / 180.0f; }
struct OrbitCam {
    XMFLOAT3 target{0,0,0};
    float radius = 18.f;
    float yawDeg = 35.f, pitchDeg = 25.f;
    XMMATRIX view() const {
        float cy = ToRad(yawDeg), cp = ToRad(pitchDeg);
        XMVECTOR eyeOff = XMVectorSet(radius * std::cos(cy)*std::cos(cp),
                                      radius * std::sin(cp),
                                      radius * std::sin(cy)*std::cos(cp), 0);
        XMVECTOR tgt = XMLoadFloat3(&target);
        return XMMatrixLookAtLH(XMVectorAdd(tgt, eyeOff), tgt, XMVectorSet(0,1,0,0));
    }
};
struct FreeCam {
    XMVECTOR pos = XMVectorSet(0, 3, -8, 0);
    float yaw = 0.0f, pitch = 0.0f; // radians
    float moveSpeed = 8.0f;
    float mouseSens = 0.0025f;
    void processMouse(float dx, float dy) {
        yaw   += dx * mouseSens;
        pitch += dy * mouseSens;
        const float k = ToRad(89.0f);
        if (pitch >  k) pitch =  k;
        if (pitch < -k) pitch = -k;
    }
    void processKeys(float dt) {
        float speed = moveSpeed * dt * (KeyDown(VK_SHIFT) ? 3.f : 1.f);
        XMVECTOR fwd = XMVectorSet(std::cos(yaw)*std::cos(pitch),
                                   std::sin(pitch),
                                   std::sin(yaw)*std::cos(pitch), 0);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(fwd, XMVectorSet(0,1,0,0)));
        XMVECTOR up    = XMVector3Normalize(XMVector3Cross(right, fwd));

        if (KeyDown('W')) pos = XMVectorAdd(pos, XMVectorScale(fwd,  speed));
        if (KeyDown('S')) pos = XMVectorAdd(pos, XMVectorScale(fwd, -speed));
        if (KeyDown('A')) pos = XMVectorAdd(pos, XMVectorScale(right, -speed));
        if (KeyDown('D')) pos = XMVectorAdd(pos, XMVectorScale(right,  speed));
        if (KeyDown('Q')) pos = XMVectorAdd(pos, XMVectorScale(up,   -speed));
        if (KeyDown('E')) pos = XMVectorAdd(pos, XMVectorScale(up,    speed));
    }
    XMMATRIX view() const {
        XMVECTOR fwd = XMVectorSet(std::cos(yaw)*std::cos(pitch),
                                   std::sin(pitch),
                                   std::sin(yaw)*std::cos(pitch), 0);
        return XMMatrixLookToLH(pos, fwd, XMVectorSet(0,1,0,0));
    }
};

// --------------------------------------------------------------------------------------
// Slice (content)
// --------------------------------------------------------------------------------------
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

    // States
    ComPtr<ID3D11BlendState>      blendAlpha;
    ComPtr<ID3D11RasterizerState> rsSolid, rsWire;

    // Orbital
    colony::space::OrbitalSystem          orbital;
    colony::space::OrbitalRenderer        orender;
    colony::space::OrbitalRendererOptions orbOpts{};

    // Cameras
    enum class CamMode { Orbit=0, Free=1 };
    CamMode camMode = CamMode::Orbit;
    OrbitCam orbitCam{};
    FreeCam  freeCam{};
    bool rightMouseWasDown = false;
    POINT lastMouse{};
    float fovDeg = 60.f;

    // Selection/follow
    int   selectedBody = 0;   // index into orbital.Bodies()
    bool  followSelected = false;

    // Controls / sim
    bool vsync = true;
    bool paused = false;
    bool singleStep = false;
    bool drawCube = true;
    bool orbitBlend = true;
    double timeDays  = 0.0;
    double timeScale = 5.0; // game days per real second
    float  TileWorld = 0.5f;
    float  HeightAmp = 6.0f;

    // Heightmap params
    int    HM = 128;
    float  hmScale = 24.0f;
    int    hmOctaves = 4;
    float  hmPersistence = 0.5f;

    // Lighting
    XMFLOAT3 lightDir = XMFLOAT3(0.3f, 0.8f, 0.5f);

    // Perf
    FPSCounter fps;
    GPUTimer timerFrame, timerTerrain, timerCube, timerOrbital;

    // Key edge detection
    SHORT prevKey[256]{};

    bool keyPressed(int vk) {
        SHORT cur = GetAsyncKeyState(vk);
        bool wasDown = (prevKey[vk] & 0x8000) != 0;
        bool isDown  = (cur & 0x8000) != 0;
        prevKey[vk] = cur;
        return isDown && !wasDown;
    }

    void create(Device& dev) {
        d = &dev;

        // Heightmap
        std::vector<float> hm;
        makeHeightmap(hm, HM, HM, gSeed, hmScale, hmOctaves, hmPersistence);
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

        // Cube mesh
        cube = makeCube(d->dev.Get(), 0.5f);

        // Blend (alpha)
        {
            D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            HR(d->dev->CreateBlendState(&bd, blendAlpha.GetAddressOf()));
        }
        // Rasterizer (solid/wire)
        {
            D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_BACK; rd.DepthClipEnable=TRUE;
            HR(d->dev->CreateRasterizerState(&rd, rsSolid.GetAddressOf()));
            rd.FillMode=D3D11_FILL_WIREFRAME;
            HR(d->dev->CreateRasterizerState(&rd, rsWire.GetAddressOf()));
        }

        // GPU timers
        timerFrame.init(d->dev.Get());
        timerTerrain.init(d->dev.Get());
        timerCube.init(d->dev.Get());
        timerOrbital.init(d->dev.Get());

        // Orbital system
        {
            colony::space::SystemConfig cfg{};
            cfg.seed = gSeed;
            cfg.minPlanets = 5;
            cfg.maxPlanets = 8;
            cfg.generateMoons = true;
            orbital = colony::space::OrbitalSystem::Generate(cfg);

            auto vs = orbital.Scale();
            vs.auToUnits   = 6.0; // compact system for this slice
            vs.kmToUnits   = vs.auToUnits / colony::space::AU_KM;
            vs.radiusScale = 7000.0;
            orbital.SetScale(vs);

            HR(orender.Initialize(d->dev.Get(), L"res\\shaders"));
            orbOpts.drawStar    = true;
            orbOpts.drawPlanets = true;
            orbOpts.drawMoons   = true;
            orbOpts.drawOrbits  = true;
            orbOpts.sphereSubdiv = 2;
        }

        GetCursorPos(&lastMouse);
    }

    void regenerateHeight() {
        std::vector<float> hm;
        makeHeightmap(hm, HM, HM, gSeed, hmScale, hmOctaves, hmPersistence);
        heightTex.create(d->dev.Get(), hm, HM, HM);
    }
    void regenerateOrbital(uint32_t newSeed) {
        colony::space::SystemConfig cfg{};
        cfg.seed = newSeed;
        cfg.minPlanets = 4 + (newSeed % 6); // 4..9
        cfg.maxPlanets = std::max(cfg.minPlanets, 9);
        cfg.generateMoons = true;
        orbital = colony::space::OrbitalSystem::Generate(cfg);

        auto vs = orbital.Scale();
        vs.auToUnits   = 6.0;
        vs.kmToUnits   = vs.auToUnits / colony::space::AU_KM;
        vs.radiusScale = 7000.0;
        orbital.SetScale(vs);

        selectedBody = (int)std::min<size_t>(selectedBody, orbital.Bodies().size() ? orbital.Bodies().size()-1 : 0);
    }

    // Screenshot (BMP, 32bpp BGRA, top-down)
    bool saveScreenshotBMP() {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t name[256];
        swprintf_s(name, L"Screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        ComPtr<ID3D11Texture2D> back;
        HR(d->swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()));
        D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC sdesc = desc;
        sdesc.Usage = D3D11_USAGE_STAGING;
        sdesc.BindFlags = 0;
        sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sdesc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        HR(d->dev->CreateTexture2D(&sdesc, nullptr, staging.GetAddressOf()));
        d->ctx->CopyResource(staging.Get(), back.Get());

        D3D11_MAPPED_SUBRESOURCE ms{};
        if (FAILED(d->ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;

        std::ofstream f(name, std::ios::binary);
        if (!f) { d->ctx->Unmap(staging.Get(), 0); return false; }

        const UINT W = desc.Width, H = desc.Height;
        const UINT rowSize = W * 4;

#pragma pack(push,1)
        struct BMPFileHeader { uint16_t bfType{0x4D42}; uint32_t bfSize; uint16_t r1{0}; uint16_t r2{0}; uint32_t bfOffBits{54}; };
        struct BMPInfoHeader { uint32_t biSize{40}; int32_t biWidth; int32_t biHeight; uint16_t biPlanes{1}; uint16_t biBitCount{32};
                               uint32_t biCompression{0}; uint32_t biSizeImage; int32_t biXPelsPerMeter{2835}; int32_t biYPelsPerMeter{2835};
                               uint32_t biClrUsed{0}; uint32_t biClrImportant{0}; };
#pragma pack(pop)
        BMPFileHeader fh{}; fh.bfSize = 54 + rowSize * H;
        BMPInfoHeader ih{}; ih.biWidth = (int32_t)W; ih.biHeight = -(int32_t)H; ih.biSizeImage = rowSize * H;
        f.write(reinterpret_cast<char*>(&fh), sizeof(fh));
        f.write(reinterpret_cast<char*>(&ih), sizeof(ih));

        for (UINT y = 0; y < H; ++y) {
            const uint8_t* src = reinterpret_cast<const uint8_t*>((const uint8_t*)ms.pData + y * ms.RowPitch);
            std::vector<uint8_t> row(rowSize);
            for (UINT x = 0; x < W; ++x) {
                row[x*4 + 0] = src[x*4 + 2]; // B
                row[x*4 + 1] = src[x*4 + 1]; // G
                row[x*4 + 2] = src[x*4 + 0]; // R
                row[x*4 + 3] = src[x*4 + 3]; // A
            }
            f.write((char*)row.data(), rowSize);
        }
        d->ctx->Unmap(staging.Get(), 0);
        return true;
    }

    XMFLOAT3 bodyWorldUnits(int idx) const {
        const auto& b = orbital.Bodies()[size_t(idx)];
        const auto& s = orbital.Scale();
        return XMFLOAT3(
            float(b.worldPosKm.x * s.kmToUnits),
            float(b.worldPosKm.y * s.kmToUnits),
            float(b.worldPosKm.z * s.kmToUnits));
    }

    void selectNextBody(int dir) { // dir = +1 or -1
        if (orbital.Bodies().empty()) return;
        selectedBody = (selectedBody + dir + (int)orbital.Bodies().size()) % (int)orbital.Bodies().size();
        if (followSelected) {
            orbitCam.target = bodyWorldUnits(selectedBody);
        }
    }

    void handleInputToggles() {
        if (keyPressed(VK_F1)) { // wireframe
            static bool wf = false; wf = !wf;
            d->ctx->RSSetState(wf ? rsWire.Get() : rsSolid.Get());
        }
        if (keyPressed('V')) { vsync = !vsync; }
        if (keyPressed(VK_SPACE)) { paused = !paused; }
        if (keyPressed('G')) { singleStep = true; } // step one fixed frame when paused
        if (keyPressed(VK_OEM_PLUS) || keyPressed(VK_ADD)) { timeScale *= 1.25; }
        if (keyPressed(VK_OEM_MINUS) || keyPressed(VK_SUBTRACT)) { timeScale = std::max(0.01, timeScale / 1.25); }
        if (keyPressed('R')) { regenerateOrbital(++gSeed); }
        if (keyPressed('N')) { regenerateHeight(); } // rebuild heightmap with current params
        if (keyPressed('O')) { orbOpts.drawOrbits  = !orbOpts.drawOrbits; }
        if (keyPressed('P')) { orbOpts.drawPlanets = !orbOpts.drawPlanets; }
        if (keyPressed('M')) { orbOpts.drawMoons   = !orbOpts.drawMoons; }
        if (keyPressed('T')) { orbOpts.drawStar    = !orbOpts.drawStar; }
        if (keyPressed('B')) { orbitBlend = !orbitBlend; }
        if (keyPressed('H')) { drawCube = !drawCube; }
        if (keyPressed('1')) { camMode = CamMode::Orbit; }
        if (keyPressed('2')) { camMode = CamMode::Free; }
        if (keyPressed('F')) { orender.Shutdown(); HR(orender.Initialize(d->dev.Get(), L"res\\shaders")); }
        if (keyPressed(VK_OEM_4)) { // '[' lower height amplitude
            HeightAmp = std::max(0.1f, HeightAmp - 0.5f);
        }
        if (keyPressed(VK_OEM_6)) { // ']' raise height amplitude
            HeightAmp += 0.5f;
        }
        if (keyPressed(VK_F12)) { saveScreenshotBMP(); }
        if (keyPressed(VK_OEM_COMMA)) { selectNextBody(-1); }    // ',' prev
        if (keyPressed(VK_OEM_PERIOD)) { selectNextBody(+1); }   // '.' next
        if (keyPressed('L')) { followSelected = !followSelected; if (followSelected) orbitCam.target = bodyWorldUnits(selectedBody); }
        if (keyPressed('C')) { followSelected = false; orbitCam.target = XMFLOAT3(0,0,0); } // reset target
        if (keyPressed('3')) { fovDeg = std::max(20.f, fovDeg - 2.f); }
        if (keyPressed('4')) { fovDeg = std::min(120.f, fovDeg + 2.f); }

        // --- Added: objective tracker debug events (simulate slice loop) ---
        if (keyPressed('Y')) { /* Build structure */ g_slice.notifyStructureBuilt(); }
        if (keyPressed('U')) { /* Craft item     */ g_slice.notifyItemCrafted(); }
        if (keyPressed('J')) { /* Spawn colonist */ g_slice.notifyColonistSpawned(); }
        if (keyPressed('K')) { /* Colonist death */ g_slice.notifyColonistDied(); } // also logs "colonist.death" event internally
        // -------------------------------------------------------------------
    }

    void updateCameraMouse() {
        bool rmb = KeyDown(VK_RBUTTON);
        POINT p; GetCursorPos(&p);
        if (rmb && rightMouseWasDown) {
            float dx = float(p.x - lastMouse.x);
            float dy = float(p.y - lastMouse.y);
            if (camMode == CamMode::Orbit) {
                orbitCam.yawDeg   += dx * 0.25f;
                orbitCam.pitchDeg -= dy * 0.25f;
                if (orbitCam.pitchDeg < -89) orbitCam.pitchDeg = -89;
                if (orbitCam.pitchDeg >  89) orbitCam.pitchDeg =  89;
            } else {
                freeCam.processMouse(dx, -dy);
            }
        }
        rightMouseWasDown = rmb;
        lastMouse = p;

        // Orbit cam radius with PgUp/PgDn
        if (camMode == CamMode::Orbit) {
            if (KeyDown(VK_NEXT)) orbitCam.radius = std::min(100.f, orbitCam.radius + 0.5f);
            if (KeyDown(VK_PRIOR)) orbitCam.radius = std::max(2.f, orbitCam.radius - 0.5f);
        }
    }

    void updateSim(double dt) {
        handleInputToggles();
        updateCameraMouse();
        if (camMode == CamMode::Free) {
            freeCam.processKeys((float)dt);
        }
        // Light anim (slow rotate)
        static float tLight = 0.f; tLight += (float)dt * 0.2f;
        lightDir = XMFLOAT3(std::cos(tLight)*0.3f, 0.8f, std::sin(tLight)*0.5f);

        // --- Added: feed objective tracker time ---
        // Pause/resume logic mirrors app 'paused' + 'singleStep' semantics.
        if (!paused) {
            g_slice.update(dt);
        } else if (singleStep) {
            g_slice.update(dt); // single-step advances once while paused
        }
        // ------------------------------------------

        if (!paused || singleStep) {
            timeDays += dt * timeScale;
            singleStep = false;
        }
        orbital.Update(timeDays);

        // Follow selected body
        if (followSelected && selectedBody >= 0 && selectedBody < (int)orbital.Bodies().size()) {
            orbitCam.target = bodyWorldUnits(selectedBody);
        }
    }

    void renderFrame() {
        timerFrame.begin(d->ctx.Get());

        // Build view/proj
        XMMATRIX V = (camMode == CamMode::Orbit) ? orbitCam.view() : freeCam.view();
        XMMATRIX P = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovDeg), d->width / float(d->height), 0.1f, 500.f);

        // --- Terrain ---
        timerTerrain.begin(d->ctx.Get());
        CameraCB cam{};
        XMStoreFloat4x4(&cam.World, XMMatrixIdentity());
        XMStoreFloat4x4(&cam.View , V);
        XMStoreFloat4x4(&cam.Proj , P);
        cam.HeightAmplitude = HeightAmp;
        cam.HeightTexel = XMFLOAT2(1.f / HM, 1.f / HM);
        cam.TileWorld = TileWorld;
        UpdateCB(d->ctx.Get(), cbCamera.Get(), cam);

        TerrainCB tcb{};
        tcb.LightDir = lightDir;
        tcb.BaseColor = XMFLOAT3(0.32f, 0.58f, 0.32f);
        tcb.HeightScale = HeightAmp / TileWorld;
        tcb.HeightTexel = XMFLOAT2(1.f / HM, 1.f / HM);
        UpdateCB(d->ctx.Get(), cbTerrain.Get(), tcb);

        UINT stride = sizeof(Vtx), offs = 0;
        d->ctx->IASetVertexBuffers(0, 1, grid.vbo.GetAddressOf(), &stride, &offs);
        d->ctx->IASetIndexBuffer(grid.ibo.Get(), DXGI_FORMAT_R32_UINT, 0);
        d->ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d->ctx->IASetInputLayout(terrainIL.Get());
        d->ctx->VSSetShader(terrainVS.Get(), nullptr, 0);
        d->ctx->VSSetConstantBuffers(0, 1, cbCamera.GetAddressOf());
        ID3D11ShaderResourceView* srvs[1] = { heightTex.srv.Get() };
        d->ctx->VSSetShaderResources(0, 1, srvs);
        d->ctx->VSSetSamplers(0, 1, sampLinear.GetAddressOf());
        d->ctx->PSSetShader(terrainPS.Get(), nullptr, 0);
        d->ctx->PSSetConstantBuffers(1, 1, cbTerrain.GetAddressOf());
        d->ctx->PSSetShaderResources(0, 1, srvs);
        d->ctx->PSSetSamplers(0, 1, sampLinear.GetAddressOf());
        d->ctx->DrawIndexed(grid.indexCount, 0, 0);
        timerTerrain.end(d->ctx.Get());

        // --- Cube prop ---
        timerCube.begin(d->ctx.Get());
        if (drawCube) {
            CameraCB camCube = cam;
            XMStoreFloat4x4(&camCube.World, XMMatrixTranslation(0, 0.5f, 0));
            UpdateCB(d->ctx.Get(), cbCameraCube.Get(), camCube);

            ColorCB ccb{}; ccb.LightDir = lightDir; ccb.Albedo = XMFLOAT3(0.7f, 0.2f, 0.2f);
            UpdateCB(d->ctx.Get(), cbColor.Get(), ccb);

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
        timerCube.end(d->ctx.Get());

        // --- Orbital system ---
        timerOrbital.begin(d->ctx.Get());
        {
            // Lift system to avoid ground intersection; if FreeCam, keep as-is
            XMMATRIX V_orb = XMMatrixTranslation(0.0f, -6.0f, 0.0f) * V;

            float blendFactor[4] = {0,0,0,0};
            d->ctx->OMSetBlendState(orbitBlend ? blendAlpha.Get() : nullptr, blendFactor, 0xFFFFFFFF);
            orender.Render(d->ctx.Get(), orbital, V_orb, P, orbOpts);
            d->ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
        }
        timerOrbital.end(d->ctx.Get());

        timerFrame.end(d->ctx.Get());

        // Resolve queries from previous frames (non-blocking; may lag a few frames)
        timerFrame.resolve(d->ctx.Get());
        timerTerrain.resolve(d->ctx.Get());
        timerCube.resolve(d->ctx.Get());
        timerOrbital.resolve(d->ctx.Get());
    }
};

// --------------------------------------------------------------------------------------
// Added: global objective tracker instance + basic localization
// --------------------------------------------------------------------------------------
static slice::ObjectiveTracker g_slice =
    slice::ObjectiveTracker::MakeDefault(/*surviveSeconds*/ 600.0,
                                         /*structuresToBuild*/ 2,
                                         /*itemsToCraft*/ 1,
                                         /*startingColonists*/ 3);

// --------------------------------------------------------------------------------------
// App boilerplate
// --------------------------------------------------------------------------------------
static Device gDev;
static Slice  gSlice;
static bool   gRunning = true;

static std::wstring MMSS(double s) {
    if (s < 0.0) s = 0.0;
    int is = int(s + 0.5);
    int m = is / 60; int sec = is % 60;
    wchar_t buf[16]; swprintf_s(buf, L"%02d:%02d", m, sec);
    return buf;
}

static void UpdateWindowTitle(HWND hwnd, const Slice& s) {
    std::wstring selName = L"";
    if (!s.orbital.Bodies().empty() && s.selectedBody >= 0 && s.selectedBody < (int)s.orbital.Bodies().size()) {
        selName = Widen(s.orbital.Bodies()[size_t(s.selectedBody)].name);
    }

    // Slice HUD first line (localized via g_slice localizer)
    auto hud = g_slice.hudLines();
    std::wstring objLine = hud.empty() ? L"Objective: (none)" : Widen(hud[0]);
    int pct = int(g_slice.overallProgress() * 100.0 + 0.5);
    const auto& st = g_slice.state();

    wchar_t title[512];
    swprintf_s(title,
        L"Colony Vertical Slice | FPS: %.0f (%.2f ms) | GPU: F%.2fms T%.2fms C%.2fms O%.2fms | Bodies:%zu Sel:%s | VSync:%s | TimeScale:%.2f | Seed:%u | %s | %d%% | Built:%d Crafted:%d Colonists:%d | Surv:%s",
        s.fps.fps, s.fps.ms,
        s.timerFrame.lastMs, s.timerTerrain.lastMs, s.timerCube.lastMs, s.timerOrbital.lastMs,
        s.orbital.Bodies().size(), selName.c_str(),
        s.vsync ? L"On" : L"Off", s.timeScale, gSeed,
        objLine.c_str(), pct, st.structuresBuilt, st.itemsCrafted, st.colonistsAlive,
        MMSS(st.elapsedSeconds).c_str());
    SetWindowTextW(hwnd, title);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE:
        if (gDev.dev) {
            gDev.width  = LOWORD(l);
            gDev.height = HIWORD(l);
            HR(gDev.swap->ResizeBuffers(0, gDev.width, gDev.height, DXGI_FORMAT_UNKNOWN, 0));
            gDev.recreateRT();
        }
        return 0;
    case WM_SYSKEYDOWN:
        // Alt+Enter fullscreen toggle
        if (w == VK_RETURN && (HIWORD(l) & KF_ALTDOWN)) { gDev.toggleFullscreen(); return 0; }
        break;
    case WM_DESTROY: gRunning = false; PostQuitMessage(0); return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) { DestroyWindow(h); } return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cl, int) {
    ParseArgs(cl);

    WNDCLASSW wc{}; wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"SliceWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Colony Vertical Slice", WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, hi, nullptr);

    gDev.create(hwnd, 1280, 720);
    gSlice.create(gDev);

    // Optional: localized labels for the default tracker tokens
    g_slice.setLocalizer([](std::string_view tok)->std::string{
        if (tok=="EstablishColony") return "Establish the colony";
        if (tok=="BuildDesc") return "Build structures";
        if (tok=="BuildStructures") return "Build structures";
        if (tok=="EnableProduction") return "Enable production";
        if (tok=="CraftDesc") return "Craft items";
        if (tok=="CraftItems") return "Craft items";
        if (tok=="WeatherTheNight") return "Weather the night";
        if (tok=="SurviveDesc") return "Survive the timer";
        if (tok=="SurviveTimer") return "Survive timer";
        if (tok=="NoDeaths60s") return "No deaths in last 60s";
        if (tok=="NoRecentDeaths") return "No recent deaths";
        if (tok=="KeepThemAlive") return "Keep them alive";
        if (tok=="EndWith3Colonists") return "Finish with at least 3 colonists alive";
        if (tok=="ColonistsGte3") return "Colonists \u2265 3";
        return std::string(tok);
    });

    // Fixed-step update for determinism; render every loop
    const double dtFixed = 1.0 / 120.0;
    double acc = 0.0;
    auto tPrev = std::chrono::high_resolution_clock::now();

    MSG msg{};
    while (gRunning) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }

        auto tNow = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(tNow - tPrev).count();
        tPrev = tNow;
        if (dt > 0.25) dt = 0.25; // after breakpoints, etc.

        gSlice.fps.tick(dt);
        UpdateWindowTitle(hwnd, gSlice);

        acc += dt;
        while (acc >= dtFixed) {
            gSlice.updateSim(dtFixed);
            acc -= dtFixed;
        }

        float clear[4] = { 0.06f, 0.09f, 0.12f, 1.f };
        gDev.beginFrame(clear);
        gSlice.renderFrame();
        gDev.present(gSlice.vsync);
    }
    return 0;
}
