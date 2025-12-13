#pragma once

// ============================================================================
// Fertility.h
// Simple "fertility / farm suitability" map generator (0..1 per cell).
//
// Why this is useful in THIS repo:
// - procgen::WorldGen produces height/moisture/temperature/biome in WorldData.
// - worldgen::SettlementSitingGenerator can optionally consume a fertility01
//   layer to bias towns toward arable land.
// This header gives you that fertility01 layer with zero extra dependencies.
//
// Output:
// - fertility01[i] in [0..1], where 1 = great farmland, 0 = barren/unsuitable.
//
// Notes:
// - Treats water (height <= seaLevel) as 0 fertility.
// - Prefers: moderate elevation, gentle slopes, adequate moisture,
//            temperate climates, and specific biomes.
// - Includes a tiny optional smoothing pass to avoid speckle.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

#include "Biome.h"
#include "Types.h"

namespace procgen {

struct FertilityParams {
  // --- Basic interpretation ---
  float seaLevel = 0.45f;     // height01 <= seaLevel => water => fertility 0
  float hillStart = 0.62f;    // gentle penalty starts
  float mountainStart = 0.78f;// strong penalty above this

  // --- Slope model ---
  // We measure slope as max abs delta to 4-neighbors (in height01 units).
  // slopeFull = slope at which slopeFactor becomes 0.
  float slopeFull = 0.020f;

  // --- Water proximity preference (distance in grid cells) ---
  int   minWaterDist = 3;     // too close to shore => 0 water factor
  int   idealWaterDist = 18;  // irrigation/access sweet spot
  float waterSigma = 14.0f;   // width of Gaussian preference
  int   maxWaterDist = 160;   // beyond this, apply extra attenuation (<=0 disables)
  float farFromWaterMultiplier = 0.65f; // multiplier when beyond maxWaterDist

  // --- Moisture / temperature preference (expects 0..1 inputs) ---
  float idealMoisture = 0.62f;
  float moistureSigma = 0.20f;

  float idealTemperature = 0.55f; // 0=cold, 1=hot
  float temperatureSigma = 0.25f;

  // --- Biome bias (0..1 base fertility per biome) ---
  // Tune freely; these are sane defaults for a colony sim.
  float biomeOcean      = 0.0f;
  float biomeBeach      = 0.35f;
  float biomeDesert     = 0.08f;
  float biomeGrassland  = 0.95f;
  float biomeForest     = 0.75f;
  float biomeRainforest = 0.70f;
  float biomeSavanna    = 0.65f;
  float biomeTaiga      = 0.45f;
  float biomeTundra     = 0.18f;
  float biomeSnow       = 0.05f;
  float biomeMountain   = 0.10f;

  // --- Weighted blend (all inputs are 0..1 factors) ---
  float wBiome       = 0.35f;
  float wMoisture    = 0.25f;
  float wTemperature = 0.10f;
  float wSlope       = 0.20f;
  float wWater       = 0.10f;
  float wElevation   = 0.10f;

  // --- Optional smoothing to reduce speckle ---
  int smoothIters = 1; // 0 = off, 1..3 typical
};

namespace detail {

inline float clamp01(float v) {
  return std::clamp(v, 0.0f, 1.0f);
}

inline bool inb(int x, int y, int w, int h) {
  return (x >= 0 && y >= 0 && x < w && y < h);
}

inline int idx(int x, int y, int w) {
  return y * w + x;
}

inline float gauss_pref(float x, float mu, float sigma) {
  sigma = std::max(1e-6f, sigma);
  const float z = (x - mu) / sigma;
  return std::exp(-0.5f * z * z);
}

inline float max_neighbor_delta4(const std::vector<float>& h01, int w, int h, int x, int y) {
  const float e = h01[(size_t)idx(x, y, w)];
  float m = 0.0f;
  if (x > 0)       m = std::max(m, std::abs(e - h01[(size_t)idx(x - 1, y, w)]));
  if (x + 1 < w)   m = std::max(m, std::abs(e - h01[(size_t)idx(x + 1, y, w)]));
  if (y > 0)       m = std::max(m, std::abs(e - h01[(size_t)idx(x, y - 1, w)]));
  if (y + 1 < h)   m = std::max(m, std::abs(e - h01[(size_t)idx(x, y + 1, w)]));
  return m;
}

// Multi-source BFS distance-to-water (4-neighborhood).
inline std::vector<int> distance_to_water4(const std::vector<float>& height01, int w, int h, float seaLevel) {
  const int N = w * h;
  constexpr int INF = std::numeric_limits<int>::max();

  std::vector<int> dist((size_t)N, INF);
  std::deque<int> q;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int i = idx(x, y, w);
      if (height01[(size_t)i] <= seaLevel) {
        dist[(size_t)i] = 0;
        q.push_back(i);
      }
    }
  }

  if (q.empty()) return dist;

  static const int DX[4] = { -1, 1, 0, 0 };
  static const int DY[4] = { 0, 0, -1, 1 };

  while (!q.empty()) {
    const int v = q.front();
    q.pop_front();

    const int vx = v % w;
    const int vy = v / w;
    const int vd = dist[(size_t)v];

    for (int k = 0; k < 4; ++k) {
      const int nx = vx + DX[k];
      const int ny = vy + DY[k];
      if (!inb(nx, ny, w, h)) continue;

      const int ni = idx(nx, ny, w);
      if (dist[(size_t)ni] > vd + 1) {
        dist[(size_t)ni] = vd + 1;
        q.push_back(ni);
      }
    }
  }

  return dist;
}

inline float biome_base_fertility(Biome b, const FertilityParams& p) {
  switch (b) {
    case Biome::Ocean:      return p.biomeOcean;
    case Biome::Beach:      return p.biomeBeach;
    case Biome::Desert:     return p.biomeDesert;
    case Biome::Grassland:  return p.biomeGrassland;
    case Biome::Forest:     return p.biomeForest;
    case Biome::Rainforest: return p.biomeRainforest;
    case Biome::Savanna:    return p.biomeSavanna;
    case Biome::Taiga:      return p.biomeTaiga;
    case Biome::Tundra:     return p.biomeTundra;
    case Biome::Snow:       return p.biomeSnow;
    case Biome::Mountain:   return p.biomeMountain;
    default:                return 0.5f;
  }
}

inline void box_blur3x3(std::vector<float>& dst, const std::vector<float>& src, int w, int h) {
  const size_t N = (size_t)w * (size_t)h;
  dst.assign(N, 0.0f);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float sum = 0.0f;
      int cnt = 0;

      for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
          const int nx = x + ox;
          const int ny = y + oy;
          if (!inb(nx, ny, w, h)) continue;
          sum += src[(size_t)idx(nx, ny, w)];
          cnt++;
        }
      }

      dst[(size_t)idx(x, y, w)] = (cnt > 0) ? (sum / float(cnt)) : src[(size_t)idx(x, y, w)];
    }
  }
}

} // namespace detail

// -----------------------------------------------------------------------------
// Core API: compute fertility01 from raw layers.
// Any optional layer pointer may be null or the wrong size; defaults are used.
// -----------------------------------------------------------------------------
inline std::vector<float> computeFertility01(
    const std::vector<float>& height01,
    int w,
    int h,
    const FertilityParams& p = {},
    const std::vector<float>* moisture01 = nullptr,
    const std::vector<float>* temperature01 = nullptr,
    const std::vector<Biome>* biomes = nullptr)
{
  std::vector<float> out;
  if (w <= 1 || h <= 1) return out;

  const size_t N = (size_t)w * (size_t)h;
  if (height01.size() != N) return out;

  const bool haveMoist = moisture01 && moisture01->size() == N;
  const bool haveTemp  = temperature01 && temperature01->size() == N;
  const bool haveBiome = biomes && biomes->size() == N;

  // Precompute distance-to-water once.
  const std::vector<int> d2w = detail::distance_to_water4(height01, w, h, p.seaLevel);

  out.assign(N, 0.0f);

  const float wsum =
      std::max(1e-6f,
        p.wBiome + p.wMoisture + p.wTemperature + p.wSlope + p.wWater + p.wElevation);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int i = detail::idx(x, y, w);

      const float h01 = height01[(size_t)i];
      if (h01 <= p.seaLevel) {
        out[(size_t)i] = 0.0f;
        continue;
      }

      const float m01 = haveMoist ? (*moisture01)[(size_t)i] : 0.5f;
      const float t01 = haveTemp  ? (*temperature01)[(size_t)i] : 0.5f;
      const Biome b   = haveBiome ? (*biomes)[(size_t)i] : Biome::Grassland;

      // --- Factors (each 0..1) ---
      const float biomeF = detail::clamp01(detail::biome_base_fertility(b, p));

      // Moisture: peak around idealMoisture
      const float moistF = detail::gauss_pref(detail::clamp01(m01), p.idealMoisture, p.moistureSigma);

      // Temperature: peak around idealTemperature
      const float tempF  = detail::gauss_pref(detail::clamp01(t01), p.idealTemperature, p.temperatureSigma);

      // Slope: penalize steep terrain
      const float slope = detail::max_neighbor_delta4(height01, w, h, x, y);
      const float slope01 = detail::clamp01(slope / std::max(1e-6f, p.slopeFull));
      const float slopeF = 1.0f - slope01;

      // Water distance: prefer an "ideal ring", but avoid shoreline flooding
      float waterF = 0.5f; // fallback if there is no water
      const int d = d2w[(size_t)i];
      if (d != std::numeric_limits<int>::max()) {
        if (d < p.minWaterDist) {
          waterF = 0.0f;
        } else {
          waterF = detail::gauss_pref(float(d), float(p.idealWaterDist), p.waterSigma);
          if (p.maxWaterDist > 0 && d > p.maxWaterDist) {
            waterF *= detail::clamp01(p.farFromWaterMultiplier);
          }
        }
      }

      // Elevation: reduce fertility for high hills/mountains (but not a hard clamp)
      float elevF = 1.0f;
      if (h01 >= p.mountainStart) {
        elevF = 0.10f;
      } else if (h01 >= p.hillStart && p.mountainStart > p.hillStart) {
        const float t = (h01 - p.hillStart) / (p.mountainStart - p.hillStart);
        elevF = 1.0f - 0.60f * detail::clamp01(t);
      }

      // Weighted blend, clamped to 0..1
      const float score =
          (p.wBiome       * biomeF +
           p.wMoisture    * moistF +
           p.wTemperature * tempF +
           p.wSlope       * slopeF +
           p.wWater       * waterF +
           p.wElevation   * elevF) / wsum;

      out[(size_t)i] = detail::clamp01(score);
    }
  }

  // Optional smoothing
  if (p.smoothIters > 0) {
    std::vector<float> tmp;
    for (int it = 0; it < p.smoothIters; ++it) {
      detail::box_blur3x3(tmp, out, w, h);
      out.swap(tmp);
    }
    for (float& v : out) v = detail::clamp01(v);
  }

  return out;
}

// Convenience overload for existing procgen::WorldData
inline std::vector<float> computeFertility01(const WorldData& w, FertilityParams p = {}) {
  // If you want fertility to match a specific world sea-level, pass it in p.
  // WorldData doesn't store seaLevel, so we leave p.seaLevel as provided.
  return computeFertility01(
      w.height, w.w, w.h, p,
      &w.moisture, &w.temperature, &w.biome);
}

// Optional helper: grayscale preview (RGBA8) for quick debug visualization.
inline std::vector<std::uint8_t> makeFertilityPreviewRGBA(const std::vector<float>& fert01, int w, int h) {
  std::vector<std::uint8_t> img;
  if (w <= 0 || h <= 0) return img;
  const size_t N = (size_t)w * (size_t)h;
  if (fert01.size() != N) return img;

  img.resize(N * 4);
  for (size_t i = 0; i < N; ++i) {
    const float f = detail::clamp01(fert01[i]);
    const std::uint8_t c = (std::uint8_t)std::lround(f * 255.0f);
    img[i * 4 + 0] = c;
    img[i * 4 + 1] = c;
    img[i * 4 + 2] = c;
    img[i * 4 + 3] = 255;
  }
  return img;
}

} // namespace procgen
