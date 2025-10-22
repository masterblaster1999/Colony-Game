#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>

namespace proc
{
    inline void ToRGBA8_UNorm(const std::vector<float>& in, std::vector<uint8_t>& outRGBA)
    {
        outRGBA.resize(in.size() * 4);
        for (size_t i = 0; i < in.size(); ++i)
        {
            // Map [-1,1] to [0,255]
            float v = (in[i] * 0.5f + 0.5f);
            v = (v < 0.f ? 0.f : (v > 1.f ? 1.f : v));
            uint8_t u = static_cast<uint8_t>(v * 255.f + 0.5f);
            outRGBA[i*4 + 0] = u;
            outRGBA[i*4 + 1] = u;
            outRGBA[i*4 + 2] = u;
            outRGBA[i*4 + 3] = 255;
        }
    }

    inline Microsoft::WRL::ComPtr<ID3D11Texture2D>
    CreateTexture2DFromRGBA(ID3D11Device* device, int w, int h, const uint8_t* rgba)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w; desc.Height = h;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = rgba;
        init.SysMemPitch = UINT(w * 4);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device->CreateTexture2D(&desc, &init, &tex)))
            return nullptr;

        return tex;
    }
}
