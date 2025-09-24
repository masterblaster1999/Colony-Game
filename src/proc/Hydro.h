#pragma once
// Hydro.h - Lightweight hydrology utilities for raster (row-major) grids.
// -------------------------------------------------------------------------------------------------
// This header provides pit filling, flow routing (D8 & MFD), flow accumulation, stream extraction,
// watershed labeling, Strahler/Shreve stream orders, flow length, slope/aspect (Horn), and TWI.
//
// Design goals:
//  - Header-only by default, with an opt-out to declarations-only if preferred.
//  - Zero external dependencies; C++17; works on std::vector<T> row-major grids.
//  - Safe, documented, and game-friendly defaults. Tweak via Options structs.
//  - Keeps your original API signatures for PriorityFlood(...) and FlowAccumulation(...).
//
// References (see the accompanying PR/commit message for details and links):
//  - Priority-Flood pit filling: Barnes, Lehman, Mulla (2014/2015).
//  - D8 single-flow: O'Callaghan & Mark (1984).
//  - D∞ concept & background: Tarboton (1997).
//  - MFD (multiple-flow direction) family: Quinn et al. (1991), Freeman (1991).
//  - Slope/Aspect (Horn, 1981 kernel).
//
// License: MIT (match your project if different).
// -------------------------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef HYDRO_HEADER_ONLY
  #define HYDRO_HEADER_ONLY 1
#endif

#ifndef HYDRO_ASSERT
  #include <cassert>
  #define HYDRO_ASSERT(x) assert(x)
#endif

namespace hydro {

// ---- Public basic types -------------------------------------------------------------------------

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i32 = std::int32_t;
using f32 = float;
using f64 = double;

// ---- Grid helpers --------------------------------------------------------------------------------

struct GridSize {
  int W = 0, H = 0;
  [[nodiscard]] int size() const noexcept { return W * H; }
};

constexpr int kDX8[8] = {+1,+1, 0,-1,-1,-1, 0,+1};
constexpr int kDY8[8] = { 0,+1,+1,+1, 0,-1,-1,-1};

constexpr int kDX4[4] = {+1, 0,-1, 0};
constexpr int kDY4[4] = { 0,+1, 0,-1};

[[nodiscard]] inline int Idx(int x, int y, int W) noexcept { return y * W + x; }
[[nodiscard]] inline bool InBounds(int x, int y, int W, int H) noexcept {
  return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H;
}

// ---- Enums & options -----------------------------------------------------------------------------

enum class NeighborMode : u8 { N4 = 4, N8 = 8 };
enum class BorderMode   : u8 { Open = 0, Torus = 1 };   // Torus = periodic wrapping
enum class DirEncoding  : u8 { D8Index = 0, Bitmask = 1 };

struct PFOptions {
  NeighborMode neighbors = NeighborMode::N8;
  BorderMode   border    = BorderMode::Open;
  f32          nodata    = std::numeric_limits<f32>::quiet_NaN();
  bool         make_monotone_with_epsilon = false;      // create gentle drains across flats
  f32          epsilon   = 1e-4f;                       // used only if flag above is true
};

struct D8Options {
  BorderMode border = BorderMode::Open;
  f32        nodata = std::numeric_limits<f32>::quiet_NaN();
};

struct AccumOptions {
  // For D8 accumulation:
  u32        base_contribution = 1;    // contribution per valid cell
  bool       include_self      = true; // if false, sources start from 0
  // For MFD accumulation:
  float      mfd_exponent      = 1.1f; // 1..∞, larger means more "D8-like"
};

struct SlopeOptions {
  f32 cell_size   = 1.0f;              // horizontal/vertical cell size (assumes square cells)
  f32 nodata      = std::numeric_limits<f32>::quiet_NaN();
};

struct StreamOptions {
  // Everything >= threshold becomes "stream".
  // For integer D8 accumulation, a good starting threshold is ~ (W+H)/6.
  float threshold = 50.0f;
};

struct LabelOptions {
  // If true, any edge cell (or cell flowing outside) is an outlet label root.
  bool edges_are_outlets = true;
};

// ---- NoData helpers ------------------------------------------------------------------------------

template <class T> [[nodiscard]] inline bool IsNaNLike(T v) {
  if constexpr (std::is_floating_point<T>::value) return std::isnan(v);
  return false;
}
template <class T> [[nodiscard]] inline bool IsNoData(T v, T nodata) {
  if constexpr (std::is_floating_point<T>::value) {
    if (std::isnan(nodata)) return std::isnan(v);
  }
  return v == nodata;
}

// --------------------------------------------------------------------------------------------------
//  Original API (kept for compatibility)
// --------------------------------------------------------------------------------------------------

// In-place pit filling (Priority-Flood) on a row-major grid.
void PriorityFlood(std::vector<float>& H, int W, int Hgt);

// Classic D8 flow accumulation (expects depression-free heights).
void FlowAccumulation(const std::vector<float>& H, int W, int Hgt, std::vector<uint32_t>& outAccum);

// --------------------------------------------------------------------------------------------------
//  Expanded API
// --------------------------------------------------------------------------------------------------

// Priority-Flood with configuration & nodata handling.
void PriorityFlood(std::vector<float>& z, GridSize g, const PFOptions& opt);

// Compute D8 downslope target index per cell; -1 for outlets/sinks.
void FlowDirsD8(const std::vector<float>& z, GridSize g, std::vector<int>& out_to, const D8Options& opt = {});

// D8 flow accumulation given precomputed directions (-1 means outlet). Returns integer counts.
void FlowAccumulationD8(const std::vector<int>& to, GridSize g,
                        std::vector<uint32_t>& accum,
                        const AccumOptions& opt = {});

// One-shot: compute D8 directions and accumulation from heights (depression-free recommended).
void FlowAccumulationD8(const std::vector<float>& z, GridSize g,
                        std::vector<int>& out_to,
                        std::vector<uint32_t>& accum,
                        const D8Options& dopt = {}, const AccumOptions& aopt = {});

// MFD (multiple flow direction) accumulation (Freeman/Quinn family); float output.
void FlowAccumulationMFD(const std::vector<float>& z, GridSize g,
                         std::vector<float>& accum, const AccumOptions& opt = {});

// Extract binary stream mask from accumulation.
void ExtractStreams(const std::vector<uint32_t>& accum_u32, GridSize g,
                    std::vector<u8>& out_stream_mask, const StreamOptions& opt = {});
void ExtractStreams(const std::vector<float>& accum_f32, GridSize g,
                    std::vector<u8>& out_stream_mask, const StreamOptions& opt = {});

// Label watersheds (ID per cell) by propagating from outlets upstream.
void LabelWatersheds(const std::vector<int>& to, GridSize g, std::vector<int>& out_label,
                     const LabelOptions& opt = {});

// Strahler stream order on a stream network (mask=1 on streams).
void StrahlerOrder(const std::vector<int>& to, const std::vector<u8>& stream_mask, GridSize g,
                   std::vector<u16>& out_order);

// Shreve magnitude on a stream network (mask=1 on streams).
void ShreveMagnitude(const std::vector<int>& to, const std::vector<u8>& stream_mask, GridSize g,
                     std::vector<u32>& out_mag);

// Longest downslope flow length (in world units) to an outlet (D8).
void LongestFlowLengthD8(const std::vector<int>& to, GridSize g, float cell_size,
                         std::vector<float>& out_len);

// Horn slope/aspect from heights (radians). Out-of-bounds/nodata -> NaN.
void SlopeAspectHorn(const std::vector<float>& z, GridSize g, const SlopeOptions& opt,
                     std::vector<float>& out_slope, std::vector<float>& out_aspect);

// Topographic Wetness Index = ln( a / tan(beta) ), where a is specific catchment area.
// Requires a flow accumulation and slope (radians).
template <class AccumT>
void TopographicWetnessIndex(const std::vector<AccumT>& accum, const std::vector<float>& slope,
                             GridSize g, float cell_size, std::vector<float>& out_twi);

// ---- Implementation ------------------------------------------------------------------------------

#if HYDRO_HEADER_ONLY

// Priority-Flood implementation (Barnes et al.). Uses a min-heap + plain queue inside depressions.
// Safe, simple, O(n log n) for floating-point, O(n) for integer-ish orderings.
inline void PriorityFlood(std::vector<float>& z, GridSize g, const PFOptions& opt) {
  const int W = g.W, H = g.H, N = W * H;
  if (N == 0) return;

  auto is_nd = [&](float v) { return IsNoData<float>(v, opt.nodata); };

  struct Node { float h; int i; };
  struct Cmp {
    bool operator()(const Node& a, const Node& b) const noexcept { return a.h > b.h; } // min-heap
  };

  std::priority_queue<Node, std::vector<Node>, Cmp> pq;
  std::vector<char> visited(N, 0);
  std::queue<int> q;

  auto push_if = [&](int x, int y) {
    if (!InBounds(x, y, W, H)) return;
    const int i = Idx(x, y, W);
    if (visited[i] || is_nd(z[i])) return;
    visited[i] = 1;
    pq.push({z[i], i});
  };

  // Seed with "ocean": all edge cells that are valid (Open border). Torus: treat all edges as normal.
  if (opt.border == BorderMode::Open) {
    for (int x = 0; x < W; ++x) { push_if(x, 0); push_if(x, H - 1); }
    for (int y = 1; y < H - 1; ++y) { push_if(0, y); push_if(W - 1, y); }
  } else { // Torus: fall back to pushing global minima to ensure progress
    int argmin = -1;
    float mn = std::numeric_limits<float>::infinity();
    for (int i = 0; i < N; ++i) if (!is_nd(z[i]) && z[i] < mn) { mn = z[i]; argmin = i; }
    if (argmin >= 0) { visited[argmin] = 1; pq.push({z[argmin], argmin}); }
  }

  const bool use8 = (opt.neighbors == NeighborMode::N8);

  auto for_each_nb = [&](int i, auto&& f) {
    const int x = i % W, y = i / W;
    if (use8) {
      for (int k = 0; k < 8; ++k) {
        int nx = x + kDX8[k], ny = y + kDY8[k];
        if (opt.border == BorderMode::Torus) {
          nx = (nx + W) % W; ny = (ny + H) % H;
        }
        if (!InBounds(nx, ny, W, H)) continue;
        f(Idx(nx, ny, W));
      }
    } else {
      for (int k = 0; k < 4; ++k) {
        int nx = x + kDX4[k], ny = y + kDY4[k];
        if (opt.border == BorderMode::Torus) {
          nx = (nx + W) % W; ny = (ny + H) % H;
        }
        if (!InBounds(nx, ny, W, H)) continue;
        f(Idx(nx, ny, W));
      }
    }
  };

  auto raise_if_needed = [&](int nb, float refh) {
    if (IsNoData<float>(z[nb], opt.nodata)) return;
    if (z[nb] <= refh) {
      z[nb] = opt.make_monotone_with_epsilon ? (refh + opt.epsilon) : refh;
      q.push(nb);
    } else {
      pq.push({z[nb], nb});
    }
  };

  while (!pq.empty()) {
    Node n = pq.top(); pq.pop();
    for_each_nb(n.i, [&](int nb) {
      if (visited[nb]) return;
      visited[nb] = 1;
      raise_if_needed(nb, n.h);
    });

    // Process interior of the depression using a plain queue (O(1) per cell).
    while (!q.empty()) {
      int u = q.front(); q.pop();
      for_each_nb(u, [&](int v) {
        if (visited[v]) return;
        visited[v] = 1;
        raise_if_needed(v, z[u]);
      });
    }
  }
}

inline void PriorityFlood(std::vector<float>& H, int W, int Hgt) {
  PriorityFlood(H, GridSize{W, Hgt}, PFOptions{});
}

// Compute D8 target index per cell (steepest descent). -1 if pit/outlet/no descent.
inline void FlowDirsD8(const std::vector<float>& z, GridSize g,
                       std::vector<int>& out_to, const D8Options& opt) {
  const int W = g.W, H = g.H, N = W * H;
  out_to.assign(N, -1);
  if (N == 0) return;

  auto is_nd = [&](float v) { return IsNoData<float>(v, opt.nodata); };

  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const int i = Idx(x, y, W);
      const float zi = z[i];
      if (is_nd(zi)) { out_to[i] = -1; continue; }

      int best = -1;
      float best_drop = 0.0f;

      for (int k = 0; k < 8; ++k) {
        int nx = x + kDX8[k], ny = y + kDY8[k];
        if (opt.border == BorderMode::Torus) {
          nx = (nx + W) % W; ny = (ny + H) % H;
        }
        if (!InBounds(nx, ny, W, H)) continue;

        const int j = Idx(nx, ny, W);
        const float zj = z[j];
        if (is_nd(zj)) continue;
        const float dz = zi - zj;
        if (dz <= 0.0f) continue;

        // Diagonal neighbors are at distance sqrt(2), cardinal at 1: use slope = drop/dist.
        const float dist = (k == 0 || k == 2 || k == 4 || k == 6) ? 1.0f : std::sqrt(2.0f);
        const float slope = dz / dist;
        if (slope > best_drop) { best_drop = slope; best = j; }
      }

      out_to[i] = best; // may be -1
    }
  }
}

// D8 accumulation from given directions (-1 => outlet). Topologically processes cells by indegree.
inline void FlowAccumulationD8(const std::vector<int>& to, GridSize g,
                               std::vector<uint32_t>& accum, const AccumOptions& opt) {
  const int N = g.W * g.H;
  accum.assign(N, 0);
  if (N == 0) return;

  std::vector<int> indeg(N, 0);
  for (int i = 0; i < N; ++i) if (to[i] >= 0) ++indeg[to[i]];

  std::queue<int> q;
  for (int i = 0; i < N; ++i) if (indeg[i] == 0) q.push(i);

  // initialize sources
  if (opt.include_self) {
    for (int i = 0; i < N; ++i) accum[i] = (to[i] >= -1) ? opt.base_contribution : 0;
  }

  while (!q.empty()) {
    int u = q.front(); q.pop();
    int v = to[u];
    if (v >= 0) {
      accum[v] += accum[u];
      if (--indeg[v] == 0) q.push(v);
    }
  }
}

// One-shot D8 directions + accumulation.
inline void FlowAccumulationD8(const std::vector<float>& z, GridSize g,
                               std::vector<int>& out_to,
                               std::vector<uint32_t>& accum,
                               const D8Options& dopt, const AccumOptions& aopt) {
  FlowDirsD8(z, g, out_to, dopt);
  FlowAccumulationD8(out_to, g, accum, aopt);
}

// Original FlowAccumulation signature kept: computes D8 internally.
inline void FlowAccumulation(const std::vector<float>& H, int W, int Hgt,
                             std::vector<uint32_t>& outAccum) {
  std::vector<int> to;
  FlowAccumulationD8(H, GridSize{W, Hgt}, to, outAccum, D8Options{}, AccumOptions{});
}

// Multiple-Flow-Direction accumulation (MFD): distribute to all downslope neighbors
// with weights proportional to slope^p (Freeman/Quinn family).
inline void FlowAccumulationMFD(const std::vector<float>& z, GridSize g,
                                std::vector<float>& accum, const AccumOptions& opt) {
  const int W = g.W, H = g.H, N = W * H;
  accum.assign(N, 0.0f);
  if (N == 0) return;

  // Topological ordering by height (descending) is sufficient on depression-free DEMs.
  std::vector<int> order(N);
  order.reserve(N);
  for (int i = 0; i < N; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return z[a] > z[b]; });

  for (int idx = 0; idx < N; ++idx) {
    int i = order[idx];
    float zi = z[i];
    float base = opt.include_self ? float(opt.base_contribution) : 0.0f;
    accum[i] += base;

    // Gather downslope neighbors and weights
    float weight_sum = 0.0f;
    float w[8]; int nb[8]; int m = 0;

    const int x = i % W, y = i / W;
    for (int k = 0; k < 8; ++k) {
      int nx = x + kDX8[k], ny = y + kDY8[k];
      if (!InBounds(nx, ny, W, H)) continue;
      int j = Idx(nx, ny, W);
      float dz = zi - z[j];
      if (dz <= 0.0f) continue;
      const float dist = (k == 0 || k == 2 || k == 4 || k == 6) ? 1.0f : std::sqrt(2.0f);
      const float s = dz / dist;
      const float ww = std::pow(s, std::max(1.0f, opt.mfd_exponent));
      w[m] = ww; nb[m] = j; weight_sum += ww; ++m;
    }

    if (m == 0) continue; // local minimum/outlet
    const float inv = (weight_sum > 0.0f) ? (1.0f / weight_sum) : 0.0f;
    const float a = accum[i];
    for (int t = 0; t < m; ++t)
      accum[nb[t]] += a * (w[t] * inv);
  }
}

inline void ExtractStreams(const std::vector<uint32_t>& acc, GridSize g,
                           std::vector<u8>& out_mask, const StreamOptions& opt) {
  const int N = g.W * g.H; out_mask.assign(N, 0);
  for (int i = 0; i < N; ++i) out_mask[i] = (acc[i] >= (uint32_t)std::max(0.0f, opt.threshold)) ? 1 : 0;
}
inline void ExtractStreams(const std::vector<float>& acc, GridSize g,
                           std::vector<u8>& out_mask, const StreamOptions& opt) {
  const int N = g.W * g.H; out_mask.assign(N, 0);
  for (int i = 0; i < N; ++i) out_mask[i] = (acc[i] >= opt.threshold) ? 1 : 0;
}

// Assign watershed labels by propagating from outlets upstream along D8.
inline void LabelWatersheds(const std::vector<int>& to, GridSize g, std::vector<int>& label,
                            const LabelOptions& opt) {
  const int W = g.W, H = g.H, N = W * H;
  label.assign(N, -1);
  int next_label = 0;

  auto is_edge = [&](int i) {
    int x = i % W, y = i / W;
    return (x == 0 || y == 0 || x == W - 1 || y == H - 1);
  };

  std::queue<int> q;

  // Seed labels at outlets
  for (int i = 0; i < N; ++i) {
    if (to[i] == -1 && (!opt.edges_are_outlets || is_edge(i))) {
      label[i] = next_label++;
      q.push(i);
    }
  }
  if (opt.edges_are_outlets) {
    for (int i = 0; i < N; ++i) {
      if (label[i] < 0 && is_edge(i) && to[i] == -1) { label[i] = next_label++; q.push(i); }
    }
  }
  // If graph has no -1 (rare), choose any "lowest indegree" as outlet substitute.
  if (next_label == 0) {
    for (int i = 0; i < N; ++i) if (to[i] == -1) { label[i] = next_label++; q.push(i); }
  }

  // BFS upstream: a cell belongs to the same label as the cell it flows *into*.
  auto for_each_upstream = [&](int center, auto&& f) {
    const int cx = center % W, cy = center / W;
    for (int k = 0; k < 8; ++k) {
      int nx = cx + kDX8[k], ny = cy + kDY8[k];
      if (!InBounds(nx, ny, W, H)) continue;
      int p = Idx(nx, ny, W);
      if (to[p] == center) f(p);
    }
  };

  while (!q.empty()) {
    int u = q.front(); q.pop();
    for_each_upstream(u, [&](int p) {
      if (label[p] >= 0) return;
      label[p] = label[u];
      q.push(p);
    });
  }
}

// Strahler stream order (requires stream mask).
inline void StrahlerOrder(const std::vector<int>& to, const std::vector<u8>& stream, GridSize g,
                          std::vector<u16>& order) {
  const int N = g.W * g.H;
  order.assign(N, 0);
  std::vector<int> indeg(N, 0), maxOrd(N, 0), cntMax(N, 0);
  std::queue<int> q;

  for (int i = 0; i < N; ++i) if (stream[i]) {
    int v = to[i]; if (v >= 0 && stream[v]) ++indeg[v];
  }
  for (int i = 0; i < N; ++i) if (stream[i] && indeg[i] == 0) { order[i] = 1; q.push(i); }

  while (!q.empty()) {
    int u = q.front(); q.pop();
    int v = to[u];
    if (v < 0 || !stream[v]) continue;

    if (order[u] > maxOrd[v]) { maxOrd[v] = order[u]; cntMax[v] = 1; }
    else if (order[u] == maxOrd[v]) { cntMax[v] += 1; }

    if (--indeg[v] == 0) {
      order[v] = (cntMax[v] >= 2) ? u16(maxOrd[v] + 1) : u16(maxOrd[v]);
      q.push(v);
    }
  }
}

// Shreve magnitude (sum of headwaters contributing counts).
inline void ShreveMagnitude(const std::vector<int>& to, const std::vector<u8>& stream, GridSize g,
                            std::vector<u32>& mag) {
  const int N = g.W * g.H;
  mag.assign(N, 0);
  std::vector<int> indeg(N, 0);
  std::queue<int> q;

  for (int i = 0; i < N; ++i) if (stream[i]) {
    int v = to[i]; if (v >= 0 && stream[v]) ++indeg[v];
  }
  for (int i = 0; i < N; ++i) if (stream[i] && indeg[i] == 0) { mag[i] = 1; q.push(i); }

  while (!q.empty()) {
    int u = q.front(); q.pop();
    int v = to[u];
    if (v < 0 || !stream[v]) continue;
    mag[v] += mag[u];
    if (--indeg[v] == 0) q.push(v);
  }
}

// Longest D8 flow path length to outlet (in same units as cell_size).
inline void LongestFlowLengthD8(const std::vector<int>& to, GridSize g, float cell_size,
                                std::vector<float>& out_len) {
  const int W = g.W, H = g.H, N = W * H;
  out_len.assign(N, 0.0f);

  std::vector<int> indeg(N, 0);
  for (int i = 0; i < N; ++i) if (to[i] >= 0) ++indeg[to[i]];
  std::queue<int> q; for (int i = 0; i < N; ++i) if (indeg[i] == 0) q.push(i);

  while (!q.empty()) {
    int u = q.front(); q.pop();
    int v = to[u];
    if (v >= 0) {
      // step distance depends on diagonal vs cardinal
      int ux = u % W, uy = u / W;
      int vx = v % W, vy = v / W;
      const float step = ((ux != vx) && (uy != vy)) ? (cell_size * std::sqrt(2.0f)) : cell_size;
      out_len[v] = std::max(out_len[v], out_len[u] + step);
      if (--indeg[v] == 0) q.push(v);
    }
  }
}

// Horn slope/aspect (3x3 kernel). Slope in radians; aspect in radians, measured clockwise from +x.
inline void SlopeAspectHorn(const std::vector<float>& z, GridSize g, const SlopeOptions& opt,
                            std::vector<float>& out_slope, std::vector<float>& out_aspect) {
  const int W = g.W, H = g.H, N = W * H;
  out_slope.assign(N, std::numeric_limits<float>::quiet_NaN());
  out_aspect.assign(N, std::numeric_limits<float>::quiet_NaN());
  if (N == 0) return;

  auto zAt = [&](int x, int y) -> float {
    if (!InBounds(x, y, W, H)) return std::numeric_limits<float>::quiet_NaN();
    return z[Idx(x, y, W)];
  };

  const float cs = opt.cell_size;
  const float inv8cs = 1.f / (8.f * cs);

  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const float z1 = zAt(x-1,y-1), z2 = zAt(x,y-1), z3 = zAt(x+1,y-1);
      const float z4 = zAt(x-1,y  ), /* z5 */       z6 = zAt(x+1,y  );
      const float z7 = zAt(x-1,y+1), z8 = zAt(x,y+1), z9 = zAt(x+1,y+1);

      // If any neighbor is NaN, skip (edge or nodata).
      if (IsNaNLike(z1) || IsNaNLike(z2) || IsNaNLike(z3) ||
          IsNaNLike(z4) || IsNaNLike(z6) ||
          IsNaNLike(z7) || IsNaNLike(z8) || IsNaNLike(z9)) continue;

      const float dzdx = ( (z3 + 2*z6 + z9) - (z1 + 2*z4 + z7) ) * inv8cs;
      const float dzdy = ( (z7 + 2*z8 + z9) - (z1 + 2*z2 + z3) ) * inv8cs;

      const float slope  = std::atan(std::hypot(dzdx, dzdy));
      const float aspect = std::atan2(dzdy, -dzdx); // Horn convention

      out_slope[Idx(x, y, W)]  = slope;
      out_aspect[Idx(x, y, W)] = aspect; // radians
    }
  }
}

template <class AccumT>
inline void TopographicWetnessIndex(const std::vector<AccumT>& accum, const std::vector<float>& slope,
                                    GridSize g, float cell_size, std::vector<float>& out_twi) {
  const int N = g.W * g.H;
  out_twi.resize(N);
  const float eps = 1e-6f;

  for (int i = 0; i < N; ++i) {
    const float a = float(accum[i]) * cell_size;     // specific catchment area ~ (cells * cell_size)
    const float beta = slope[i];                     // radians
    const float twi = std::log( (a + eps) / std::max(std::tan(beta), eps) );
    out_twi[i] = twi;
  }
}

#endif // HYDRO_HEADER_ONLY

} // namespace hydro
