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

    // --- New fields ---
    bool  generateNormals = true; // allow skipping normal generation (e.g., if doing it on GPU)
    bool  index32         = false; // hint: prefer 16-bit indices when false (renderer may downcast)
    float skirtMeters     = 0.0f;  // optional skirt to hide cracks at edges/LOD boundaries
};

// Optional neighbor height sampler for border-aware normals.
// Coordinates are chunk-local; implementation may read adjacent chunks.
// If null, border sampling will clamp to the current chunk.
using HeightSampleFn = float(*)(int x, int y);

// Build a triangle-grid mesh for the given chunk.
// - Positions are chunk-local unless you provide origin/centering.
// - One vertex per height sample (N x N), two triangles per cell ((N-1) x (N-1) x 2).
// - If provided, 'neighbor' is used to sample heights outside [0..N-1] for smoother border normals.
MeshData BuildTerrainMesh(const WorldChunk& chunk,
                          const TerrainMeshParams& params,
                          HeightSampleFn neighbor = nullptr);

} // namespace colony::worldgen
