// Procgen3DPreview.cpp  --  single-file 3D terrain preview (Windows / D3D11)
// Build: add this file to your Windows target. Requires d3d11.lib and d3dcompiler.lib.
// Ship D3DCompiler_47.dll application-local if you rely on runtime shader compilation.
// (Microsoft recommends app-local deployment for this DLL.)  See docs for details.
//
// Public entry point:
//   void CG_DrawProcgen3DPreview(ID3D11Device* dev, ID3D11DeviceContext* ctx, float timeSeconds, bool wireframe);

#ifndef _WIN32
#  error "Procgen3DPreview.cpp is Windows-only and should not be built on non-Windows platforms."
#endif

// Prefer the project's unified Windows header if available; otherwise fall back
// to a minimal Windows.h include with the usual macro hygiene. Guard the
// WIN32_LEAN_AND_MEAN define so we don't trigger C4005 when the macro is also
// supplied via the compiler command line (/DWIN32_LEAN_AND_MEAN). :contentReference[oaicite:0]{index=0}
#ifdef __has_include
#  if __has_include("platform/win/WinHeaders.h")
#    include "platform/win/WinHeaders.h"
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <Windows.h>
#  endif
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring> // for strlen in CompileShaderFromSrc

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT3 nrm;
};

struct CBGlobals {
    XMMATRIX mvp;          // column-major by default for HLSL (row-major here, we'll transpose)
    XMFLOAT3 lightDir;
    float _pad0;
    XMFLOAT4 albedo;
};

// Simple Perlin-style hash table (repeat=256)
static const int PSize = 256;
static int perm[512];
static bool permInit = false;

inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline float lerp(float a, float b, float t) { return a + t * (b - a); }
inline float grad(int h, float x, float y) {
    // 8 gradient directions
    const float u = (h & 1) ? x : -x;
    const float v = (h & 2) ? y : -y;
    return u + v;
}

inline float perlin2(float x, float y) {
    int xi = (int)std::floor(x) & 255;
    int yi = (int)std::floor(y) & 255;
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = fade(xf), v = fade(yf);

    int aa = perm[perm[xi] + yi]     & 7;
    int ab = perm[perm[xi] + yi + 1] & 7;
    int ba = perm[perm[xi + 1] + yi] & 7;
    int bb = perm[perm[xi + 1] + yi + 1] & 7;

    float x1 = lerp(grad(aa, xf,     yf),     grad(ba, xf - 1.0f, yf),     u);
    float x2 = lerp(grad(ab, xf, yf - 1.0f),  grad(bb, xf - 1.0f, yf - 1), u);
    return lerp(x1, x2, v);
}

inline float fbm2(float x, float y, int octaves=5, float lacunarity=2.0f, float gain=0.5f) {
    float amp = 0.5f, sum = 0.0f, freq = 1.0f;
    for (int i=0; i<octaves; ++i) {
        sum += amp * perlin2(x * freq, y * freq);
        freq *= lacunarity;
        amp  *= gain;
    }
    return sum;
}

void initPermutation(uint32_t seed) {
    if (permInit) return;
    std::vector<int> p(PSize);
    for (int i=0;i<PSize;++i) p[i] = i;
    // very small LCG shuffle (deterministic)
    uint32_t s = seed ? seed : 0xdeadbeefu;
    for (int i=PSize-1;i>0;--i) {
        s = 1664525u * s + 1013904223u;
        int j = s % (i+1);
        std::swap(p[i], p[j]);
    }
    for (int i=0;i<512;++i) perm[i] = p[i % PSize];
    permInit = true;
}

// Compile a shader from memory source
HRESULT CompileShaderFromSrc(const char* src, const char* entry, const char* profile, UINT flags, ComPtr<ID3DBlob>& blob) {
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompile(
        src,
        static_cast<SIZE_T>(std::strlen(src)),
        nullptr,
        nullptr,
        nullptr,
        entry,
        profile,
        flags,
        0,
        blob.GetAddressOf(),
        err.GetAddressOf());
    if (FAILED(hr) && err) {
        OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
    }
    return hr;
}

// Minimal inline HLSL (VS/PS)
static const char* kHlsl = R"(
cbuffer Globals : register(b0) {
    float4x4 g_mvp;
    float3   g_lightDir;
    float    _pad0;
    float4   g_albedo;
};

struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; };

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = mul(g_mvp, float4(i.pos, 1));
    o.nrm = normalize(i.nrm);
    return o;
}

float4 ps_main(VSOut i) : SV_Target {
    float3 n = normalize(i.nrm);
    float ndl = saturate(dot(n, -normalize(g_lightDir)));
    float3 col = g_albedo.rgb * (0.20 + 0.80 * ndl);
    return float4(col, 1);
}
)";

struct State {
    // Resources (persist across calls)
    ComPtr<ID3D11Buffer> vb, ib, cb;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader>  ps;
    ComPtr<ID3D11InputLayout>  il;
    ComPtr<ID3D11DepthStencilState> dss;
    ComPtr<ID3D11RasterizerState> rs_solid, rs_wire;
    UINT indexCount = 0;
    int grid = 257;           // 257x257 verts -> 256x256 quads
    float scaleXZ = 1.0f;     // world units per grid cell
    float amp = 25.0f;        // height amplitude
} g;

void buildMesh(ID3D11Device* dev, int N, float scaleXZ, float amp, float time) {
    g.grid = N; g.scaleXZ = scaleXZ; g.amp = amp;
    const int W = N, H = N;
    const int verts = W * H;
    std::vector<Vertex> v(verts);

    // animate noise with time (slow drift)
    const float baseFreq = 1.0f / 64.0f;
    const float tShift = time * 0.05f;

    for (int z=0; z<H; ++z) {
        for (int x=0; x<W; ++x) {
            float fx = (float)x;
            float fz = (float)z;
            float h = fbm2((fx+tShift)*baseFreq, (fz)*baseFreq, 5, 2.0f, 0.5f);
            h *= amp;

            Vertex& vv = v[z*W + x];
            vv.pos = XMFLOAT3((fx - (W-1)*0.5f)*scaleXZ, h, (fz - (H-1)*0.5f)*scaleXZ);
            // normal from central differences
            auto heightAt = [&](int ix, int iz)->float{
                ix = std::clamp(ix, 0, W-1); iz = std::clamp(iz, 0, H-1);
                float hx = fbm2(((float)ix+tShift)*baseFreq, ((float)iz)*baseFreq, 5, 2.0f, 0.5f);
                return hx * amp;
            };
            float hL = heightAt(x-1,z), hR = heightAt(x+1,z), hD = heightAt(x,z-1), hU = heightAt(x,z+1);
            XMFLOAT3 n( hL - hR, 2.0f, hD - hU );
            XMVECTOR nv = XMVector3Normalize(XMLoadFloat3(&n));
            XMStoreFloat3(&vv.nrm, nv);
        }
    }

    std::vector<uint32_t> idx;
    idx.reserve((W-1)*(H-1)*6);
    for (int z=0; z<H-1; ++z) {
        for (int x=0; x<W-1; ++x) {
            uint32_t i0 = z*W + x;
            uint32_t i1 = z*W + (x+1);
            uint32_t i2 = (z+1)*W + x;
            uint32_t i3 = (z+1)*W + (x+1);
            // two triangles
            idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
            idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
        }
    }
    g.indexCount = (UINT)idx.size();

    // Upload to GPU
    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.ByteWidth = (UINT)(v.size() * sizeof(Vertex));
    D3D11_SUBRESOURCE_DATA vbInit{ v.data(), 0, 0 };
    g.vb.Reset();
    dev->CreateBuffer(&vbDesc, &vbInit, g.vb.GetAddressOf());

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibDesc.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
    D3D11_SUBRESOURCE_DATA ibInit{ idx.data(), 0, 0 };
    g.ib.Reset();
    dev->CreateBuffer(&ibDesc, &ibInit, g.ib.GetAddressOf());
}

void ensurePipeline(ID3D11Device* dev) {
    if (g.vs) return;

    // Compile shaders
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vsb, psb;
    CompileShaderFromSrc(kHlsl, "vs_main", "vs_5_0", flags, vsb);
    CompileShaderFromSrc(kHlsl, "ps_main", "ps_5_0", flags, psb);

    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, g.vs.GetAddressOf());
    dev->CreatePixelShader (psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, g.ps.GetAddressOf());

    // Input layout
    D3D11_INPUT_ELEMENT_DESC ied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(Vertex,pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(Vertex,nrm), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    dev->CreateInputLayout(ied, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), g.il.GetAddressOf());

    // Constant buffer
    D3D11_BUFFER_DESC cbd{};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbd.ByteWidth = sizeof(CBGlobals) + (16 - (sizeof(CBGlobals)%16))%16; // 16-byte align
    dev->CreateBuffer(&cbd, nullptr, g.cb.GetAddressOf());

    // Depth (state)
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateDepthStencilState(&dsd, g.dss.GetAddressOf());

    // Rasterizer states
    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode = D3D11_FILL_SOLID;
    rsd.CullMode = D3D11_CULL_BACK;
    rsd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rsd, g.rs_solid.GetAddressOf());

    rsd.FillMode = D3D11_FILL_WIREFRAME;
    rsd.CullMode = D3D11_CULL_NONE;
    dev->CreateRasterizerState(&rsd, g.rs_wire.GetAddressOf());

    // Initial mesh
    initPermutation(1337);
}

ComPtr<ID3D11Texture2D> ensureDepth(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ComPtr<ID3D11DepthStencilView>& dsvOut) {
    // If a DSV is already bound, keep it; else make a transient one matching the RT.
    ComPtr<ID3D11DepthStencilView> curDSV;
    ComPtr<ID3D11RenderTargetView> curRTV;
    ctx->OMGetRenderTargets(1, curRTV.GetAddressOf(), curDSV.GetAddressOf());
    if (curDSV) { dsvOut = curDSV; return nullptr; }

    ComPtr<ID3D11Resource> res;
    rtv->GetResource(res.GetAddressOf());
    ComPtr<ID3D11Texture2D> rtTex;
    res.As(&rtTex);
    D3D11_TEXTURE2D_DESC rtDesc{};
    rtTex->GetDesc(&rtDesc);

    D3D11_TEXTURE2D_DESC dsDesc{};
    dsDesc.Width = rtDesc.Width;
    dsDesc.Height = rtDesc.Height;
    dsDesc.MipLevels = 1;
    dsDesc.ArraySize = 1;
    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.SampleDesc = {1,0};
    dsDesc.Usage = D3D11_USAGE_DEFAULT;
    dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthTex;
    dev->CreateTexture2D(&dsDesc, nullptr, depthTex.GetAddressOf());
    dev->CreateDepthStencilView(depthTex.Get(), nullptr, dsvOut.GetAddressOf());
    return depthTex; // return to keep alive in caller scope if transient
}

} // anon

void CG_DrawProcgen3DPreview(ID3D11Device* dev, ID3D11DeviceContext* ctx, float timeSeconds, bool wireframe)
{
    if (!dev || !ctx) return;
    ensurePipeline(dev);

    // Save bound RT/DS + viewport to restore after drawing
    ComPtr<ID3D11RenderTargetView> oldRTV;
    ComPtr<ID3D11DepthStencilView> oldDSV;
    ctx->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT vpCount = 0; ctx->RSGetViewports(&vpCount, nullptr);
    std::vector<D3D11_VIEWPORT> oldVP(vpCount ? vpCount : 1);
    if (vpCount) ctx->RSGetViewports(&vpCount, oldVP.data());

    if (!oldRTV) return; // need a render target

    ComPtr<ID3D11DepthStencilView> dsv;
    auto keepAliveDepth = ensureDepth(dev, ctx, oldRTV.Get(), dsv);

    // Maybe rebuild mesh every ~1s for animated terrain (cheap)
    static float lastBuildT = -999.0f;
    if (timeSeconds - lastBuildT > 1.0f || !g.vb) {
        buildMesh(dev, /*N=*/257, /*scaleXZ=*/2.0f, /*amp=*/25.0f, timeSeconds);
        lastBuildT = timeSeconds;
    }

    // Camera (simple orbit)
    const float dist = g.grid * g.scaleXZ * 1.6f;
    XMVECTOR eye = XMVectorSet(0.0f + std::cos(timeSeconds*0.2f)*dist, g.amp*2.0f, -dist + std::sin(timeSeconds*0.2f)*dist, 0);
    XMVECTOR at  = XMVectorSet(0, 0, 0, 0);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    // Viewport from current RT
    D3D11_VIEWPORT vp{};
    if (vpCount) vp = oldVP[0];
    else { vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=1600.0f; vp.Height=900.0f; }
    float aspect = vp.Width / std::max(1.0f, vp.Height);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 10000.0f);
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX mvpT = XMMatrixTranspose(world * view * proj);

    // Upload CB
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(ctx->Map(g.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        CBGlobals* cb = reinterpret_cast<CBGlobals*>(map.pData);
        cb->mvp = mvpT;
        cb->lightDir = XMFLOAT3(-0.3f, -1.0f, -0.2f);
        cb->albedo   = XMFLOAT4(0.35f, 0.70f, 0.30f, 1.0f);
        ctx->Unmap(g.cb.Get(), 0);
    }

    // Bind pipeline and draw
    UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetInputLayout(g.il.Get());
    ctx->IASetVertexBuffers(0, 1, g.vb.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(g.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g.vs.Get(), nullptr, 0);
    ctx->PSSetShader(g.ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, g.cb.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, g.cb.GetAddressOf());
    ctx->OMSetDepthStencilState(g.dss.Get(), 0);
    ctx->RSSetState(wireframe ? g.rs_wire.Get() : g.rs_solid.Get());
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, oldRTV.GetAddressOf(), dsv.Get());

    ctx->DrawIndexed(g.indexCount, 0, 0);

    // Restore state that’s intrusive
    if (vpCount) ctx->RSSetViewports(vpCount, oldVP.data());
    ctx->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
}
