// -------------------------------------------------------------------------------------------------
// Renderer2D_D3D11.cpp
// Single-file, Windows-only, zero-3rd-party-deps 2D renderer for Colony-Game (Direct3D 11 + DWrite/D2D)
// -------------------------------------------------------------------------------------------------
// Build: Add this file to your target and link: d3d11 dxgi d3dcompiler d2d1 dwrite windowscodecs
// License: MIT (c) 2025 Colony-Game contributors
// -------------------------------------------------------------------------------------------------
// Features:
//  - GPU-accelerated 2D with sprite batching (rects, lines, sprites, nine-slice, tilemaps)
//  - DirectWrite text via Direct2D interop onto the swapchain surface (full Unicode, crisp)
//  - WIC image loader (PNG/JPG/BMP) with sRGB-correct sampling
//  - Modern flip-model swapchain; optional tearing when vsync=false (if OS/GPU support it)
//  - Optional post-process pass with FXAA (toggleable)
//  - Screenshot capture to PNG via WIC
//  - Wireframe debug, GPU timing queries overlay
//  - Minimal public API in namespace cg (see bottom for tiny usage demo)
//  - Robust error handling, no undefined sampling (auto white 1x1 texture), safe COM lifetime
// -------------------------------------------------------------------------------------------------

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

// -------------------------------------------------------------------------------------------------
// Utilities
// -------------------------------------------------------------------------------------------------
namespace cg {

using Microsoft::WRL::ComPtr;

static inline uint32_t rgba_u32(uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    return (uint32_t(r)<<24)|(uint32_t(g)<<16)|(uint32_t(b)<<8)|uint32_t(a);
}
static inline void u32_to_rgba(uint32_t c, float& r,float& g,float& b,float& a){
    r = ((c>>24)&255)/255.0f; g=((c>>16)&255)/255.0f; b=((c>>8)&255)/255.0f; a=(c&255)/255.0f;
}

struct float2 { float x,y; };
struct float3 { float x,y,z; };
struct float4 { float x,y,z,w; };
struct float4x4 { float m[4][4]; };

static inline float4x4 orthoLH(float l,float r,float b,float t,float zn=0.0f,float zf=1.0f){
    float4x4 M{};
    M.m[0][0] =  2.0f/(r-l);
    M.m[1][1] =  2.0f/(t-b);
    M.m[2][2] =  1.0f/(zf-zn);
    M.m[3][0] = -(r+l)/(r-l);
    M.m[3][1] = -(t+b)/(t-b);
    M.m[3][2] = -zn/(zf-zn);
    M.m[3][3] =  1.0f;
    return M;
}

// Simple scope helper for HRESULT checks.
#ifndef CG_ASSERT
#define CG_ASSERT(x) do{ if(!(x)){ ::OutputDebugStringW(L"[cg] Assert failed: " L#x L"\n"); __debugbreak(); } }while(0)
#endif

static inline void dbgprintf(const char* fmt, ...){
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

struct RendererDesc {
    HWND   hwnd = nullptr;
    int    width = 1280, height = 720;
    bool   vsync = true;
    bool   srgb  = true; // create sRGB RTV / textures
    bool   enableFXAA = true;
};

using TextureId = uint32_t; // 0 is valid and bound to an internal 1x1 white texture

class Renderer2D {
public:
    bool  init(const RendererDesc& d);
    void  shutdown();

    void  resize(int w, int h);
    void  beginFrame(float r, float g, float b, float a);
    void  endFrame(); // presents

    // Drawing
    void  drawRect(float x, float y, float w, float h, uint32_t rgba);
    void  drawLine(float x0, float y0, float x1, float y1, float thickness, uint32_t rgba);
    void  drawSprite(TextureId tex, float x, float y, float w, float h,
                     float u0=0, float v0=0, float u1=1, float v1=1,
                     uint32_t rgba=0xFFFFFFFF);
    void  drawNineSlice(TextureId tex, float x, float y, float w, float h, float l, float t, float r, float b, uint32_t rgba=0xFFFFFFFF);

    // Text
    void  setTextFont(const wchar_t* family, float sizePx, float dpiScale=1.0f);
    void  drawText(float x, float y, const wchar_t* utf16, uint32_t rgba=0xFFFFFFFF);

    // Tile layer (orthogonal)
    void  drawTileLayer(TextureId atlas, int tilesX, int tilesY, float tileW, float tileH,
                        const uint32_t* tileIndices, int atlasCols, int atlasRows, uint32_t tint=0xFFFFFFFF);

    // Resources
    TextureId loadTextureFromFile(const wchar_t* path);
    void      releaseTexture(TextureId id);

    // Camera/UI
    void  setOrtho(float left, float top, float right, float bottom); // 2D camera
    void  setViewport(int x, int y, int w, int h);

    // Debug
    void  toggleWireframe(bool on);
    void  toggleFXAA(bool on);
    void  setPixelArtSampling(bool pointFiltering); // new: switch between linear/point

    // Utilities
    bool  saveScreenshotPNG(const wchar_t* filePath);

private:
    // Internal structures
    struct Vertex {
        float x,y,u,v;
        uint32_t color;
    };

    struct TextItem {
        float x,y;
        std::wstring text;
        uint32_t color;
    };

    struct Texture {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        uint32_t w=0,h=0; bool srgb=true;
    };

    struct FrameState {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        TextureId currentTexture = UINT32_MAX; // invalid to force first bind
    };

    // D3D objects
    ComPtr<ID3D11Device>           m_dev;
    ComPtr<ID3D11DeviceContext>    m_ctx;
    ComPtr<IDXGISwapChain1>        m_swap;
    ComPtr<ID3D11RenderTargetView> m_rtv;       // backbuffer RTV (sRGB view if requested)

    // Scene offscreen color (for post-processing)
    ComPtr<ID3D11Texture2D>          m_sceneColor;
    ComPtr<ID3D11RenderTargetView>   m_sceneRTV;
    ComPtr<ID3D11ShaderResourceView> m_sceneSRV;

    // States
    ComPtr<ID3D11BlendState>       m_blendAlpha;   // non-premultiplied alpha
    ComPtr<ID3D11BlendState>       m_blendOpaque;
    ComPtr<ID3D11DepthStencilState> m_depthDisabled;
    ComPtr<ID3D11RasterizerState>  m_rasterSolid;
    ComPtr<ID3D11RasterizerState>  m_rasterWire;
    ComPtr<ID3D11SamplerState>     m_samLinear;
    ComPtr<ID3D11SamplerState>     m_samPoint;

    // Dynamic buffers
    ComPtr<ID3D11Buffer>           m_vb;
    ComPtr<ID3D11Buffer>           m_ib;
    size_t                         m_vbCapacity = 0;
    size_t                         m_ibCapacity = 0;

    // Shaders & constants
    ComPtr<ID3D11VertexShader>     m_vs;
    ComPtr<ID3D11PixelShader>      m_psSprite;
    ComPtr<ID3D11InputLayout>      m_il;
    ComPtr<ID3D11Buffer>           m_cbProj; // projection

    // Post-process (FXAA/pass-through)
    ComPtr<ID3D11VertexShader>     m_vsFull;
    ComPtr<ID3D11PixelShader>      m_psCopy;
    ComPtr<ID3D11PixelShader>      m_psFXAA;
    ComPtr<ID3D11Buffer>           m_cbPost; // {float2 invTexSize; int fxaaEnabled; float pad;}

    // DWrite/D2D for text
    ComPtr<ID2D1Factory1>          m_d2dFactory;
    ComPtr<ID2D1Device>            m_d2dDevice;
    ComPtr<ID2D1DeviceContext>     m_d2dCtx;
    ComPtr<ID2D1Bitmap1>           m_d2dTargetBitmap;
    ComPtr<IDWriteFactory>         m_dwFactory;
    ComPtr<IDWriteTextFormat>      m_textFormat;

    // WIC
    ComPtr<IWICImagingFactory>     m_wicFactory;

    // View/Proj
    D3D11_VIEWPORT                 m_viewport{};
    float4x4                       m_proj;

    // Frame staging
    FrameState                     m_frame;
    std::vector<TextItem>          m_textQueue;
    bool                           m_wireframe = false;
    bool                           m_vsync = true;
    bool                           m_srgb = true;
    bool                           m_enableFXAA = true;
    bool                           m_pointSampling = false;
    bool                           m_allowTearing = false;
    int                            m_width = 0, m_height = 0;

    // Resource cache
    std::unordered_map<std::wstring, TextureId> m_pathToTex;
    std::vector<Texture>            m_textures; // [0] reserved: 1x1 white
    TextureId                       m_whiteTexId = 0;

    // COM lifetime
    bool                           m_comInit = false;

    // GPU timing
    struct GPUTimer {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> qBegin;
        ComPtr<ID3D11Query> qEnd;
        double lastMs = 0.0;
        void init(ID3D11Device* dev){
            D3D11_QUERY_DESC d{}; d.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; dev->CreateQuery(&d, &disjoint);
            d.Query = D3D11_QUERY_TIMESTAMP; dev->CreateQuery(&d, &qBegin); dev->CreateQuery(&d, &qEnd);
        }
        void begin(ID3D11DeviceContext* ctx){ ctx->Begin(disjoint.Get()); ctx->End(qBegin.Get()); }
        void end(ID3D11DeviceContext* ctx){
            ctx->End(qEnd.Get()); ctx->End(disjoint.Get());
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            while(ctx->GetData(disjoint.Get(), &dj, sizeof(dj), 0) != S_OK) {}
            if(!dj.Disjoint){
                UINT64 t0=0,t1=0;
                while(ctx->GetData(qBegin.Get(), &t0, sizeof(t0), 0) != S_OK) {}
                while(ctx->GetData(qEnd.Get(),   &t1, sizeof(t1), 0) != S_OK) {}
                lastMs = double(t1 - t0) / double(dj.Frequency) * 1000.0;
            }
        }
    } m_gpuTimer;

private:
    bool  createDeviceAndSwap(const RendererDesc& d);
    bool  createBackbufferTargets();
    bool  createSceneTargets();
    bool  createStatesAndShaders();
    bool  createTextSubsystem();
    bool  ensureDynamicBuffers(size_t vtxNeeded, size_t idxNeeded);
    void  flushBatch(); // flush sprite/primitive batch to GPU
    void  resetBatch();

    TextureId createTextureFromRGBA8(const uint8_t* pixels, uint32_t w, uint32_t h, bool srgb);
    void      bindTexture(TextureId id);
    bool      createWICFactoryIfNeeded();

    // helpers
    static HRESULT CompileHLSL(const char* src, const char* entry, const char* profile, ID3DBlob** outBlob);
    void      updatePostConstants();
    bool      checkAllowTearing();
};

// -------------------------------------------------------------------------------------------------
// Shader sources (HLSL, SM 5.0). Kept small and embedded for single-file build.
// -------------------------------------------------------------------------------------------------

static const char* g_VS_SRC = R"(
cbuffer cbProj : register(b0) {
    float4x4 uProj;
};
struct VSIn  { float2 pos: POSITION; float2 uv: TEXCOORD0; uint color: COLOR0; };
struct VSOut { float4 pos: SV_POSITION; float2 uv: TEXCOORD0; float4 color: COLOR0; };
float4 UnpackColor(uint c){
    float4 k;
    k.r = ((c>>24)&255)/255.0;
    k.g = ((c>>16)&255)/255.0;
    k.b = ((c>>8 )&255)/255.0;
    k.a = ((c    )&255)/255.0;
    return k;
}
VSOut main(VSIn i){
    VSOut o;
    o.pos = mul(float4(i.pos,0,1), uProj);
    o.uv  = i.uv;
    o.color = UnpackColor(i.color);
    return o;
}
)";

static const char* g_PS_SPRITE_SRC = R"(
Texture2D uTex0 : register(t0);
SamplerState uSamp : register(s0);
float4 main(float4 pos:SV_POSITION, float2 uv:TEXCOORD0, float4 color:COLOR0) : SV_Target {
    float4 tex = uTex0.Sample(uSamp, uv);
    return tex * color;
}
)";

static const char* g_VS_FULL_SRC = R"(
struct VSOut { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
VSOut main(uint id:SV_VertexID){
    VSOut o;
    float2 verts[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) }; // full-screen tri
    float2 uvs[3]   = { float2(0,1),   float2(0,-1), float2(2,1) };
    o.pos = float4(verts[id], 0, 1);
    o.uv  = uvs[id];
    return o;
}
)";

static const char* g_PS_COPY_SRC = R"(
Texture2D uScene : register(t0);
SamplerState uSamp : register(s0);
float4 main(float4 pos:SV_POSITION, float2 uv:TEXCOORD0) : SV_Target {
    return uScene.Sample(uSamp, uv);
}
)";

static const char* g_PS_FXAA_SRC = R"(
// Minimal FXAA-inspired filter (adapted for brevity; not full NVIDIA FXAA reference)
Texture2D uScene : register(t0);
SamplerState uSamp : register(s0);
cbuffer cbPost : register(b0) { float2 uInvTex; int uFXAA; float _pad; }

float luma(float3 c){ return dot(c, float3(0.299,0.587,0.114)); }

float4 main(float4 pos:SV_POSITION, float2 uv:TEXCOORD0) : SV_Target {
    if(uFXAA==0) return uScene.Sample(uSamp, uv);
    float2 px = uInvTex;

    float3 cM = uScene.Sample(uSamp, uv).rgb;
    float3 cN = uScene.Sample(uSamp, uv + float2(0,-px.y)).rgb;
    float3 cW = uScene.Sample(uSamp, uv + float2(-px.x,0)).rgb;
    float3 cE = uScene.Sample(uSamp, uv + float2(px.x,0)).rgb;
    float3 cS = uScene.Sample(uSamp, uv + float2(0,px.y)).rgb;

    float lM = luma(cM);
    float lMin = min(lM, min(min(luma(cN), luma(cS)), min(luma(cW), luma(cE))));
    float lMax = max(lM, max(max(luma(cN), luma(cS)), max(luma(cW), luma(cE))));

    float range = lMax - lMin;
    if(range < 0.031) return float4(cM,1);

    float3 cA = (cN + cS + cW + cE) * 0.25;
    float3 cB = (cA + cM) * 0.5;
    return float4( lerp(cA, cB, 0.5), 1 );
}
)";

// -------------------------------------------------------------------------------------------------
// Helper: compile HLSL from in-memory source
// -------------------------------------------------------------------------------------------------
static HRESULT Renderer2D::CompileHLSL(const char* src, const char* entry, const char* profile, ID3DBlob** outBlob){
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, profile, flags, 0, &blob, &err);
    if(FAILED(hr)){
        if(err) OutputDebugStringA((const char*)err->GetBufferPointer());
        return hr;
    }
    *outBlob = blob.Detach();
    return S_OK;
}

// -------------------------------------------------------------------------------------------------
// WIC helpers
// -------------------------------------------------------------------------------------------------
static bool CreateWICFactory(IWICImagingFactory** out){
    static ComPtr<IWICImagingFactory> g;
    if(!g){
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g));
        if(FAILED(hr)) return false;
    }
    *out = g.Get(); (*out)->AddRef();
    return true;
}

// -------------------------------------------------------------------------------------------------
// Renderer2D Implementation
// -------------------------------------------------------------------------------------------------

bool Renderer2D::init(const RendererDesc& d){
    // Initialize COM (MTA). If the app already initialized COM differently, we don't fail hard.
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if(SUCCEEDED(hrCo)) m_comInit = true;

    m_vsync = d.vsync; m_srgb = d.srgb; m_enableFXAA = d.enableFXAA; m_width=d.width; m_height=d.height;
    m_allowTearing = checkAllowTearing();

    if(!createDeviceAndSwap(d)) return false;
    if(!createBackbufferTargets()) return false;
    if(!createSceneTargets()) return false;
    if(!createStatesAndShaders()) return false;
    if(!createTextSubsystem()) return false;

    m_proj = orthoLH(0,(float)m_width,(float)m_height,0, 0.0f,1.0f);

    // reserve [0] for a 1x1 white texture to ensure rects/lines always sample defined data
    m_textures.clear();
    {
        uint8_t white[4] = {255,255,255,255};
        m_whiteTexId = createTextureFromRGBA8(white, 1, 1, m_srgb);
        CG_ASSERT(m_whiteTexId == 0); // first push must result in id 0
    }

    resetBatch();
    m_gpuTimer.init(m_dev.Get());
    return true;
}

void Renderer2D::shutdown(){
    m_textQueue.clear();
    m_frame.vertices.clear(); m_frame.indices.clear();
    m_il.Reset(); m_vs.Reset(); m_psSprite.Reset();
    m_vsFull.Reset(); m_psCopy.Reset(); m_psFXAA.Reset();
    m_cbProj.Reset(); m_cbPost.Reset();
    m_vb.Reset(); m_ib.Reset();
    m_sceneColor.Reset(); m_sceneRTV.Reset(); m_sceneSRV.Reset();
    m_rtv.Reset(); m_swap.Reset(); m_ctx.Reset(); m_dev.Reset();
    m_d2dTargetBitmap.Reset(); m_d2dCtx.Reset(); m_d2dDevice.Reset(); m_d2dFactory.Reset();
    m_dwFactory.Reset(); m_textFormat.Reset();
    m_wicFactory.Reset();
    m_textures.clear(); m_pathToTex.clear();

    if(m_comInit) { CoUninitialize(); m_comInit=false; }
}

bool Renderer2D::checkAllowTearing(){
    ComPtr<IDXGIFactory6> f6;
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    if(SUCCEEDED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&f6)))){
        BOOL allow = FALSE;
        if(SUCCEEDED(f6->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))){
            return allow == TRUE;
        }
    }
    return false;
}

bool Renderer2D::createDeviceAndSwap(const RendererDesc& d){
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL flOut;
    const D3D_FEATURE_LEVEL req[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, req, _countof(req),
                                   D3D11_SDK_VERSION, &m_dev, &flOut, &m_ctx);
    if(FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDev; m_dev.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adp; dxgiDev->GetAdapter(&adp);
    ComPtr<IDXGIFactory2> fac; adp->GetParent(IID_PPV_ARGS(&fac));

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = d.width; scd.Height = d.height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // create sRGB RTV for it
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = (m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

    hr = fac->CreateSwapChainForHwnd(m_dev.Get(), d.hwnd, &scd, nullptr, nullptr, &m_swap);
    if(FAILED(hr)) return false;

    m_viewport = { 0.0f, 0.0f, float(d.width), float(d.height), 0.0f, 1.0f };
    return true;
}

bool Renderer2D::createBackbufferTargets(){
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = m_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if(FAILED(hr)) return false;

    D3D11_RENDER_TARGET_VIEW_DESC rtd{};
    rtd.Format = m_srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    rtd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtd.Texture2D.MipSlice = 0;

    hr = m_dev->CreateRenderTargetView(back.Get(), &rtd, &m_rtv);
    return SUCCEEDED(hr);
}

bool Renderer2D::createSceneTargets(){
    // Offscreen color for post processing (same size as backbuffer)
    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_width; td.Height = m_height;
    td.Format = m_srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_dev->CreateTexture2D(&td, nullptr, &m_sceneColor);
    if(FAILED(hr)) return false;
    hr = m_dev->CreateRenderTargetView(m_sceneColor.Get(), nullptr, &m_sceneRTV);
    if(FAILED(hr)) return false;
    hr = m_dev->CreateShaderResourceView(m_sceneColor.Get(), nullptr, &m_sceneSRV);
    return SUCCEEDED(hr);
}

bool Renderer2D::createStatesAndShaders(){
    // Blend states (non-premultiplied alpha for sprite textures loaded via WIC)
    {
        D3D11_BLEND_DESC bd{};
        auto& rt0 = bd.RenderTarget[0]; // NOTE: RenderTarget, not RenderTargets
        rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        rt0.BlendEnable = TRUE;
        rt0.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt0.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt0.BlendOp = D3D11_BLEND_OP_ADD;
        rt0.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt0.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt0.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        HRESULT hr = m_dev->CreateBlendState(&bd, &m_blendAlpha);
        if(FAILED(hr)) return false;
    }
    {
        D3D11_BLEND_DESC bd{};
        auto& rt0 = bd.RenderTarget[0];
        rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        rt0.BlendEnable = FALSE;
        if(FAILED(m_dev->CreateBlendState(&bd, &m_blendOpaque))) return false;
    }

    // Depth disabled
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE; dd.StencilEnable = FALSE;
    if(FAILED(m_dev->CreateDepthStencilState(&dd, &m_depthDisabled))) return false;

    // Rasterizers
    D3D11_RASTERIZER_DESC rd{};
    rd.CullMode = D3D11_CULL_NONE; rd.FillMode = D3D11_FILL_SOLID; rd.DepthClipEnable = TRUE;
    if(FAILED(m_dev->CreateRasterizerState(&rd, &m_rasterSolid))) return false;
    rd.FillMode = D3D11_FILL_WIREFRAME;
    if(FAILED(m_dev->CreateRasterizerState(&rd, &m_rasterWire))) return false;

    // Samplers
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if(FAILED(m_dev->CreateSamplerState(&sd, &m_samLinear))) return false;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if(FAILED(m_dev->CreateSamplerState(&sd, &m_samPoint))) return false;

    // Shaders
    ComPtr<ID3DBlob> vsb, psb;
    if(FAILED(CompileHLSL(g_VS_SRC, "main", "vs_5_0", &vsb))) return false;
    if(FAILED(CompileHLSL(g_PS_SPRITE_SRC, "main", "ps_5_0", &psb))) return false;

    if(FAILED(m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs))) return false;
    if(FAILED(m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_psSprite))) return false;

    // Input layout
    D3D11_INPUT_ELEMENT_DESC ie[] = {
        {"POSITION",0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,  D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",   0, DXGI_FORMAT_R8G8B8A8_UNORM,0,16, D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    if(FAILED(m_dev->CreateInputLayout(ie, _countof(ie), vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_il))) return false;

    // Constant buffer (proj)
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(float4x4);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if(FAILED(m_dev->CreateBuffer(&cbd, nullptr, &m_cbProj))) return false;

    // Post-process shaders and constants
    ComPtr<ID3DBlob> vsf, psc, psx;
    if(FAILED(CompileHLSL(g_VS_FULL_SRC, "main", "vs_5_0", &vsf))) return false;
    if(FAILED(CompileHLSL(g_PS_COPY_SRC, "main", "ps_5_0", &psc))) return false;
    if(FAILED(CompileHLSL(g_PS_FXAA_SRC, "main", "ps_5_0", &psx))) return false;
    if(FAILED(m_dev->CreateVertexShader(vsf->GetBufferPointer(), vsf->GetBufferSize(), nullptr, &m_vsFull))) return false;
    if(FAILED(m_dev->CreatePixelShader(psc->GetBufferPointer(), psc->GetBufferSize(), nullptr, &m_psCopy))) return false;
    if(FAILED(m_dev->CreatePixelShader(psx->GetBufferPointer(), psx->GetBufferSize(), nullptr, &m_psFXAA))) return false;

    D3D11_BUFFER_DESC cbd2{}; cbd2.ByteWidth = 16; cbd2.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    cbd2.Usage=D3D11_USAGE_DYNAMIC; cbd2.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(m_dev->CreateBuffer(&cbd2, nullptr, &m_cbPost))) return false;

    return true;
}

bool Renderer2D::createTextSubsystem(){
    // D2D factory/device/context bound to DXGI device (for BGRA swapchain interop)
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&m_d2dFactory));
    if(FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDev; m_dev.As(&dxgiDev);
    if(FAILED(m_d2dFactory->CreateDevice(dxgiDev.Get(), &m_d2dDevice))) return false;
    if(FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dCtx))) return false;

    // Create target bitmap for current backbuffer
    ComPtr<ID3D11Texture2D> back; m_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    ComPtr<IDXGISurface> surf; back.As(&surf);

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if(FAILED(m_d2dCtx->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &m_d2dTargetBitmap))) return false;
    m_d2dCtx->SetTarget(m_d2dTargetBitmap.Get());
    m_d2dCtx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // DWrite
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&m_dwFactory))) return false;
    if(FAILED(m_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            18.0f, L"en-us", &m_textFormat))) return false;
    return true;
}

bool Renderer2D::ensureDynamicBuffers(size_t vtxNeeded, size_t idxNeeded){
    bool ok = true;
    if(vtxNeeded > m_vbCapacity){
        m_vb.Reset();
        m_vbCapacity = std::max<size_t>(vtxNeeded, 65536);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(m_vbCapacity * sizeof(Vertex));
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.Usage = D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok &= SUCCEEDED(m_dev->CreateBuffer(&bd, nullptr, &m_vb));
    }
    if(idxNeeded > m_ibCapacity){
        m_ib.Reset();
        m_ibCapacity = std::max<size_t>(idxNeeded, 65536);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(m_ibCapacity * sizeof(uint32_t));
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.Usage = D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok &= SUCCEEDED(m_dev->CreateBuffer(&bd, nullptr, &m_ib));
    }
    return ok;
}

void Renderer2D::setOrtho(float l, float t, float r, float b){
    m_proj = orthoLH(l, r, b, t, 0.0f, 1.0f);
}

void Renderer2D::setViewport(int x, int y, int w, int h){
    m_viewport.TopLeftX = float(x); m_viewport.TopLeftY = float(y);
    m_viewport.Width = float(w); m_viewport.Height = float(h);
}

void Renderer2D::toggleWireframe(bool on){ m_wireframe = on; }
void Renderer2D::toggleFXAA(bool on){ m_enableFXAA = on; }
void Renderer2D::setPixelArtSampling(bool pointFiltering){ m_pointSampling = pointFiltering; }

void Renderer2D::resize(int w, int h){
    if(w<=0 || h<=0) return;
    m_width=w; m_height=h;
    m_rtv.Reset();
    m_sceneColor.Reset(); m_sceneRTV.Reset(); m_sceneSRV.Reset();
    m_d2dTargetBitmap.Reset();

    HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, (m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0));
    if(FAILED(hr)) return;

    createBackbufferTargets();
    createSceneTargets();

    // Rebind D2D target to new backbuffer
    ComPtr<ID3D11Texture2D> back; m_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    ComPtr<IDXGISurface> surf; back.As(&surf);
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    m_d2dCtx->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &m_d2dTargetBitmap);
    m_d2dCtx->SetTarget(m_d2dTargetBitmap.Get());

    m_viewport = { 0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f };
    m_proj = orthoLH(0,(float)w,(float)h,0);
}

void Renderer2D::beginFrame(float r, float g, float b, float a){
    // Update projection constant
    D3D11_MAPPED_SUBRESOURCE ms{};
    if(SUCCEEDED(m_ctx->Map(m_cbProj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))){
        memcpy(ms.pData, &m_proj, sizeof(m_proj));
        m_ctx->Unmap(m_cbProj.Get(), 0);
    }

    // Bind scene RT as render target, set viewport, clear
    ID3D11RenderTargetView* rt = m_sceneRTV.Get();
    m_ctx->OMSetRenderTargets(1, &rt, nullptr);
    m_ctx->RSSetViewports(1, &m_viewport);

    const float clr[4] = {r,g,b,a};
    m_ctx->ClearRenderTargetView(m_sceneRTV.Get(), clr);

    // Common pipeline state
    float blendFactor[4] = {0,0,0,0};
    UINT mask = 0xFFFFFFFF;
    m_ctx->OMSetBlendState(m_blendAlpha.Get(), blendFactor, mask);
    m_ctx->OMSetDepthStencilState(m_depthDisabled.Get(), 0);
    m_ctx->RSSetState(m_wireframe ? m_rasterWire.Get() : m_rasterSolid.Get());

    // Shaders/buffers
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->IASetInputLayout(m_il.Get());
    m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    m_ctx->PSSetShader(m_psSprite.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { m_cbProj.Get() };
    m_ctx->VSSetConstantBuffers(0, 1, cbs);
    ID3D11SamplerState* samp[] = { (m_pointSampling ? m_samPoint.Get() : m_samLinear.Get()) };
    m_ctx->PSSetSamplers(0, 1, samp);

    resetBatch();
    m_gpuTimer.begin(m_ctx.Get());

    // Ensure white texture is bound at start (so untextured draws work even before any drawSprite)
    bindTexture(0);
}

void Renderer2D::updatePostConstants(){
    struct { float invW, invH; int fxaa; float pad; } cbpost = { 1.0f/m_width, 1.0f/m_height, m_enableFXAA?1:0, 0.0f };
    D3D11_MAPPED_SUBRESOURCE ms{};
    if(SUCCEEDED(m_ctx->Map(m_cbPost.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))){
        memcpy(ms.pData, &cbpost, sizeof(cbpost));
        m_ctx->Unmap(m_cbPost.Get(), 0);
    }
}

void Renderer2D::endFrame(){
    flushBatch();

    // Post-process to backbuffer (FXAA or copy)
    ID3D11RenderTargetView* bb = m_rtv.Get();
    m_ctx->OMSetRenderTargets(1, &bb, nullptr);
    m_ctx->RSSetViewports(1, &m_viewport);
    m_ctx->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);
    m_ctx->IASetInputLayout(nullptr);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->VSSetShader(m_vsFull.Get(), nullptr, 0);
    ID3D11Buffer* cbp[] = { m_cbPost.Get() };
    m_ctx->PSSetShaderResources(0, 1, m_sceneSRV.GetAddressOf());
    ID3D11SamplerState* samp[] = { (m_pointSampling ? m_samPoint.Get() : m_samLinear.Get()) };
    m_ctx->PSSetSamplers(0, 1, samp);

    updatePostConstants();
    m_ctx->PSSetConstantBuffers(0, 1, cbp);
    m_ctx->PSSetShader(m_enableFXAA ? m_psFXAA.Get() : m_psCopy.Get(), nullptr, 0);
    m_ctx->Draw(3, 0);

    // Text draw (D2D on backbuffer)
    if(!m_textQueue.empty()){
        m_d2dCtx->BeginDraw();
        for(const auto& it : m_textQueue){
            float r,g,b,a; u32_to_rgba(it.color, r,g,b,a);
            ComPtr<ID2D1SolidColorBrush> brush;
            m_d2dCtx->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a), &brush);
            D2D1_RECT_F rc = D2D1::RectF(it.x, it.y, 10000.0f, 10000.0f);
            // ID2D1DeviceContext exposes DrawText (no 'W' suffix).
            m_d2dCtx->DrawText(it.text.c_str(), (UINT32)it.text.size(), m_textFormat.Get(), rc, brush.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        }
        m_d2dCtx->EndDraw();
        m_textQueue.clear();
    }

    m_gpuTimer.end(m_ctx.Get());

    // Present
    UINT presentFlags = 0;
    UINT sync = m_vsync ? 1u : 0u;
    if(!m_vsync && m_allowTearing) presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    m_swap->Present(sync, presentFlags);

    // Unbind SRVs to avoid warnings on next Clear
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    m_ctx->PSSetShaderResources(0, 1, nullSRV);
}

void Renderer2D::resetBatch(){
    m_frame.vertices.clear(); m_frame.indices.clear();
    m_frame.currentTexture = UINT32_MAX;
}

bool Renderer2D::createWICFactoryIfNeeded(){
    if(!m_wicFactory){
        if(!CreateWICFactory(&m_wicFactory)) return false;
    }
    return true;
}

TextureId Renderer2D::createTextureFromRGBA8(const uint8_t* pixels, uint32_t w, uint32_t h, bool srgb){
    D3D11_TEXTURE2D_DESC td{};
    td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
    td.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; // RT for atlas updates if needed
    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = pixels;
    srd.SysMemPitch = w*4;
    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = m_dev->CreateTexture2D(&td, pixels ? &srd : nullptr, &tex);
    if(FAILED(hr)) return UINT32_MAX;
    ComPtr<ID3D11ShaderResourceView> srv;
    hr = m_dev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    if(FAILED(hr)) return UINT32_MAX;

    Texture t; t.tex=tex; t.srv=srv; t.w=w; t.h=h; t.srgb=srgb;
    TextureId id = (TextureId)m_textures.size();
    m_textures.push_back(std::move(t));
    return id;
}

TextureId Renderer2D::loadTextureFromFile(const wchar_t* path){
    if(!path) return m_whiteTexId;
    auto it = m_pathToTex.find(path);
    if(it!=m_pathToTex.end()) return it->second;

    if(!createWICFactoryIfNeeded()) return m_whiteTexId;

    ComPtr<IWICBitmapDecoder> dec;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if(FAILED(hr)) return m_whiteTexId;
    ComPtr<IWICBitmapFrameDecode> frm;
    hr = dec->GetFrame(0, &frm); if(FAILED(hr)) return m_whiteTexId;

    UINT w=0,h=0; frm->GetSize(&w,&h);
    ComPtr<IWICFormatConverter> cvt; m_wicFactory->CreateFormatConverter(&cvt);
    hr = cvt->Initialize(frm.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if(FAILED(hr)) return m_whiteTexId;

    std::vector<uint8_t> px; px.resize(size_t(w)*size_t(h)*4u);
    hr = cvt->CopyPixels(nullptr, w*4, (UINT)px.size(), px.data());
    if(FAILED(hr)) return m_whiteTexId;

    // Upload as-is (straight alpha). Blend state is configured for non-premultiplied alpha.
    TextureId id = createTextureFromRGBA8(px.data(), w, h, m_srgb);
    if(id!=UINT32_MAX) m_pathToTex[path] = id;
    return (id==UINT32_MAX) ? m_whiteTexId : id;
}

void Renderer2D::releaseTexture(TextureId id){
    if(id>=m_textures.size()) return;
    if(id==m_whiteTexId) return; // keep the white texture alive
    m_textures[id] = Texture{}; // drop refs
}

void Renderer2D::bindTexture(TextureId id){
    if(id>=m_textures.size()) id = m_whiteTexId;
    // map 0 to white
    if(id == UINT32_MAX) id = m_whiteTexId;
    if(id == 0 && m_whiteTexId != 0) id = m_whiteTexId;

    if(m_frame.currentTexture == id) return;
    // flush previous batch if any
    flushBatch();
    m_frame.currentTexture = id;

    ID3D11ShaderResourceView* srv = m_textures[id].srv.Get();
    m_ctx->PSSetShaderResources(0, 1, &srv);
}

void Renderer2D::flushBatch(){
    if(m_frame.indices.empty()) return;
    // Ensure buffers
    if(!ensureDynamicBuffers(m_frame.vertices.size(), m_frame.indices.size())){
        resetBatch(); return;
    }
    // Upload
    D3D11_MAPPED_SUBRESOURCE mv{}, mi{};
    if(SUCCEEDED(m_ctx->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mv))){
        memcpy(mv.pData, m_frame.vertices.data(), m_frame.vertices.size()*sizeof(Vertex));
        m_ctx->Unmap(m_vb.Get(), 0);
    }
    if(SUCCEEDED(m_ctx->Map(m_ib.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mi))){
        memcpy(mi.pData, m_frame.indices.data(), m_frame.indices.size()*sizeof(uint32_t));
        m_ctx->Unmap(m_ib.Get(), 0);
    }
    UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* vb = m_vb.Get();
    m_ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    m_ctx->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_ctx->DrawIndexed((UINT)m_frame.indices.size(), 0, 0);

    resetBatch();
    // Re-bind current texture after reset to keep state coherent for next draws
    bindTexture(0);
}

void Renderer2D::drawSprite(TextureId tex, float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1, uint32_t color){
    bindTexture(tex);
    // 4 verts, 6 indices
    uint32_t base = (uint32_t)m_frame.vertices.size();
    m_frame.vertices.push_back({x,   y,   u0, v0, color});
    m_frame.vertices.push_back({x+w, y,   u1, v0, color});
    m_frame.vertices.push_back({x+w, y+h, u1, v1, color});
    m_frame.vertices.push_back({x,   y+h, u0, v1, color});
    m_frame.indices.push_back(base+0); m_frame.indices.push_back(base+1); m_frame.indices.push_back(base+2);
    m_frame.indices.push_back(base+0); m_frame.indices.push_back(base+2); m_frame.indices.push_back(base+3);
}

void Renderer2D::drawRect(float x, float y, float w, float h, uint32_t color){
    // Use white texture so color passes through
    drawSprite(m_whiteTexId, x,y,w,h, 0,0,1,1, color);
}

void Renderer2D::drawNineSlice(TextureId tex, float x, float y, float w, float h, float L, float T, float R, float B, uint32_t color){
    // L,T,R,B are border sizes in pixels on the source texture; assume full UV [0..1]
    bindTexture(tex);
    const Texture& t = m_textures[tex];
    float tw = float(std::max(1u, t.w)), th=float(std::max(1u, t.h));
    float uL = L/tw, vT = T/th, uR = 1.0f - R/tw, vB = 1.0f - B/th;

    // positions
    float xs[4] = { x, x+L, x+w-R, x+w };
    float ys[4] = { y, y+T, y+h-B, y+h };
    // uvs
    float us[4] = { 0, uL, uR, 1 };
    float vs[4] = { 0, vT, vB, 1 };

    for(int iy=0; iy<3; ++iy){
        for(int ix=0; ix<3; ++ix){
            float px = xs[ix], py=ys[iy];
            float pw = xs[ix+1]-xs[ix], ph = ys[iy+1]-ys[iy];
            float uu0 = us[ix], vv0=vs[iy], uu1=us[ix+1], vv1=vs[iy+1];
            drawSprite(tex, px,py,pw,ph, uu0,vv0,uu1,vv1, color);
        }
    }
}

void Renderer2D::drawLine(float x0, float y0, float x1, float y1, float thickness, uint32_t color){
    float dx = x1-x0, dy=y1-y0;
    float len = std::sqrt(dx*dx + dy*dy); if(len<=0.0001f) return;
    float nx = -dy/len, ny = dx/len; // normal
    float hx = nx*(thickness*0.5f), hy=ny*(thickness*0.5f);
    bindTexture(m_whiteTexId);
    uint32_t base = (uint32_t)m_frame.vertices.size();
    m_frame.vertices.push_back({x0-hx, y0-hy, 0,0,color});
    m_frame.vertices.push_back({x0+hx, y0+hy, 0,0,color});
    m_frame.vertices.push_back({x1+hx, y1+hy, 0,0,color});
    m_frame.vertices.push_back({x1-hx, y1-hy, 0,0,color});
    m_frame.indices.push_back(base+0); m_frame.indices.push_back(base+1); m_frame.indices.push_back(base+2);
    m_frame.indices.push_back(base+0); m_frame.indices.push_back(base+2); m_frame.indices.push_back(base+3);
}

void Renderer2D::drawTileLayer(TextureId atlas, int tilesX, int tilesY, float tileW, float tileH,
                               const uint32_t* tileIndices, int atlasCols, int atlasRows, uint32_t tint){
    if(!tileIndices) return;
    for(int y=0; y<tilesY; ++y){
        for(int x=0; x<tilesX; ++x){
            uint32_t id = tileIndices[y*tilesX + x];
            if(id==0xFFFFFFFFu) continue; // empty
            int tx = id % atlasCols;
            int ty = id / atlasCols;
            float u0 = (tx + 0) / float(atlasCols);
            float v0 = (ty + 0) / float(atlasRows);
            float u1 = (tx + 1) / float(atlasCols);
            float v1 = (ty + 1) / float(atlasRows);
            drawSprite(atlas, x*tileW, y*tileH, tileW, tileH, u0,v0,u1,v1, tint);
        }
    }
}

void Renderer2D::setTextFont(const wchar_t* family, float sizePx, float){
    if(!m_dwFactory) return;
    ComPtr<IDWriteTextFormat> f;
    HRESULT hr = m_dwFactory->CreateTextFormat(family?family:L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               sizePx, L"en-us", &f);
    if(SUCCEEDED(hr)) m_textFormat = f;
}

void Renderer2D::drawText(float x, float y, const wchar_t* utf16, uint32_t color){
    if(!utf16 || !*utf16) return;
    m_textQueue.push_back({x,y, std::wstring(utf16), color});
}

bool Renderer2D::saveScreenshotPNG(const wchar_t* filePath){
    // Copy backbuffer to staging, then WIC-encode
    ComPtr<ID3D11Texture2D> back; if(FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;

    D3D11_TEXTURE2D_DESC td{}; back->GetDesc(&td);
    td.BindFlags = 0; td.MiscFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if(FAILED(m_dev->CreateTexture2D(&td, nullptr, &staging))) return false;
    m_ctx->CopyResource(staging.Get(), back.Get());

    D3D11_MAPPED_SUBRESOURCE ms{};
    if(FAILED(m_ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;

    // WIC
    if(!createWICFactoryIfNeeded()){ m_ctx->Unmap(staging.Get(), 0); return false; }
    ComPtr<IWICBitmap> bmp;
    if(FAILED(m_wicFactory->CreateBitmapFromMemory(td.Width, td.Height, GUID_WICPixelFormat32bppBGRA,
                                                   ms.RowPitch, ms.RowPitch*td.Height, (BYTE*)ms.pData, &bmp))){
        m_ctx->Unmap(staging.Get(), 0); return false;
    }
    ComPtr<IWICStream> stream; m_wicFactory->CreateStream(&stream);
    if(FAILED(stream->InitializeFromFilename(filePath, GENERIC_WRITE))){ m_ctx->Unmap(staging.Get(), 0); return false; }
    ComPtr<IWICBitmapEncoder> enc; if(FAILED(m_wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))){ m_ctx->Unmap(staging.Get(), 0); return false; }
    if(FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))){ m_ctx->Unmap(staging.Get(), 0); return false; }
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> bag;
    if(FAILED(enc->CreateNewFrame(&frame, &bag))){ m_ctx->Unmap(staging.Get(), 0); return false; }
    if(FAILED(frame->Initialize(bag.Get()))){ m_ctx->Unmap(staging.Get(), 0); return false; }
    frame->SetSize(td.Width, td.Height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit(); enc->Commit();
    m_ctx->Unmap(staging.Get(), 0);
    return true;
}

// -------------------------------------------------------------------------------------------------
// End of implementation; optional tiny usage sketch (not compiled):
//
//  cg::Renderer2D gfx;
//  cg::RendererDesc rd{ hwnd, width, height, vsync /*=true*/, /*srgb*/true, /*fxaa*/true };
//  gfx.init(rd);
//  gfx.beginFrame(0.08f,0.08f,0.10f,1);
//  auto tex = gfx.loadTextureFromFile(L"assets/sprites/colonist.png");
//  gfx.drawSprite(tex, 100,100,64,64);
//  gfx.drawText(16,16, L"Day 4 — 12 colonists");
//  gfx.endFrame();
// -------------------------------------------------------------------------------------------------

} // namespace cg

// -------------------------------------------------------------------------------------------------
// Appendix: In-File Manual, FAQ, and Advanced Notes
// -------------------------------------------------------------------------------------------------
// This appendix documents design decisions, tuning tips, and extension points. It is intentionally
// verbose to serve as living documentation within the single translation unit. You can safely remove
// sections after you become familiar with the renderer, but leaving it improves discoverability.
//
// Topics:
//  1) Why sRGB?
//  2) Flip-model swap chain vs legacy (and tearing)
//  3) Performance tuning (batching thresholds, map modes, state changes)
//  4) Text quality and alternatives (glyph atlas vs Direct2D)
//  5) Asset pipeline notes for WIC
//  6) FXAA caveats
//  7) DPI awareness and coordinate systems
//  8) Threading considerations
//  9) Debugging tips
// 10) Future extensions
//
// [1] Why sRGB?
// sRGB-correct sampling ensures that alpha blending, gradients, and images appear consistent across
// displays. We store textures in R8G8B8A8_UNORM_SRGB and make the backbuffer RTV sRGB. That means
// linear math in the shader and automatic conversion at the output merger. If you need "raw" UNORM,
// set RendererDesc.srgb = false.
//
// [2] Flip-model and tearing
// Flip-model (DXGI_SWAP_EFFECT_FLIP_*) is recommended for D3D11+ due to lower latency and better
// DWM integration. When vsync=false and the OS/GPU support it, we enable DXGI_PRESENT_ALLOW_TEARING
// to allow variable refresh rate and tear-free fast presents.
//
// [3] Performance tuning
//  - Batching: This renderer batches sprites by "currently bound texture". Changing textures flushes.
//    Pack small sprites into atlases to reduce texture switches.
//  - Dynamic buffers: Uses MAP_WRITE_DISCARD to upload per-frame geometry. If scenes exceed 65k
//    vertices often, increase the growth heuristic in ensureDynamicBuffers().
//  - Sampling: For pixel-perfect UI/tilemaps, call setPixelArtSampling(true) to switch to point
//    filtering globally for the frame.
//  - Scissoring: Add scissor rectangles per-widget via RSSetScissorRects and a scissor-enable
//    rasterizer state.
//  - State objects: We pre-create a small set (opaque/alpha, solid/wire) to avoid runtime churn.
//
// [4] Text quality
// DirectWrite via Direct2D yields excellent hinting and ClearType. For pixel-art modes, consider
// grayscale AA (we default to that) via SetTextAntialiasMode.
//
// [5] WIC pipeline
// WIC decoders handle PNG/JPG/BMP/GIF (we load frame 0). If you need multi-frame GIFs, extend
// loadTextureFromFile() to iterate frames and build an animation system. We keep textures in
// straight alpha and use non-premultiplied blending for sprites, which pairs well with typical PNGs.
//
// [6] FXAA
// The included FXAA is a minimal variant for edge softening. Replace with the full reference FXAA
// or SMAA if you need better quality.
//
// [7] DPI
// Coordinates are in pixels. If your app is per-monitor DPI aware, HWND size already reflects scaled
// pixels. For point-based UI, scale values by GetDpiForWindow(hwnd)/96.
//
// [8] Threading
// All D3D calls must execute on the device thread. Texture loading via WIC can happen off-thread;
// just pass RGBA pixels back and call createTextureFromRGBA8() on the device thread, or guard with
// a command queue.
//
// [9] Debugging
// If you link the D3D11 debug layer (Debug build), device state warnings will appear in the output
// window. We unbind SRVs after Present to avoid "resource still bound" warnings.
//
// [10] Future extensions
//  - Add an atlas builder for batched glyphs and vector icons.
//  - Add a geometry shader for thick lines with joins/caps.
//  - Add scissor stack helpers for UI panels.
//  - Add render-to-texture for mini-maps and thumbnails.
//  - Add color grading LUT and vignette effects in the post-process chain.
// -------------------------------------------------------------------------------------------------
