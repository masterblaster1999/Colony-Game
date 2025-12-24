// src/slice/TerrainRendererD3D11.cpp

#include "TerrainRendererD3D11.h"

#include "SliceSimulation.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace slice {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const wchar_t* kTerrainVS = L"res/shaders/Slice_TerrainVS.hlsl";
static const wchar_t* kTerrainPS = L"res/shaders/Slice_TerrainPS.hlsl";
static const wchar_t* kColorVS   = L"res/shaders/Slice_ColorVS.hlsl";
static const wchar_t* kColorPS   = L"res/shaders/Slice_ColorPS.hlsl";

#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

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
    TerrainRendererD3D11::Mesh makeGrid(ID3D11Device* dev, int N, float tileWorld) {
        std::vector<TerrainRendererD3D11::Vtx> v; v.reserve(size_t(N) * N);
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

        TerrainRendererD3D11::Mesh m{};
        m.indexCount = to_uint_checked(idx.size());

        // VB
        {
            const size_t vbBytes = v.size() * sizeof(TerrainRendererD3D11::Vtx);
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

    TerrainRendererD3D11::Mesh makeCube(ID3D11Device* dev, float s) {
        const float h = s * 0.5f;
        TerrainRendererD3D11::VtxN verts[] = {
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

        TerrainRendererD3D11::Mesh m{};
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

void TerrainRendererD3D11::HeightTexture::create(ID3D11Device* dev, const std::vector<float>& h, int w, int hgt) {
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

void TerrainRendererD3D11::create(ID3D11Device* dev, ID3D11DeviceContext* /*ctx*/, const SliceSimulation& sim) {
    // Heightmap + grid
    regenerateHeight(dev, sim);
    grid_ = makeGrid(dev, sim.HM, sim.TileWorld);

    // Terrain pipeline
    auto vsb = Compile(kTerrainVS, "main", "vs_5_0");
    auto psb = Compile(kTerrainPS, "main", "ps_5_0");
    HR(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, terrainVS_.GetAddressOf()));
    HR(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, terrainPS_.GetAddressOf()));

    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0 ,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT   ,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    HR(dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), terrainIL_.GetAddressOf()));

    // Cube pipeline
    auto vsb2 = Compile(kColorVS, "main", "vs_5_0");
    auto psb2 = Compile(kColorPS, "main", "ps_5_0");
    HR(dev->CreateVertexShader(vsb2->GetBufferPointer(), vsb2->GetBufferSize(), nullptr, colorVS_.GetAddressOf()));
    HR(dev->CreatePixelShader(psb2->GetBufferPointer(), psb2->GetBufferSize(), nullptr, colorPS_.GetAddressOf()));

    D3D11_INPUT_ELEMENT_DESC il2[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0 ,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL"  ,0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    HR(dev->CreateInputLayout(il2, 2, vsb2->GetBufferPointer(), vsb2->GetBufferSize(), colorIL_.GetAddressOf()));

    // CBuffers (align ByteWidth to 16 as required)
    D3D11_BUFFER_DESC cbd{};
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cbd.ByteWidth = align16_uint_size(sizeof(CameraCB));
    HR(dev->CreateBuffer(&cbd, nullptr, cbCamera_.GetAddressOf()));

    cbd.ByteWidth = align16_uint_size(sizeof(CameraCB));
    HR(dev->CreateBuffer(&cbd, nullptr, cbCameraCube_.GetAddressOf()));

    D3D11_BUFFER_DESC cbd2 = cbd;
    cbd2.ByteWidth = align16_uint_size(sizeof(TerrainCB));
    HR(dev->CreateBuffer(&cbd2, nullptr, cbTerrain_.GetAddressOf()));

    D3D11_BUFFER_DESC cbd3 = cbd;
    cbd3.ByteWidth = align16_uint_size(sizeof(ColorCB));
    HR(dev->CreateBuffer(&cbd3, nullptr, cbColor_.GetAddressOf()));

    // Sampler
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    HR(dev->CreateSamplerState(&sd, sampLinear_.GetAddressOf()));

    // Cube mesh
    cube_ = makeCube(dev, 0.5f);
}

void TerrainRendererD3D11::regenerateHeight(ID3D11Device* dev, const SliceSimulation& sim) {
    std::vector<float> hm;
    makeHeightmap(hm, sim.HM, sim.HM, sim.seed, sim.hmScale, sim.hmOctaves, sim.hmPersistence);
    heightTex_.create(dev, hm, sim.HM, sim.HM);
}

void TerrainRendererD3D11::drawTerrain(ID3D11DeviceContext* ctx, const SliceSimulation& sim,
                                      const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P) {
    CameraCB cam{};
    XMStoreFloat4x4(&cam.World, XMMatrixIdentity());
    XMStoreFloat4x4(&cam.View, V);
    XMStoreFloat4x4(&cam.Proj, P);
    cam.HeightAmplitude = sim.HeightAmp;
    cam.HeightTexel = XMFLOAT2(1.f / sim.HM, 1.f / sim.HM);
    cam.TileWorld = sim.TileWorld;
    UpdateCB(ctx, cbCamera_.Get(), cam);

    TerrainCB tcb{};
    tcb.LightDir = sim.lightDir;
    tcb.BaseColor = XMFLOAT3(0.32f, 0.58f, 0.32f);
    tcb.HeightScale = sim.HeightAmp / sim.TileWorld;
    tcb.HeightTexel = XMFLOAT2(1.f / sim.HM, 1.f / sim.HM);
    UpdateCB(ctx, cbTerrain_.Get(), tcb);

    const UINT stride = static_cast<UINT>(sizeof(Vtx));
    const UINT offs = 0;
    ctx->IASetVertexBuffers(0, 1, grid_.vbo.GetAddressOf(), &stride, &offs);
    ctx->IASetIndexBuffer(grid_.ibo.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(terrainIL_.Get());

    ctx->VSSetShader(terrainVS_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cbCamera_.GetAddressOf());

    ID3D11ShaderResourceView* srvs[1] = { heightTex_.srv.Get() };
    ctx->VSSetShaderResources(0, 1, srvs);
    ctx->VSSetSamplers(0, 1, sampLinear_.GetAddressOf());

    ctx->PSSetShader(terrainPS_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cbTerrain_.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, srvs);
    ctx->PSSetSamplers(0, 1, sampLinear_.GetAddressOf());

    ctx->DrawIndexed(grid_.indexCount, 0, 0);
}

void TerrainRendererD3D11::drawCube(ID3D11DeviceContext* ctx, const SliceSimulation& sim,
                                   const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P) {
    if (!sim.drawCube) return;

    CameraCB camCube{};
    XMStoreFloat4x4(&camCube.World, XMMatrixTranslation(0, 0.5f, 0));
    XMStoreFloat4x4(&camCube.View, V);
    XMStoreFloat4x4(&camCube.Proj, P);
    camCube.HeightAmplitude = sim.HeightAmp;
    camCube.HeightTexel = XMFLOAT2(1.f / sim.HM, 1.f / sim.HM);
    camCube.TileWorld = sim.TileWorld;
    UpdateCB(ctx, cbCameraCube_.Get(), camCube);

    ColorCB ccb{};
    ccb.LightDir = sim.lightDir;
    ccb.Albedo = XMFLOAT3(0.7f, 0.2f, 0.2f);
    UpdateCB(ctx, cbColor_.Get(), ccb);

    const UINT stride2 = static_cast<UINT>(sizeof(VtxN));
    const UINT offs2 = 0;
    ctx->IASetVertexBuffers(0, 1, cube_.vbo.GetAddressOf(), &stride2, &offs2);
    ctx->IASetIndexBuffer(cube_.ibo.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->IASetInputLayout(colorIL_.Get());

    ctx->VSSetShader(colorVS_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cbCameraCube_.GetAddressOf());
    ctx->PSSetShader(colorPS_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cbColor_.GetAddressOf());

    ctx->DrawIndexed(cube_.indexCount, 0, 0);
}

} // namespace slice
