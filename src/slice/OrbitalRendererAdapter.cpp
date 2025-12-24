// src/slice/OrbitalRendererAdapter.cpp

#include "OrbitalRendererAdapter.h"

#include "SliceSimulation.h"

#include <cassert>

namespace slice {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { assert(false); ExitProcess((UINT)_hr); } } while(0)

void OrbitalRendererAdapter::create(ID3D11Device* dev) {
    // Blend (alpha) - matches the previous monolithic renderer.
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
        HR(dev->CreateBlendState(&bd, blendAlpha_.GetAddressOf()));
    }

    if (!orender_.Initialize(dev, L"res\\shaders")) {
        assert(false);
        ExitProcess(1);
    }
}

void OrbitalRendererAdapter::reload(ID3D11Device* dev) {
    orender_.Shutdown();
    if (!orender_.Initialize(dev, L"res\\shaders")) {
        assert(false);
        ExitProcess(1);
    }
}

void OrbitalRendererAdapter::draw(ID3D11DeviceContext* ctx, const SliceSimulation& sim, const XMMATRIX& V, const XMMATRIX& P) {
    // Lift system to avoid ground intersection; if FreeCam, keep as-is
    XMMATRIX V_orb = XMMatrixTranslation(0.0f, -6.0f, 0.0f) * V;

    float blendFactor[4] = { 0,0,0,0 };
    ctx->OMSetBlendState(sim.orbitBlend ? blendAlpha_.Get() : nullptr, blendFactor, 0xFFFFFFFF);
    orender_.Render(ctx, sim.orbital, V_orb, P, sim.orbOpts);
    ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

} // namespace slice
