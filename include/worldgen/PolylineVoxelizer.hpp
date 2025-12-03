#pragma once
// ============================================================================
// PolylineVoxelizer.hpp — voxelize 2D polylines (at layer z) + emit instances
// C++17 / STL-only | Designed to pair with CaveNetworkGenerator.hpp
//
// What you get:
//  • A tiny VoxelVolume (W×H×Z) with set/get helpers
//  • RasterizePolylineWide2D(...) → stamps a "thick" line as voxels
//  • VoxelizePolylineLayer(...)   → same but across a z-thickness band
//  • VoxelizeAll(...)             → convenience for a batch of polylines
//  • SampleInstancesAlongPolyline(...) → positions + yaw every N cells
//
// Notes:
//  • Segment stepping uses an integer Bresenham 2D line; we then "dilate"
//    with a disk structuring element to achieve thickness. For 3D ray-like
//    traversals, see Amanatides & Woo's fast voxel traversal (DDA variant).
//    (Concept refs at bottom.)
// ============================================================================

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <utility>

namespace worldgen {

struct I2 { int x=0, y=0; };
struct Pose {
    float x=0, y=0, z=0;   // grid-space; multiply by cell size in your renderer
    float yaw=0;           // radians; +X is 0, +Y is +π/2 (atan2(dy,dx))
    float scale=1.0f;      // optional per-instance scaler
    int   kind=0;          // user tag (e.g., 0=beam,1=torch,...)
};

// -------------------- Voxel volume (uint8 occupancy) --------------------

struct VoxelVolume {
    int W=0, H=0, Z=0;
    std::vector<uint8_t> vox; // size W*H*Z, 0/1 occupancy

    VoxelVolume() = default;
    VoxelVolume(int w,int h,int z): W(w),H(h),Z(z),vox((size_t)w*h*z,0u){}
    inline size_t idx(int x,int y,int z) const { return (size_t)z*(size_t)W*H + (size_t)y*(size_t)W + (size_t)x; }
    inline bool inb(int x,int y,int z) const {
        return (unsigned)x<(unsigned)W && (unsigned)y<(unsigned)H && (unsigned)z<(unsigned)Z;
    }
    inline uint8_t get(int x,int y,int z) const { return inb(x,y,z) ? vox[idx(x,y,z)] : 0u; }
    inline void     set(int x,int y,int z, uint8_t v){ if(inb(x,y,z)) vox[idx(x,y,z)] = v; }
};

// -------------------- Internals: raster + stamping --------------------

namespace detail {

// Integer Bresenham 2D (covers main path; we "thicken" with a disk stamp).
template <class Fn>
inline void bresenham2D(int x0,int y0,int x1,int y1, Fn&& visit)
{
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy =-std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy; // err = dx - |dy|
    int x=x0, y=y0;
    for(;;){
        visit(x,y);
        if (x==x1 && y==y1) break;
        int e2 = 2*err;
        if (e2 >= dy){ err += dy; x += sx; }
        if (e2 <= dx){ err += dx; y += sy; }
    }
}

// Stamp a solid disc of radius r in XY at fixed z.
inline void stamp_disc_xy(VoxelVolume& vol, int cx,int cy,int z, float r)
{
    int R = (int)std::ceil(r);
    for(int oy=-R; oy<=R; ++oy){
        for(int ox=-R; ox<=R; ++ox){
            if (ox*ox + oy*oy > r*r) continue;
            int x=cx+ox, y=cy+oy;
            if (vol.inb(x,y,z)) vol.set(x,y,z, 1u);
        }
    }
}

} // namespace detail

// -------------------- Public API --------------------

// Rasterize a "thick" 2D polyline into layer z (single z-slice).
inline void RasterizePolylineWide2D(
    VoxelVolume& vol, const std::vector<I2>& pts,
    int z, float radiusXY)
{
    if ((int)pts.size()<2 || z<0 || z>=vol.Z) return;
    auto stampSeg = [&](int x0,int y0,int x1,int y1){
        detail::bresenham2D(x0,y0,x1,y1, [&](int x,int y){
            detail::stamp_disc_xy(vol, x,y,z, radiusXY);
        });
    };
    for (size_t i=1;i<pts.size();++i) stampSeg(pts[i-1].x, pts[i-1].y, pts[i].x, pts[i].y);
}

// Voxelize a 2D polyline into a *band* of z-layers (thickness in Z as well).
inline void VoxelizePolylineLayer(
    VoxelVolume& vol, const std::vector<I2>& pts,
    int zCenter, int zHalfThickness, float radiusXY)
{
    if (vol.Z<=0 || (int)pts.size()<2) return;
    int z0 = std::max(0, zCenter - zHalfThickness);
    int z1 = std::min(vol.Z-1, zCenter + zHalfThickness);
    for (int z=z0; z<=z1; ++z) RasterizePolylineWide2D(vol, pts, z, radiusXY);
}

// Batch convenience: radii/z-bands can vary per polyline via lambdas.
inline void VoxelizeAll(
    VoxelVolume& vol,
    const std::vector<std::vector<I2>>& polylines,
    const std::vector<int>* layerPerPolyline,       // optional; else use zCenter
    int zCenter, int zHalfThickness,
    float radiusXY)
{
    for (size_t i=0;i<polylines.size();++i){
        int zc = layerPerPolyline ? std::clamp((*layerPerPolyline)[i], 0, vol.Z-1) : zCenter;
        VoxelizePolylineLayer(vol, polylines[i], zc, zHalfThickness, radiusXY);
    }
}

// Emit evenly spaced instance poses along a polyline (position + yaw).
inline std::vector<Pose> SampleInstancesAlongPolyline(
    const std::vector<I2>& pts,
    float spacingCells, int kind=0,
    float z=0.0f, float scale=1.0f)
{
    std::vector<Pose> out;
    if (pts.size()<2 || spacingCells <= 0) return out;

    // Precompute cumulative arc length in grid cells
    std::vector<float> acc; acc.reserve(pts.size()); acc.push_back(0.f);
    for (size_t i=1;i<pts.size();++i){
        float dx = (float)(pts[i].x - pts[i-1].x);
        float dy = (float)(pts[i].y - pts[i-1].y);
        acc.push_back(acc.back() + std::sqrt(dx*dx + dy*dy));
    }
    float total = acc.back();
    if (total <= 0) return out;

    auto sampleAt = [&](float s)->Pose{
        // find segment
        size_t j=1; while (j<acc.size() && acc[j] < s) ++j;
        if (j >= acc.size()) j = acc.size()-1;
        float s0 = acc[j-1], s1 = acc[j], t = (s1> s0)? (s - s0)/(s1-s0) : 0.f;
        float x = (1-t)*pts[j-1].x + t*pts[j].x;
        float y = (1-t)*pts[j-1].y + t*pts[j].y;
        // orientation from local segment direction
        float dx = (float)(pts[j].x - pts[j-1].x);
        float dy = (float)(pts[j].y - pts[j-1].y);
        float yaw = std::atan2(dy, dx);
        return Pose{ x, y, z, yaw, scale, kind };
    };

    // place at uniform spacing; include start & (approx) end
    for (float s=0.f; s<=total; s+=spacingCells) out.push_back(sampleAt(s));
    if (out.empty() || (total - (float)((int)(total/spacingCells))*spacingCells) > 1e-3f)
        out.push_back(sampleAt(total));
    return out;
}

// Optional: supercover variant (covers all touched cells).
// For lines that must hit *every* cell crossed, consider replacing
// Bresenham with a "supercover" raster (see notes).
// Also, for 3D rays through voxels, consider Amanatides & Woo DDA.

} // namespace worldgen
