#pragma once
#include <vector>
#include <cstdint>
#include "../procgen/ProceduralGraph.hpp"

namespace cg {

struct TerrainVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b, a;
};

struct TerrainMeshData {
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t>      indices;
};

TerrainMeshData BuildTerrainMesh(const pg::Outputs& world,
                                 float xyScale = 1.0f,
                                 float zScale  = 1.0f);

} // namespace cg
