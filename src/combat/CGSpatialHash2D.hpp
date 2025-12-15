#pragma once

#include "CGCombatTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace colony::combat {

// Very small spatial hash for 2D gameplay queries (AoE / nearest enemy).
// Not meant to be perfect; it's a pragmatic speedup over O(N) scans.
class SpatialHash2D final {
public:
  SpatialHash2D() = default;
  explicit SpatialHash2D(float cell_size) { set_cell_size(cell_size); }

  void set_cell_size(float cell_size) {
    cell_size_ = std::max(0.01f, cell_size);
    inv_cell_size_ = 1.0f / cell_size_;
  }

  [[nodiscard]] float cell_size() const noexcept { return cell_size_; }

  void clear() { buckets_.clear(); }

  void reserve(std::size_t approx_entity_count) {
    // Heuristic: ~1 bucket per 4 entities, but keep at least a few buckets.
    buckets_.reserve(std::max<std::size_t>(8, approx_entity_count / 4));
  }

  void insert(EntityId id, Vec2 p) {
    const auto [cx, cy] = cell_coords(p);
    buckets_[pack_key(cx, cy)].push_back(id);
  }

  // Query entities that may intersect a circle. Caller is expected to distance-check.
  template <class Fn>
  void query_circle_candidates(Vec2 center, float radius, Fn&& fn) const {
    radius = std::max(0.0f, radius);

    const int min_x = world_to_cell(center.x - radius);
    const int max_x = world_to_cell(center.x + radius);
    const int min_y = world_to_cell(center.y - radius);
    const int max_y = world_to_cell(center.y + radius);

    for (int cy = min_y; cy <= max_y; ++cy) {
      for (int cx = min_x; cx <= max_x; ++cx) {
        const auto it = buckets_.find(pack_key(cx, cy));
        if (it == buckets_.end()) continue;
        for (EntityId id : it->second) {
          fn(id);
        }
      }
    }
  }

private:
  [[nodiscard]] int world_to_cell(float v) const noexcept {
    // floor works for negative coordinates too.
    return static_cast<int>(std::floor(v * inv_cell_size_));
  }

  [[nodiscard]] std::pair<int, int> cell_coords(Vec2 p) const noexcept {
    return {world_to_cell(p.x), world_to_cell(p.y)};
  }

  [[nodiscard]] static std::int64_t pack_key(int cx, int cy) noexcept {
    // Pack two 32-bit signed ints into one 64-bit key.
    const std::uint64_t ux = static_cast<std::uint32_t>(cx);
    const std::uint64_t uy = static_cast<std::uint32_t>(cy);
    return static_cast<std::int64_t>((ux << 32ULL) | uy);
  }

  float cell_size_{2.0f};
  float inv_cell_size_{0.5f};

  std::unordered_map<std::int64_t, std::vector<EntityId>> buckets_{};
};

} // namespace colony::combat
