#pragma once
#include <FastNoise/FastNoise.h>
#include <vector>

namespace proc
{
    class NoiseGraph {
    public:
        // Construct from a NoiseTool-encoded node graph (Base64 string)
        explicit NoiseGraph(const std::string& encoded)
            : _node(FastNoise::NewFromEncodedNodeTree(encoded.c_str())) {}

        // 2D tileable noise (good for textures)
        void GenTileable2D(std::vector<float>& out, int width, int height, float freq, int seed) const
        {
            out.resize(size_t(width) * size_t(height));
            _node->GenTileable2D(out.data(), width, height, freq, seed);
        }

        // 2D grid (non-tileable)
        void Gen2D(std::vector<float>& out, int x0, int y0, int width, int height, float freq, int seed) const
        {
            out.resize(size_t(width) * size_t(height));
            _node->GenUniformGrid2D(out.data(), x0, y0, width, height, freq, seed);
        }

        // 3D volume (for voxels, density, erosion sims)
        void Gen3D(std::vector<float>& out, int x0, int y0, int z0, int w, int h, int d, float freq, int seed) const
        {
            out.resize(size_t(w) * h * d);
            _node->GenUniformGrid3D(out.data(), x0, y0, z0, w, h, d, freq, seed);
        }

    private:
        FastNoise::SmartNode<> _node;
    };
} // namespace proc
