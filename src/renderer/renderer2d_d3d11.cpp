// src/renderer/renderer2d_d3d11.cpp
#include "renderer2d_d3d11.h"
#include "render/d3d11_device.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <fstream>
#include <cstdint>
#include <memory>
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace gfx {

struct Vertex {
    float x, y;
    float r, g, b, a;
};

static bool LoadFileBytes(const wchar_t* path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.seekg(0, std::ios::end);
    const std::streamoff endPos = f.tellg();
    if (endPos <= 0) return false;

    const size_t sz = static_cast<size_t>(endPos);
    f.seekg(0, std::ios::beg);

    out.resize(sz);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), sz));
}

// NOTE: keep this a 'struct' to match the header forward-declaration and avoid C4099.
struct Renderer2D_D3D11::Impl {
public:
    explicit Impl(render::D3D11Device& dev) : m_dev(dev) {}

    bool init()
    {
        auto* d = m_dev.device();
        if (!d) return false;

        // Load VS/PS bytecode compiled by CMake/VS into res/shaders
        // (Match your CMake outdir; VS path uses "$(OutDir)res\\shaders")
        std::vector<uint8_t> vs, ps;
        if (!LoadFileBytes(L"res/shaders/Batch2D_vs.cso", vs)) return false;
        if (!LoadFileBytes(L"res/shaders/Batch2D_ps.cso", ps)) return false;

        if (FAILED(d->CreateVertexShader(vs.data(), vs.size(), nullptr, m_vs.GetAddressOf()))) return false;
        if (FAILED(d->CreatePixelShader(ps.data(), ps.size(), nullptr, m_ps.GetAddressOf()))) return false;

        // Input layout
        D3D11_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,         0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,   0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(d->CreateInputLayout(il, 2, vs.data(), vs.size(), m_il.GetAddressOf()))) return false;

        // Dynamic vertex buffer (grow on demand)
        if (!createVB(1024u)) return false;

        // Blend state (alpha blending)
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable           = TRUE;
        bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(d->CreateBlendState(&bd, m_blend.GetAddressOf()))) return false;

        // Rasterizer: no cull, solid
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode      = D3D11_FILL_SOLID;
        rs.CullMode      = D3D11_CULL_NONE;
        rs.ScissorEnable = FALSE;
        if (FAILED(d->CreateRasterizerState(&rs, m_rast.GetAddressOf()))) return false;

        // Sampler: linear clamp (for future textured quads)
        D3D11_SAMPLER_DESC sd{};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(d->CreateSamplerState(&sd, m_samp.GetAddressOf()))) return false;

        return true;
    }

    void resize(uint32_t /*w*/, uint32_t /*h*/)
    {
        // Optional: set/update an orthographic matrix CBuffer here
    }

    void begin()
    {
        m_cpu.clear();
    }

    void rect(float x, float y, float w, float h, Color c)
    {
        const float r = c.r, g = c.g, b = c.b, a = c.a;
        const float x0 = x,     y0 = y;
        const float x1 = x + w, y1 = y + h;

        // two triangles (0,1,2) (2,1,3)
        m_cpu.push_back(Vertex{ x0, y0, r,g,b,a });
        m_cpu.push_back(Vertex{ x1, y0, r,g,b,a });
        m_cpu.push_back(Vertex{ x0, y1, r,g,b,a });

        m_cpu.push_back(Vertex{ x0, y1, r,g,b,a });
        m_cpu.push_back(Vertex{ x1, y0, r,g,b,a });
        m_cpu.push_back(Vertex{ x1, y1, r,g,b,a });
    }

    void end()
    {
        if (m_cpu.empty()) return;

        auto* d  = m_dev.device();
        auto* dc = m_dev.context();
        if (!d || !dc) return;

        // Grow VB if needed
        const UINT bytesNeeded = static_cast<UINT>(m_cpu.size() * sizeof(Vertex));
        if (bytesNeeded > m_vbSize) {
            const UINT newSize = std::max<UINT>(bytesNeeded, m_vbSize ? (m_vbSize * 2u) : bytesNeeded);
            if (!createVB(newSize)) return;
        }

        // Update VB
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(dc->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            std::memcpy(map.pData, m_cpu.data(), bytesNeeded);
            dc->Unmap(m_vb.Get(), 0);
        } else {
            return;
        }

        // Bind pipeline
        UINT stride = sizeof(Vertex), offset = 0;
        ID3D11Buffer* vbs[] = { m_vb.Get() };
        dc->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->IASetInputLayout(m_il.Get());
        dc->VSSetShader(m_vs.Get(), nullptr, 0);
        dc->PSSetShader(m_ps.Get(), nullptr, 0);
        dc->PSSetSamplers(0, 1, m_samp.GetAddressOf());
        dc->OMSetBlendState(m_blend.Get(), nullptr, 0xFFFFFFFF);
        dc->RSSetState(m_rast.Get());

        // Draw
        dc->Draw(static_cast<UINT>(m_cpu.size()), 0);

        // Leave state changes minimal; the caller’s frame code is simple
    }

private:
    bool createVB(UINT sizeBytes)
    {
        auto* d = m_dev.device();
        if (!d) return false;

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = sizeBytes;
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ComPtr<ID3D11Buffer> vb;
        if (FAILED(d->CreateBuffer(&bd, nullptr, vb.GetAddressOf()))) return false;

        m_vb     = std::move(vb);
        m_vbSize = sizeBytes;
        return true;
    }

private:
    render::D3D11Device&            m_dev;

    ComPtr<ID3D11VertexShader>      m_vs;
    ComPtr<ID3D11PixelShader>       m_ps;
    ComPtr<ID3D11InputLayout>       m_il;
    ComPtr<ID3D11Buffer>            m_vb;
    ComPtr<ID3D11BlendState>        m_blend;
    ComPtr<ID3D11RasterizerState>   m_rast;
    ComPtr<ID3D11SamplerState>      m_samp;

    UINT                            m_vbSize = 0;
    std::vector<Vertex>             m_cpu;
};

// ===== Public wrappers =====

Renderer2D_D3D11::Renderer2D_D3D11(render::D3D11Device& dev)
    : m_dev(dev)
{
    m_impl = std::make_unique<Impl>(dev);
    (void)m_impl->init(); // in production, bubble this up
}

Renderer2D_D3D11::~Renderer2D_D3D11() = default;

void Renderer2D_D3D11::Resize(uint32_t w, uint32_t h) { m_impl->resize(w, h); }
void Renderer2D_D3D11::Begin()                       { m_impl->begin();       }
void Renderer2D_D3D11::DrawFilledRect(float x,float y,float w,float h, Color c) {
    m_impl->rect(x, y, w, h, c); // uses parameters → no C4100
}
void Renderer2D_D3D11::End()                         { m_impl->end();         }

std::unique_ptr<IRenderer2D> CreateRenderer2D_D3D11(render::D3D11Device& d) {
    return std::make_unique<Renderer2D_D3D11>(d);
}

} // namespace gfx
