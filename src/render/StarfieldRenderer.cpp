#include "render/StarfieldRenderer.h"
#include <cassert>

using Microsoft::WRL::ComPtr;

namespace colony::render {

static void SafeBindNullIA(ID3D11DeviceContext* ctx) {
    // Unbind vertex buffers & input layout (we use SV_VertexID in the VS)
    ID3D11Buffer* nullVB = nullptr;
    UINT stride = 0, offset = 0;
    ctx->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

bool StarfieldRenderer::loadShaderBlob(const wchar_t* path, ID3DBlob** blobOut) {
    ComPtr<ID3DBlob> blob;
    HRESULT hr = D3DReadFileToBlob(path, blob.GetAddressOf());
    if (FAILED(hr)) return false;
    *blobOut = blob.Detach();
    return true;
}

bool StarfieldRenderer::Initialize(ID3D11Device* device)
{
    assert(device);

    // Load compiled shader objects (built by FXC into ./shaders/)
    ComPtr<ID3DBlob> vsb, psb;
    if (!loadShaderBlob(L"shaders/StarfieldVS.cso", vsb.GetAddressOf())) return false;
    if (!loadShaderBlob(L"shaders/StarfieldPS.cso", psb.GetAddressOf())) return false;

    HRESULT hr = device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_vs.GetAddressOf());
    if (FAILED(hr)) return false;
    hr = device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_ps.GetAddressOf());
    if (FAILED(hr)) return false;

    // Constant buffer (b0)
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth      = sizeof(StarCB);
    cbd.Usage          = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cbd, nullptr, m_cb.GetAddressOf()))) return false;

    // Additive blend state (stars add light)
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable           = TRUE;
    bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend             = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, m_blendAdd.GetAddressOf()))) return false;

    // Disable depth and stencil for this pass
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable   = FALSE;
    dsd.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&dsd, m_dssDisabled.GetAddressOf()))) return false;

    // Rasterizer: cull none (doesn't really matter for a fullscreen triangle)
    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode              = D3D11_FILL_SOLID;
    rsd.CullMode              = D3D11_CULL_NONE;
    rsd.DepthClipEnable       = TRUE;
    rsd.ScissorEnable         = FALSE;
    if (FAILED(device->CreateRasterizerState(&rsd, m_rsCullNone.GetAddressOf()))) return false;

    return true;
}

void StarfieldRenderer::Shutdown()
{
    m_rsCullNone.Reset();
    m_dssDisabled.Reset();
    m_blendAdd.Reset();
    m_cb.Reset();
    m_ps.Reset();
    m_vs.Reset();
}

void StarfieldRenderer::OnResize(uint32_t width, uint32_t height)
{
    m_width  = (width  == 0 ? 1u : width);
    m_height = (height == 0 ? 1u : height);
}

void StarfieldRenderer::Render(ID3D11DeviceContext* ctx, float timeSeconds, float density)
{
    assert(ctx);

    // Update CB
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        StarCB* cb = reinterpret_cast<StarCB*>(mapped.pData);
        cb->invRes[0] = 1.0f / float(m_width);
        cb->invRes[1] = 1.0f / float(m_height);
        cb->time      = timeSeconds;
        cb->density   = density;
        ctx->Unmap(m_cb.Get(), 0);
    }

    // Save minimal state we will override (blend and depth-stencil)
    ComPtr<ID3D11BlendState> prevBlend;
    FLOAT prevBlendFactor[4] = {};
    UINT  prevSampleMask     = 0xFFFFFFFF;
    ctx->OMGetBlendState(prevBlend.GetAddressOf(), prevBlendFactor, &prevSampleMask);

    ComPtr<ID3D11DepthStencilState> prevDSS;
    UINT prevStencilRef = 0;
    ctx->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevStencilRef);

    ComPtr<ID3D11RasterizerState> prevRS;
    ctx->RSGetState(prevRS.GetAddressOf());

    // Bind pipeline
    SafeBindNullIA(ctx);
    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

    ctx->OMSetBlendState(m_blendAdd.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_dssDisabled.Get(), 0);
    ctx->RSSetState(m_rsCullNone.Get());

    // Draw a single fullscreen triangle
    ctx->Draw(3, 0);

    // Restore previous states
    ctx->OMSetBlendState(prevBlend.Get(), prevBlendFactor, prevSampleMask);
    ctx->OMSetDepthStencilState(prevDSS.Get(), prevStencilRef);
    ctx->RSSetState(prevRS.Get());
}

} // namespace colony::render
