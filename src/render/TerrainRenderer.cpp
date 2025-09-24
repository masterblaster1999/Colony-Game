#include "TerrainRenderer.hpp"
#include <d3dcompiler.h>
#include <vector>
#include <cstring>

using namespace DirectX;

namespace cg {

struct CBTransform {
    XMFLOAT4X4 mvp;
    XMFLOAT4   lightDir; // xyz used
};

bool TerrainRenderer::compileShaders(const wchar_t* hlslPath) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    Microsoft::WRL::ComPtr<ID3DBlob> vsb, psb, err;

    HRESULT hr = D3DCompileFromFile(hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    "VSMain", "vs_5_0", flags, 0, vsb.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) return false;
    err.Reset();

    hr = D3DCompileFromFile(hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "PSMain", "ps_5_0", flags, 0, psb.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) return false;

    if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vs_.GetAddressOf())))
        return false;
    if (FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, ps_.GetAddressOf())))
        return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,                           D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateInputLayout(layout, _countof(layout),
                      vsb->GetBufferPointer(), vsb->GetBufferSize(), il_.GetAddressOf())))
        return false;

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CBTransform);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&cbd, nullptr, cb_.GetAddressOf())))
        return false;

    return true;
}

bool TerrainRenderer::initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, const wchar_t* hlslPath) {
    device_  = dev;
    context_ = ctx;
    return compileShaders(hlslPath);
}

void TerrainRenderer::upload(const TerrainMeshData& mesh) {
    indexCount_ = static_cast<UINT>(mesh.indices.size());

    // VB
    D3D11_BUFFER_DESC vbd = {};
    vbd.ByteWidth      = UINT(mesh.vertices.size() * sizeof(TerrainVertex));
    vbd.Usage          = D3D11_USAGE_DEFAULT;
    vbd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = { mesh.vertices.data(), 0, 0 };
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb;
    if (FAILED(device_->CreateBuffer(&vbd, &vinit, vb.GetAddressOf()))) return;
    vb_ = vb;

    // IB
    D3D11_BUFFER_DESC ibd = {};
    ibd.ByteWidth      = UINT(mesh.indices.size() * sizeof(uint32_t));
    ibd.Usage          = D3D11_USAGE_DEFAULT;
    ibd.BindFlags      = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = { mesh.indices.data(), 0, 0 };
    Microsoft::WRL::ComPtr<ID3D11Buffer> ib;
    if (FAILED(device_->CreateBuffer(&ibd, &iinit, ib.GetAddressOf()))) return;
    ib_ = ib;
}

void TerrainRenderer::render(const XMMATRIX& mvp, const float lightDir[3]) {
    if (!vb_ || !ib_) return;

    UINT stride = sizeof(TerrainVertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, vb_.GetAddressOf(), &stride, &offset);
    context_->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
    context_->IASetInputLayout(il_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    CBTransform cb;
    XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(mvp));
    cb.lightDir = XMFLOAT4(lightDir[0], lightDir[1], lightDir[2], 0.0f);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        context_->Unmap(cb_.Get(), 0);
    }

    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    context_->PSSetShader(ps_.Get(), nullptr, 0);

    context_->DrawIndexed(indexCount_, 0, 0);
}

} // namespace cg
