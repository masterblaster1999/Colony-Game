#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <string>

bool LoadBlueNoiseR8Raw(const std::wstring& path, std::vector<uint8_t>& outBytes, int& w, int& h);

bool CreateBlueNoiseSRV_R8(
    ID3D11Device* dev,
    const std::vector<uint8_t>& bytes, int w, int h,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV);
