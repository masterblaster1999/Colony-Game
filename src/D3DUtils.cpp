#include "D3DUtils.h"
#include <algorithm>

using namespace d3d;

ComPtr<ID3DBlob> d3d::CompileShaderFromFile(
    const std::wstring& path,
    const std::string& entry,
    const std::string& target,
    const D3D_SHADER_MACRO* defines)
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        defines,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(),
        target.c_str(),
        flags,
        0,
        shaderBlob.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (FAILED(hr))
    {
        std::string msg = "Shader compile failed: ";
        if (errorBlob) msg += std::string((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize());
        throw std::runtime_error(msg);
    }
    return shaderBlob;
}

void d3d::CreateFloatTextureUAVSRV(
    ID3D11Device* device,
    uint32_t width,
    uint32_t height,
    ComPtr<ID3D11Texture2D>& tex,
    ComPtr<ID3D11ShaderResourceView>& srv,
    ComPtr<ID3D11UnorderedAccessView>& uav)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    td.CPUAccessFlags = 0;

    HR_CHECK(device->CreateTexture2D(&td, nullptr, tex.GetAddressOf()));

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;
    HR_CHECK(device->CreateShaderResourceView(tex.Get(), &sd, srv.GetAddressOf()));

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = td.Format;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    ud.Texture2D.MipSlice = 0;
    HR_CHECK(device->CreateUnorderedAccessView(tex.Get(), &ud, uav.GetAddressOf()));
}

void d3d::CreateRGBA32FTextureUAVSRV(
    ID3D11Device* device,
    uint32_t width,
    uint32_t height,
    ComPtr<ID3D11Texture2D>& tex,
    ComPtr<ID3D11ShaderResourceView>& srv,
    ComPtr<ID3D11UnorderedAccessView>& uav)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    td.CPUAccessFlags = 0;

    HR_CHECK(device->CreateTexture2D(&td, nullptr, tex.GetAddressOf()));

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;
    HR_CHECK(device->CreateShaderResourceView(tex.Get(), &sd, srv.GetAddressOf()));

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = td.Format;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    ud.Texture2D.MipSlice = 0;
    HR_CHECK(device->CreateUnorderedAccessView(tex.Get(), &ud, uav.GetAddressOf()));
}

void d3d::UpdateFloatTexture(
    ID3D11DeviceContext* ctx,
    ID3D11Texture2D* tex,
    const float* data,
    uint32_t width,
    uint32_t height)
{
    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);

    D3D11_BOX box{};
    box.left = 0; box.right = width;
    box.top = 0;  box.bottom = height;
    box.front = 0; box.back = 1;

    ctx->UpdateSubresource(tex, 0, &box, data, width * sizeof(float), 0);
}

void d3d::ReadbackFloatTexture(
    ID3D11Device* device,
    ID3D11DeviceContext* ctx,
    ID3D11Texture2D* gpuTex,
    uint32_t width,
    uint32_t height,
    std::vector<float>& outCPU)
{
    outCPU.resize(size_t(width) * height);

    D3D11_TEXTURE2D_DESC desc{};
    gpuTex->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    HR_CHECK(device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()));

    ctx->CopyResource(staging.Get(), gpuTex);

    D3D11_MAPPED_SUBRESOURCE ms{};
    HR_CHECK(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms));

    const uint8_t* src = reinterpret_cast<const uint8_t*>(ms.pData);
    for (uint32_t y = 0; y < height; ++y)
    {
        const float* row = reinterpret_cast<const float*>(src + y * ms.RowPitch);
        memcpy(&outCPU[size_t(y) * width], row, width * sizeof(float));
    }

    ctx->Unmap(staging.Get(), 0);
}
