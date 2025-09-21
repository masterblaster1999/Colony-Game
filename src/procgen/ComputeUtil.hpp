#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <cstdint>

namespace gpu {
using Microsoft::WRL::ComPtr;

inline ComPtr<ID3DBlob> LoadCSO(const std::wstring& path) {
    ComPtr<ID3DBlob> blob;
    HRESULT hr = D3DReadFileToBlob(path.c_str(), &blob);
    return SUCCEEDED(hr) ? blob : nullptr;
}
inline ComPtr<ID3D11ComputeShader> CreateCS(ID3D11Device* dev, ID3DBlob* blob){
    ComPtr<ID3D11ComputeShader> cs;
    HRESULT hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs);
    return SUCCEEDED(hr) ? cs : nullptr;
}

struct Tex2D {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};

inline Tex2D CreateTex2D_UAVSRV(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt){
    Tex2D t{};
    D3D11_TEXTURE2D_DESC td{}; td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
    td.Format=fmt; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    dev->CreateTexture2D(&td, nullptr, &t.tex);
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=fmt; u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    dev->CreateUnorderedAccessView(t.tex.Get(), &u, &t.uav);
    D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=fmt; s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels=1;
    dev->CreateShaderResourceView(t.tex.Get(), &s, &t.srv);
    return t;
}
} // namespace gpu
