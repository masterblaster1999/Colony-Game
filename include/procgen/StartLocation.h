#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <random>
#include <vector>

#include "Biome.h"
#include "Heightmap.h"
#include "ResourceGen.h"
#include "Types.h"

namespace procgen {

// A small, practical helper to choose a "good" colony start cell from the generated world.
// It scores candidate land tiles using:
//   - distance to water (via multi-source BFS distance field)
//   - local slope (max abs height delta to 4-neighbors)
//   - biome preference (optional, uses the provided biome map)
//   - proximity to resources (optional, uses ResourceSite list)
//
// It also provides an optional "flatten start area" function to create a playable plateau.

struct StartLocationParams {
  // RNG used only for tie-breaking (small jitter). Does NOT affect constraints.
  uint32_t seed = 1337;

  // "Water" is defined as elevation <= seaLevel.
  float seaLevel = 0.45f;

  // Candidate sampling grid step. Higher = faster, lower = better choices.
  // For 512x512: step=6 evaluates ~7k points.
  int candidateStep = 6;

  // Ignore a border around the map (avoid spawning at edges).
  int border = 10;

  // Slope constraint: max abs height delta between this tile and its 4-neighbors.
  // Heightmap values are typically normalized; tune depending on your generator.
  float maxSlope = 0.018f;

  // Distance-to-water constraints in tiles (Manhattan distance from BFS).
  // Typical colony start: not right on the coast, but close enough.
  int minWaterDist = 8;
  int maxWaterDist = 80; // set <= 0 to disable the max constraint

  // A soft preference toward an "ideal" distance (within [minWaterDist..maxWaterDist]).
  // Used only for scoring; constraints are still enforced above.
  int idealWaterDist = 22;

  // Resource scoring
  // If you don't have resources yet, you can pass an empty vector.
  int resourceRadius = 34; // tiles
  float resourceKindWeight[4] = {
      1.00f, // kind 0 (wood-ish)
      0.70f, // kind 1 (game-ish)
      0.90f, // kind 2 (ore-ish)
      0.55f  // kind 3 (oil-ish)
  };

  // Biome preference weights in Biome enum order:
  // Ocean, Beach, Grassland, Forest, Desert, Savanna, Taiga, Tundra, Mountain, Snow
  float biomeWeight[10] = {
      -1000.0f, // Ocean (never pick)
      0.25f,    // Beach
      1.00f,    // Grassland
      0.95f,    // Forest
      0.10f,    // Desert
      0.75f,    // Savanna
      0.60f,    // Taiga
      0.25f,    // Tundra
      0.15f,    // Mountain
      0.05f     // Snow
  };

  // Overall score weights (tune to taste)
  float wBiome = 3.0f;
  float wWater = 2.0f;
  float wSlope = 1.5f;
  float wRes   = 2.0f;

  // Optional: recommended flattening settings (just helpers; you can ignore)
  int   recommendedFlattenRadius = 14;
  float recommendedFlattenBlend  = 0.75f;
};

struct StartLocationDebug {
  int distToWater = 0;
  float slope = 0.0f;
  float waterScore = 0.0f;
  float biomeScore = 0.0f;
  float resourceScore = 0.0f;
};

struct StartLocationResult {
  IV2 cell{ -1, -1 };
  float score = -std::numeric_limits<float>::infinity();
  StartLocationDebug dbg{};
};

// ------------------------------ internals --------------------------------------

inline int _sl_idx(int x, int y, int w) { return y * w + x; }

inline float _sl_clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

inline float _sl_smoothstep(float t) {
  t = _sl_clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}

inline float _sl_max_neighbor_slope_4(const Heightmap& hm, int x, int y) {
  const float e = hm.at(x, y);
  float m = 0.0f;
  if (x > 0)              m = std::max(m, std::abs(e - hm.at(x - 1, y)));
  if (x + 1 < hm.width)   m = std::max(m, std::abs(e - hm.at(x + 1, y)));
  if (y > 0)              m = std::max(m, std::abs(e - hm.at(x, y - 1)));
  if (y + 1 < hm.height)  m = std::max(m, std::abs(e - hm.at(x, y + 1)));
  return m;
}

inline std::vector<int> compute_distance_to_water(const Heightmap& hm, float seaLevel) {
  const int w = hm.width, h = hm.height;
  const int N = w * h;
  constexpr int INF = std::numeric_limits<int>::max();

  std::vector<int> dist((size_t)N, INF);
  std::deque<int> q;
  q.clear();

  // multi-source init: all water cells at dist 0
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int i = _sl_idx(x, y, w);
      if (hm.at(x, y) <= seaLevel) {
        dist[(size_t)i] = 0;
        q.push_back(i);
      }
    }
  }

  // If there is no water at all, keep INF distances.
  if (q.empty()) return dist;

  static const int DX[4] = { -1, 1, 0, 0 };
  static const int DY[4] = { 0, 0, -1, 1 };

  while (!q.empty()) {
    const int cur = q.front();
    q.pop_front();

    const int cx = cur % w;
    const int cy = cur / w;
    const int cd = dist[(size_t)cur];

    for (int k = 0; k < 4; ++k) {
      const int nx = cx + DX[k];
      const int ny = cy + DY[k];
      if (!in_bounds(nx, ny, w, h)) continue;

      const int ni = _sl_idx(nx, ny, w);
      if (dist[(size_t)ni] > cd + 1) {
        dist[(size_t)ni] = cd + 1;
        q.push_back(ni);
      }
    }
  }

  return dist;
}

inline float biome_score_from_map(uint8_t biomeU8, const StartLocationParams& p) {
  const int bi = (int)biomeU8;
  if (bi < 0 || bi >= 10) return 0.0f;
  return p.biomeWeight[bi];
}

inline float water_distance_score(int d, const StartLocationParams& p) {
  // Soft peak at idealWaterDist; falls off within a reasonable band.
  const float ideal = (float)std::max(1, p.idealWaterDist);
  // sigma tuned from the allowed range if max is enabled
  float sigma = 18.0f;
  if (p.maxWaterDist > 0 && p.maxWaterDist > p.minWaterDist) {
    sigma = std::max(6.0f, 0.35f * float(p.maxWaterDist - p.minWaterDist));
  }
  const float z = (float(d) - ideal) / sigma;
  // gaussian-ish in [~0..1]
  return std::exp(-0.5f * z * z);
}

inline float resource_proximity_score(
    const std::vector<ResourceSite>& resources,
    int x, int y,
    const StartLocationParams& p)
{
  if (resources.empty() || p.resourceRadius <= 0) return 0.0f;

  const float R = (float)p.resourceRadius;
  const float R2 = R * R;

  float sum = 0.0f;
  for (const auto& rs : resources) {
    const float dx = float(rs.cell.x - x);
    const float dy = float(rs.cell.y - y);
    const float d2 = dx * dx + dy * dy;
    if (d2 > R2) continue;

    const float d = std::sqrt(std::max(0.0f, d2));
    const float falloff = 1.0f - (d / R); // 1 near, 0 at edge

    float kindW = 0.6f;
    if (rs.kind < 4) kindW = p.resourceKindWeight[rs.kind];

    sum += (rs.richness * kindW) * falloff;
  }

  // squash to (0..1) so it combines nicely with the other terms
  // 1 - exp(-k*sum) : fast rise, saturates at 1
  constexpr float k = 0.85f;
  return 1.0f - std::exp(-k * sum);
}

// ------------------------------ public API -------------------------------------

// Main API (biome map optional but recommended):
// - biomes: same size as elevation (w*h), storing Biome enum values as uint8_t.
//           If empty, biome scoring is treated as 0.
inline StartLocationResult find_start_location(
    const Heightmap& elevation,
    const std::vector<uint8_t>& biomes,
    const std::vector<ResourceSite>& resources,
    const StartLocationParams& p)
{
  StartLocationResult best;

  const int w = elevation.width;
  const int h = elevation.height;
  if (w <= 0 || h <= 0) return best;

  const int N = w * h;
  const bool haveBiomes = ((int)biomes.size() == N);

  // Precompute distance-to-water once.
  const std::vector<int> distToWater = compute_distance_to_water(elevation, p.seaLevel);

  // Candidate sampling
  const int step = std::max(1, p.candidateStep);
  const int bx0 = std::max(0, p.border);
  const int by0 = std::max(0, p.border);
  const int bx1 = std::min(w - 1, w - 1 - p.border);
  const int by1 = std::min(h - 1, h - 1 - p.border);

  std::mt19937 rng(p.seed);
  std::uniform_real_distribution<float> jitter(0.0f, 1e-4f);

  for (int y = by0; y <= by1; y += step) {
    for (int x = bx0; x <= bx1; x += step) {
      const float e = elevation.at(x, y);
      if (e <= p.seaLevel) continue; // not on water

      const float slope = _sl_max_neighbor_slope_4(elevation, x, y);
      if (slope > p.maxSlope) continue;

      const int d = distToWater[(size_t)_sl_idx(x, y, w)];
      if (d == std::numeric_limits<int>::max()) {
        // no water in map => skip water constraints/scoring
      } else {
        if (d < p.minWaterDist) continue;
        if (p.maxWaterDist > 0 && d > p.maxWaterDist) continue;
      }

      const float biomeScore = haveBiomes ? biome_score_from_map(biomes[(size_t)_sl_idx(x, y, w)], p) : 0.0f;
      // If biome says "Ocean" but elevation isn't water due to mismatch, the weight will nuke the score.
      // That's fine: it prevents weird spawns.
      if (biomeScore <= -999.0f) continue;

      const float waterScore = (d == std::numeric_limits<int>::max()) ? 0.35f : water_distance_score(d, p);
      const float slopeScore = 1.0f - _sl_clamp01(slope / std::max(1e-6f, p.maxSlope));
      const float resScore   = resource_proximity_score(resources, x, y, p);

      float score =
          p.wBiome * biomeScore +
          p.wWater * waterScore +
          p.wSlope * slopeScore +
          p.wRes   * resScore +
          jitter(rng);

      if (score > best.score) {
        best.score = score;
        best.cell = { x, y };
        best.dbg.distToWater = (d == std::numeric_limits<int>::max()) ? -1 : d;
        best.dbg.slope = slope;
        best.dbg.waterScore = waterScore;
        best.dbg.biomeScore = biomeScore;
        best.dbg.resourceScore = resScore;
      }
    }
  }

  // Fallback: if nothing found, relax constraints and pick *some* land tile.
  if (best.cell.x < 0) {
    for (int y = by0; y <= by1; ++y) {
      for (int x = bx0; x <= bx1; ++x) {
        const float e = elevation.at(x, y);
        if (e <= p.seaLevel) continue;
        best.cell = { x, y };
        best.score = 0.0f;
        return best;
      }
    }
  }

  return best;
}

// Convenience overload (no biomes/resources yet).
inline StartLocationResult find_start_location(
    const Heightmap& elevation,
    const StartLocationParams& p)
{
  static const std::vector<uint8_t> emptyBiomes;
  static const std::vector<ResourceSite> emptyRes;
  return find_start_location(elevation, emptyBiomes, emptyRes, p);
}

// Optional helper: flatten a circular start area into a gentle plateau.
// This is useful if your generator produces rugged terrain and you want a playable spawn area.
//
// - blend: 0..1 (how hard to blend toward target height at center)
// - Keeps everything above seaLevel (won't dig into water), but it also won't raise water tiles.
//   (In practice, the start picker keeps you away from water anyway.)
inline void flatten_start_area(
    Heightmap& elevation,
    IV2 center,
    int radius,
    float seaLevel,
    float blend = 0.75f)
{
  if (radius <= 0) return;
  const int w = elevation.width, h = elevation.height;
  if (!in_bounds(center.x, center.y, w, h)) return;

  // Choose a target height: average of a small neighborhood around the center, ignoring water.
  float sum = 0.0f;
  int cnt = 0;
  const int r0 = std::min(3, radius);
  for (int dy = -r0; dy <= r0; ++dy) {
    for (int dx = -r0; dx <= r0; ++dx) {
      const int x = center.x + dx;
      const int y = center.y + dy;
      if (!in_bounds(x, y, w, h)) continue;
      const float e = elevation.at(x, y);
      if (e <= seaLevel) continue;
      sum += e;
      cnt++;
    }
  }
  const float target = (cnt > 0) ? (sum / float(cnt)) : std::max(seaLevel + 0.02f, elevation.at(center.x, center.y));

  const float R = float(radius);
  const float invR = 1.0f / std::max(1.0f, R);

  for (int y = center.y - radius; y <= center.y + radius; ++y) {
    for (int x = center.x - radius; x <= center.x + radius; ++x) {
      if (!in_bounds(x, y, w, h)) continue;

      const float e = elevation.at(x, y);
      if (e <= seaLevel) continue; // don't alter water tiles

      const float dx = float(x - center.x);
      const float dy = float(y - center.y);
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d > R) continue;

      // weight 1 at center -> 0 at edge, smoothed
      const float t = 1.0f - (d * invR);
      const float wgt = _sl_smoothstep(t) * _sl_clamp01(blend);

      // Blend toward target height
      elevation.at(x, y) = e * (1.0f - wgt) + target * wgt;
    }
  }
}

} // namespace procgen
