#include "Textures.h"
#include "HrCheck.h"

using Microsoft::WRL::ComPtr;

namespace render
{
    Texture2D CreateRWTexture2D(ID3D11Device* dev,
                                uint32_t width, uint32_t height,
                                DXGI_FORMAT fmt)
    {
        Texture2D t{};
        t.width = width; t.height = height;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

        HR_CHECK(dev->CreateTexture2D(&td, nullptr, t.tex.ReleaseAndGetAddressOf()));

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
        uavd.Format = td.Format;
        uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        HR_CHECK(dev->CreateUnorderedAccessView(t.tex.Get(), &uavd, t.uav.ReleaseAndGetAddressOf()));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MostDetailedMip = 0;
        srvd.Texture2D.MipLevels = 1;
        HR_CHECK(dev->CreateShaderResourceView(t.tex.Get(), &srvd, t.srv.ReleaseAndGetAddressOf()));

        return t;
    }

    ComPtr<ID3D11Buffer> CreateConstantBuffer(ID3D11Device* dev, size_t byteSize)
    {
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = static_cast<UINT>((byteSize + 15) & ~15u); // 16-byte aligned
        ComPtr<ID3D11Buffer> cb;
        HR_CHECK(dev->CreateBuffer(&bd, nullptr, cb.ReleaseAndGetAddressOf()));
        return cb;
    }
}
