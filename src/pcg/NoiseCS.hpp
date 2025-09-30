#pragma once
#include <cstdint>
#include <vector>

// Forward declare D3D11 if you have it; otherwise this stays as a stub.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace pcg {

struct FbmParamsCS {
    float scale = 0.004f;
    int   octaves = 5;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float z = 0.0f;
    uint32_t seed = 1;
};

// Returns false if GPU path unavailable; fills data via CPU fallback either way.
bool generate_fbm_gpu_or_cpu(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                             int width, int height, const FbmParamsCS& p,
                             std::vector<float>& out);

} // namespace pcg
