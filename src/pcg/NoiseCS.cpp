#include "NoiseCS.hpp"
#include "Noise.hpp"

namespace pcg {

// This is a CPU fallback. Hook up your D3D11 pipeline to run the HLSL above for speed.
bool generate_fbm_gpu_or_cpu(ID3D11Device* /*dev*/, ID3D11DeviceContext* /*ctx*/,
                             int width, int height, const FbmParamsCS& p,
                             std::vector<float>& out)
{
    out.resize(width*height);
    Perlin per(p.seed);
    float invW = 1.0f / float(width);
    float invH = 1.0f / float(height);
    for (int y=0;y<height;++y) {
        for (int x=0;x<width;++x) {
            float u = x * invW;
            float v = y * invH;
            float val = per.fbm(u / p.scale, v / p.scale, p.octaves, p.lacunarity, p.gain);
            out[y*width + x] = val; // ~[-1,1]
        }
    }
    return false; // false = GPU path not used
}

} // namespace pcg
