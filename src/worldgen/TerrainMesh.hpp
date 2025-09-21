#pragma once
#include <vector>
#include <cstdint>
#include "WorldGen.hpp" // for colony::worldgen::WorldChunk

namespace colony::worldgen {

// Interleaved vertex used by the mesh builder.
// Color is stored as packed RGBA8 (0xRRGGBBAA); adjust in your renderer if needed.
struct TerrainVertex {
    float px, py, pz;           // position (meters)  (X, Y-up, Z)
    float nx, ny, nz;           // normal
    std::uint32_t rgba;         // vertex color (biome-tinted)
    float u, v;                 // UVs (0..1 across the chunk)
};

struct MeshData {
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Build parameters for terrain meshing.
struct TerrainMeshParams {
    float cellSizeMeters = 1.0f; // spacing between height samples (should match GeneratorSettings)
    float heightScale    = 50.0f; // scales height (0..1) to meters
    bool  centerChunk    = true;  // center geometry around (0,0)
    bool  flipWinding    = false; // CW instead of CCW if your pipeline needs it
    float originX        = 0.0f;  // world offset (meters) if not centering
    float originZ        = 0.0f;
};

// Build a triangle-grid mesh for the given chunk.
// - Positions are chunk-local unless you provide origin/centering.
// - One vertex per height sample (N x N), two triangles per cell ((N-1) x (N-1) x 2).
MeshData BuildTerrainMesh(const WorldChunk& chunk, const TerrainMeshParams& params);

} // namespace colony::worldgen
