#include "BlueNoiseLoader.h"
#include <fstream>
#include <cassert>
using Microsoft::WRL::ComPtr;

bool LoadBlueNoiseR8Raw(const std::wstring& path, std::vector<uint8_t>& outBytes, int& w, int& h)
{
    // Expecting a raw 64x64 single-channel file (4096 bytes)
    // You can replace this with your own asset format/loader.
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    outBytes.assign(std::istreambuf_iterator<char>(f), {});
    size_t n = outBytes.size();
    if (n == 4096) { w = 64; h = 64; return true; }
    return false;
}

bool CreateBlueNoiseSRV_R8(
    ID3D11Device* dev,
    const std::vector<uint8_t>& bytes, int w, int h,
    ComPtr<ID3D11ShaderResourceView>& outSRV)
{
    if (bytes.empty() || w <= 0 || h <= 0) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = bytes.data();
    srd.SysMemPitch = (UINT)w;

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = dev->CreateTexture2D(&td, &srd, &tex);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = td.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    hr = dev->CreateShaderResourceView(tex.Get(), &srvd, &outSRV);
    return SUCCEEDED(hr);
}
