#pragma once
/*
    Heuristics.hpp — Colony Game AI Utility Toolkit
    ------------------------------------------------
    Drop-in, header-only heuristics library for colony-sim decision making:
    - Spatial scoring (distance/octile/path cost approximations)
    - Job/resource utility functions with urgency, risk, ROI, and decay
    - Build site & placement scoring (access, flatness, proximity, overlap)
    - Combat target selection and threat evaluation
    - Exploration/frontier scoring with information gain proxy
    - Multi-armed bandit (UCB1/Softmax) task selection
    - TTL caching for repeated queries + deterministic RNG
    - Lightweight scoped profiling & score breakdown diagnostics

    Dependencies: C++17 STL only.

    Integration: Provide a lightweight "world adapter" (duck-typed) that may
    define any of the following optional members used by template overloads:

      double terrain_cost(int x, int y) const;
      bool   is_walkable(int x, int y) const;
      bool   is_dangerous(int x, int y) const;
      double cover_at(int x, int y) const;         // 0..1
      double visibility_gain(int x, int y) const;  // tiles revealed proxy
      double congestion(int x, int y) const;       // 0..1
      int    width() const;  int height() const;

    The templates will detect availability at compile-time (SFINAE) and fall
    back to safe defaults when a method is missing.

    Feature toggles (define before including this header):
      - AI_HEURISTICS_ENABLE_CACHE        // enable TTL cache
      - AI_HEURISTICS_ENABLE_PROFILING    // enable ScopedTimer/records
      - AI_HEURISTICS_THREADSAFE          // add shared_mutex guards
      - AI_HEURISTICS_STRICT_ASSERTS      // enable asserts
      - AI_HEURISTICS_USE_64BIT_RNG       // RNG uses 64-bit core (default)

    Author: (you/your team)
    License: Use the repository’s existing license.
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if __cplusplus >= 202002L
  #include <string_view>
#endif

// ------------------------------ Utilities ------------------------------------

#ifndef AI_HEURISTICS_ASSERT
  #if defined(AI_HEURISTICS_STRICT_ASSERTS)
    #include <cassert>
    #define AI_HEURISTICS_ASSERT(x) assert(x)
  #else
    #define AI_HEURISTICS_ASSERT(x) ((void)0)
  #endif
#endif

#if !defined(AI_HEURISTICS_CACHE_TTL_MS)
  #define AI_HEURISTICS_CACHE_TTL_MS 250 // default cache TTL
#endif

#if !defined(AI_HEURISTICS_CACHE_MAX_ITEMS)
  #define AI_HEURISTICS_CACHE_MAX_ITEMS 4096
#endif

namespace colony::ai {

// Version/feature flags for telemetry
struct Version {
  static constexpr int major = 2;
  static constexpr int minor = 1;
  static constexpr int patch = 0;
};

// Time aliases
using Clock      = std::chrono::steady_clock;
using TimePoint  = Clock::time_point;
using Duration   = Clock::duration;
using Seconds    = std::chrono::duration<double>;
using Millis     = std::chrono::milliseconds;

// Numeric type
using Score = double;

// Small helpers
namespace detail {
  template <typename T>
  inline T clamp(T v, T lo, T hi) noexcept {
    return std::max(lo, std::min(hi, v));
  }

  inline Score logistic(Score x, Score k = 1.0, Score x0 = 0.0) noexcept {
    // 1 / (1 + e^(-k(x-x0)))
    return 1.0 / (1.0 + std::exp(-k * (x - x0)));
  }

  inline Score smoothstep(Score edge0, Score edge1, Score x) noexcept {
    x = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
  }

  inline Score exp_decay(Score age_seconds, Score half_life_seconds) noexcept {
    if (half_life_seconds <= 0) return 1.0;
    const Score lambda = std::log(2.0) / half_life_seconds;
    return std::exp(-lambda * age_seconds);
  }

  inline Score inv_safe(Score x, Score eps = 1e-9) noexcept {
    return 1.0 / std::max(std::abs(x), eps);
  }

  // Simple hashing for composite keys
  inline std::size_t hash_combine(std::size_t h, std::size_t k) noexcept {
    // 64-bit mix
    h ^= k + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }

  // XorShift RNG (deterministic, header-only). Use 64-bit core by default.
  struct XorShift {
  #if defined(AI_HEURISTICS_USE_64BIT_RNG)
    using state_t = std::uint64_t;
    state_t s{0x9E3779B97F4A7C15ULL};
    explicit XorShift(state_t seed = 0xDEADBEEFCAFEBABEULL) noexcept : s(seed ? seed : 0x1ULL) {}
    inline state_t next() noexcept {
      state_t x = s;
      x ^= x << 7;
      x ^= x >> 9;
      x ^= x << 8;
      return s = x ? x : 0x1ULL;
    }
    inline double uniform01() noexcept { return (next() >> 11) * (1.0 / 9007199254740992.0); } // 53-bit
  #else
    using state_t = std::uint32_t;
    state_t s{0x9E3779B9u};
    explicit XorShift(state_t seed = 0xA3C59AC3u) noexcept : s(seed ? seed : 0x1u) {}
    inline state_t next() noexcept {
      state_t x = s;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      return s = x ? x : 0x1u;
    }
    inline double uniform01() noexcept { return next() * (1.0 / 4294967296.0); }
  #endif
  };

  // SFINAE detection idiom (C++17)
  template <typename, typename = void> struct has_terrain_cost : std::false_type {};
  template <typename T> struct has_terrain_cost<T, std::void_t<
      decltype(std::declval<const T&>().terrain_cost(0,0))
  >> : std::true_type {};

  template <typename, typename = void> struct has_is_walkable : std::false_type {};
  template <typename T> struct has_is_walkable<T, std::void_t<
      decltype(std::declval<const T&>().is_walkable(0,0))
  >> : std::true_type {};

  template <typename, typename = void> struct has_is_dangerous : std::false_type {};
  template <typename T> struct has_is_dangerous<T, std::void_t<
      decltype(std::declval<const T&>().is_dangerous(0,0))
  >> : std::true_type {};

  template <typename, typename = void> struct has_cover_at : std::false_type {};
  template <typename T> struct has_cover_at<T, std::void_t<
      decltype(std::declval<const T&>().cover_at(0,0))
  >> : std::true_type {};

  template <typename, typename = void> struct has_visibility_gain : std::false_type {};
  template <typename T> struct has_visibility_gain<T, std::void_t<
      decltype(std::declval<const T&>().visibility_gain(0,0))
  >> : std::true_type {};

  template <typename, typename = void> struct has_congestion : std::false_type {};
  template <typename T> struct has_congestion<T, std::void_t<
      decltype(std::declval<const T&>().congestion(0,0))
  >> : std::true_type {};
} // namespace detail

// ------------------------------- Profiling -----------------------------------

#if defined(AI_HEURISTICS_ENABLE_PROFILING)
struct ScopedTimer {
  const char* label;
  TimePoint   t0;
  Seconds*    out;
  explicit ScopedTimer(const char* lbl, Seconds* dst) : label(lbl), t0(Clock::now()), out(dst) {}
  ~ScopedTimer() {
    if (out) *out += std::chrono::duration_cast<Seconds>(Clock::now() - t0);
  }
};
#else
struct ScopedTimer {
  explicit ScopedTimer(const char*, Seconds*) {}
};
#endif

// ------------------------------- TTL Cache -----------------------------------

#if defined(AI_HEURISTICS_ENABLE_CACHE)

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class TTLCache {
public:
  struct Entry { Value value; TimePoint expiry; };
  explicit TTLCache(std::size_t capacity = AI_HEURISTICS_CACHE_MAX_ITEMS,
                    Millis ttl = Millis(AI_HEURISTICS_CACHE_TTL_MS))
      : capacity_(capacity), ttl_(ttl) {}

  void set_ttl(Millis ttl) noexcept { ttl_ = ttl; }
  void set_capacity(std::size_t cap) noexcept { capacity_ = std::max<std::size_t>(16, cap); }

  std::optional<Value> get(const Key& k) {
  #if defined(AI_HEURISTICS_THREADSAFE)
    std::shared_lock<std::shared_mutex> lk(m_);
  #endif
    auto it = map_.find(k);
    if (it == map_.end()) return std::nullopt;
    if (Clock::now() > it->second.expiry) { map_.erase(it); return std::nullopt; }
    return it->second.value;
  }

  void put(const Key& k, Value v) {
  #if defined(AI_HEURISTICS_THREADSAFE)
    std::unique_lock<std::shared_mutex> lk(m_);
  #endif
    if (map_.size() >= capacity_) {
      // Simple random eviction (fast and cache-friendly enough for heuristics)
      // More elaborate LRU would need extra bookkeeping.
      auto it = map_.begin();
      std::advance(it, rng_index_(map_.size()));
      map_.erase(it);
    }
    map_[k] = Entry{std::move(v), Clock::now() + ttl_};
  }

  void clear() {
  #if defined(AI_HEURISTICS_THREADSAFE)
    std::unique_lock<std::shared_mutex> lk(m_);
  #endif
    map_.clear();
  }

private:
  std::size_t capacity_;
  Millis ttl_;
  std::unordered_map<Key, Entry, Hash> map_;
#if defined(AI_HEURISTICS_THREADSAFE)
  std::shared_mutex m_;
#endif

  // a tiny deterministic index RNG for eviction
  std::size_t rng_index_(std::size_t n) const noexcept {
    static detail::XorShift xs(0xDA1CE5EED123457ULL);
    const double u = xs.uniform01();
    return static_cast<std::size_t>(u * static_cast<double>(n ? n : 1));
  }
};

#endif // AI_HEURISTICS_ENABLE_CACHE

// ------------------------------- Weights -------------------------------------

struct DistanceWeights {
  Score manhattan   = 1.0;     // base per-tile cost
  Score diagonal    = 1.41421356237; // diagonal cost for octile
  Score hazard_mul  = 4.0;     // scales hazard penalty along route
  Score congestion  = 0.5;     // crowds/collisions penalty
  Score cutoff_bias = 0.0;     // extra to bias toward closer tasks
};

struct JobWeights {
  Score value       = 1.0;   // intrinsic value weight
  Score urgency     = 1.0;   // deadline/timer urgency
  Score distance    = 1.0;   // path cost penalty
  Score risk        = 1.0;   // danger in route/site
  Score skill_match = 0.5;   // prefer skilled pawns
  Score roi         = 0.8;   // value/time
  Score freshness   = 0.5;   // time-decay toward 0
  Score cluster     = 0.3;   // batched/proximity bonus
  Score blocking    = 1.0;   // penalty if blocks other tasks
};

struct ResourceWeights {
  Score intrinsic   = 1.0;
  Score scarcity    = 0.6;
  Score perish      = 0.9;
  Score risk        = 0.8;
  Score distance    = 0.8;
  Score extraction  = 0.7;   // time to mine/chop/etc
  Score hauling     = 0.6;   // carry distance
};

struct CombatWeights {
  Score threat      = 1.0;   // enemy DPS*reach
  Score focus_fire  = 0.6;   // stacking damage on low EHP
  Score distance    = 0.5;   // prefer closer targets
  Score cover       = 0.7;   // shoot targets with worse cover
  Score danger      = 0.9;   // avoid stepping into danger
};

struct BuildWeights {
  Score flatness    = 0.8;
  Score access      = 1.0;   // reachable w/ low cost
  Score proximity   = 0.7;   // to base/stockpiles
  Score hazards     = 1.0;   // avoid danger tiles
  Score overlap     = 1.0;   // penalize overlapping plans
  Score future_room = 0.4;   // “breathing room”
};

struct ExploreWeights {
  Score info_gain   = 1.0;   // visibility gain proxy
  Score distance    = 0.6;
  Score danger      = 0.9;
  Score frontier    = 0.7;   // prefer expanding frontiers
};

struct NeedWeights {
  Score hunger      = 1.0;
  Score rest        = 1.0;
  Score mood        = 0.8;
  Score hygiene     = 0.3;
};

struct Weights {
  DistanceWeights distance;
  JobWeights      job;
  ResourceWeights resource;
  CombatWeights   combat;
  BuildWeights    build;
  ExploreWeights  explore;
  NeedWeights     need;
};

// --------------------------- Score breakdown ---------------------------------

struct Term {
  const char* name;
  Score       value;
  Score       weight;
};

struct Breakdown {
  Score total = 0;
  std::vector<Term> terms;

  void add(const char* name, Score v, Score w) {
    terms.push_back({name, v, w});
    total += v * w;
  }
  std::string to_string(int precision = 3) const {
    std::ostringstream os;
    os.setf(std::ios::fixed); os.precision(precision);
    os << "Score = " << total << " { ";
    for (std::size_t i=0;i<terms.size();++i) {
      os << terms[i].name << ":" << (terms[i].value*terms[i].weight);
      if (i + 1 < terms.size()) os << ", ";
    }
    os << " }";
    return os.str();
  }
};

// --------------------------- Spatial primitives ------------------------------

inline int manhattan(int x1, int y1, int x2, int y2) noexcept {
  return std::abs(x1-x2) + std::abs(y1-y2);
}

inline Score euclidean(int x1, int y1, int x2, int y2) noexcept {
  const Score dx = static_cast<Score>(x1 - x2);
  const Score dy = static_cast<Score>(y1 - y2);
  return std::sqrt(dx*dx + dy*dy);
}

// Octile distance (A* admissible for 8-connected grids)
inline Score octile(int x1, int y1, int x2, int y2,
                    Score D=1.0, Score D2=1.41421356237) noexcept {
  Score dx = std::abs(x1-x2);
  Score dy = std::abs(y1-y2);
  return D*(dx+dy) + (D2 - 2.0*D)*std::min(dx, dy);
}

// Approximate path cost by sampling along the octile ray.
// World adapter is optional; if absent, defaults are used.
template <typename World>
inline Score approx_path_cost(const World& world,
                              int x1, int y1, int x2, int y2,
                              const DistanceWeights& w = {}) {
  const Score base = octile(x1,y1,x2,y2, w.manhattan, w.diagonal);
  const int steps = std::max(1, manhattan(x1,y1,x2,y2));
  Score hazard_pen = 0.0, congest_pen = 0.0, terr_accum = 0.0;

  // Parametric sampling [0..1]
  for (int i = 0; i <= std::min(steps, 64); ++i) {
    const Score t  = (steps == 0) ? 0.0 : static_cast<Score>(i) / steps;
    const int sx   = x1 + static_cast<int>(std::round((x2 - x1) * t));
    const int sy   = y1 + static_cast<int>(std::round((y2 - y1) * t));

    if constexpr (detail::has_is_walkable<World>::value) {
      if (!world.is_walkable(sx, sy)) return std::numeric_limits<Score>::infinity();
    }
    if constexpr (detail::has_terrain_cost<World>::value) {
      terr_accum += world.terrain_cost(sx, sy);
    } else {
      terr_accum += 1.0;
    }
    if constexpr (detail::has_is_dangerous<World>::value) {
      hazard_pen += world.is_dangerous(sx, sy) ? 1.0 : 0.0;
    }
    if constexpr (detail::has_congestion<World>::value) {
      congest_pen += world.congestion(sx, sy);
    }
  }

  const Score n = static_cast<Score>(std::min(steps, 64) + 1);
  const Score terr = terr_accum / n;
  const Score hazard = hazard_pen / n;
  const Score crowd  = congest_pen / n;

  // base path cost * avg terrain + penalties
  return base * terr * (1.0 + w.hazard_mul * hazard + w.congestion * crowd) + w.cutoff_bias;
}

// Fallback when caller has no adapter type; uses octile
inline Score approx_path_cost(int x1, int y1, int x2, int y2,
                              const DistanceWeights& w = {}) {
  return octile(x1,y1,x2,y2, w.manhattan, w.diagonal) + w.cutoff_bias;
}

// --------------------------- Job / Resource ----------------------------------

struct JobFeatures {
  // universal
  Score intrinsic_value   = 0.0;  // base benefit (money/progress)
  Score urgency           = 0.0;  // 0..1
  Score path_cost         = 0.0;  // tiles or seconds
  Score risk              = 0.0;  // 0..1 along route/site
  Score skill_match       = 0.0;  // 0..1 (pawn skill normalized)
  Score time_required     = 1.0;  // seconds to complete (>= eps)
  Score cluster_bonus     = 0.0;  // nearby similar tasks
  Score freshness_age_s   = 0.0;  // seconds since created
  Score half_life_s       = 60.0; // decay half-life
  bool  blocks_others     = false;
};

inline Breakdown score_job(const JobFeatures& jf, const Weights& W = {}) {
  Breakdown b;
  const Score roi = (jf.intrinsic_value) * detail::inv_safe(jf.time_required);
  const Score fresh = detail::exp_decay(jf.freshness_age_s, jf.half_life_s);

  b.add("value",    jf.intrinsic_value, W.job.value);
  b.add("urgency",  jf.urgency,         W.job.urgency);
  b.add("distance",-jf.path_cost,       W.job.distance);
  b.add("risk",    -jf.risk,            W.job.risk);
  b.add("skill",    jf.skill_match,     W.job.skill_match);
  b.add("roi",      roi,                W.job.roi);
  b.add("fresh",    fresh,              W.job.freshness);
  b.add("cluster",  std::sqrt(std::max(0.0, jf.cluster_bonus)), W.job.cluster);
  b.add("blocking", jf.blocks_others ? -1.0 : 0.0, W.job.blocking);
  return b;
}

struct ResourceFeatures {
  Score intrinsic_value   = 0.0;
  Score scarcity          = 0.0;   // 0..1 (rarer -> higher)
  Score perish_s          = 0.0;   // time to spoil; 0 => non-perishable
  Score risk              = 0.0;   // 0..1
  Score travel_cost       = 0.0;   // to resource
  Score extraction_time_s = 0.0;   // gather/mining time
  Score hauling_cost      = 0.0;   // to stockpile
};

inline Breakdown score_resource(const ResourceFeatures& rf, const Weights& W = {}) {
  Breakdown b;
  const Score perish_urg = (rf.perish_s <= 0.0) ? 0.0
                          : detail::exp_decay(/*age*/0.0, /*half-life*/ rf.perish_s * 0.5);
  const Score total_cost = rf.travel_cost + rf.extraction_time_s + rf.hauling_cost;
  const Score roi = rf.intrinsic_value * detail::inv_safe(total_cost);

  b.add("intrinsic", rf.intrinsic_value, W.resource.intrinsic);
  b.add("scarcity",  rf.scarcity,        W.resource.scarcity);
  b.add("perish",    perish_urg,         W.resource.perish);
  b.add("risk",     -rf.risk,            W.resource.risk);
  b.add("dist",     -rf.travel_cost,     W.resource.distance);
  b.add("extract",  -rf.extraction_time_s, W.resource.extraction);
  b.add("hauling",  -rf.hauling_cost,    W.resource.hauling);
  b.add("roi",       roi,                0.5); // light additional push
  return b;
}

// --------------------------- Needs / Utility ---------------------------------

struct NeedState {
  Score hunger   = 0.0; // 0..1 (1 worst)
  Score rest     = 0.0; // 0..1 (1 exhausted)
  Score mood     = 0.0; // 0..1 (1 terrible)
  Score hygiene  = 0.0; // 0..1 (1 very dirty)
};

inline Breakdown score_needs(const NeedState& ns, const Weights& W = {}) {
  Breakdown b;
  b.add("hunger", ns.hunger,  W.need.hunger);
  b.add("rest",   ns.rest,    W.need.rest);
  b.add("mood",   ns.mood,    W.need.mood);
  b.add("hygiene",ns.hygiene, W.need.hygiene);
  return b;
}

// ------------------------- Build placement -----------------------------------

struct BuildContext {
  // You may fill some/all of these as estimates if you don't have a world adapter.
  Score flatness         = 1.0;   // 0..1 (1=flat)
  Score access_path_cost = 0.0;   // path cost from base
  Score proximity_base   = 0.0;   // 0..1 (closer to desired hub)
  Score hazard_here      = 0.0;   // 0..1
  Score overlap_ratio    = 0.0;   // overlaps existing plans 0..1
  Score future_breathe   = 0.0;   // free space 0..1
};

inline Breakdown score_build_site(const BuildContext& bc, const Weights& W = {}) {
  Breakdown b;
  b.add("flat",    bc.flatness,             W.build.flatness);
  b.add("access", -bc.access_path_cost,     W.build.access);
  b.add("prox",    bc.proximity_base,       W.build.proximity);
  b.add("hazard", -bc.hazard_here,          W.build.hazards);
  b.add("overlap",-bc.overlap_ratio,        W.build.overlap);
  b.add("future",  bc.future_breathe,       W.build.future_room);
  return b;
}

// Convenience scoring for build placement using a world adapter and a rectangle footprint.
template <typename World>
inline Breakdown score_build_site_world(const World& world,
                                        int x, int y, int w, int h,
                                        int hub_x, int hub_y,
                                        const Weights& W = {}) {
  BuildContext bc;

  // Flatness proxy: average terrain_cost ~1 means flat; higher -> rough.
  Score terr = 0.0, danger = 0.0, blocked = 0.0;
  int   ntiles = 0;

  for (int yy = 0; yy < h; ++yy) {
    for (int xx = 0; xx < w; ++xx) {
      const int tx = x + xx;
      const int ty = y + yy;
      ++ntiles;

      if constexpr (detail::has_terrain_cost<World>::value) terr += world.terrain_cost(tx, ty);
      else terr += 1.0;

      if constexpr (detail::has_is_dangerous<World>::value) danger += world.is_dangerous(tx, ty) ? 1.0 : 0.0;
      if constexpr (detail::has_is_walkable<World>::value)  blocked += world.is_walkable(tx, ty) ? 0.0 : 1.0;
    }
  }

  terr /= std::max(1, ntiles);
  danger /= std::max(1, ntiles);
  blocked /= std::max(1, ntiles);

  bc.flatness       = detail::clamp(2.0 - terr, 0.0, 1.0); // terrain_cost ~1 -> 1.0 score
  bc.hazard_here    = detail::clamp(danger, 0.0, 1.0);
  bc.overlap_ratio  = detail::clamp(blocked, 0.0, 1.0);

  bc.access_path_cost = approx_path_cost(world, hub_x, hub_y, x + w/2, y + h/2, W.distance);
  bc.proximity_base   = detail::inv_safe(bc.access_path_cost) * 5.0; // map to ~0..1+
  bc.proximity_base   = detail::clamp(bc.proximity_base, 0.0, 1.0);

  // Future room: sample a ring around the footprint for openness
  int ring_n = 0; Score open = 0.0;
  for (int xx = -1; xx <= w; ++xx) {
    for (int dy : {-1, h}) {
      const int tx = x + xx;
      const int ty = y + dy;
      ++ring_n;
      if constexpr (detail::has_is_walkable<World>::value)
        open += world.is_walkable(tx, ty) ? 1.0 : 0.0;
      else open += 1.0;
    }
  }
  for (int yy = 0; yy < h; ++yy) {
    for (int dx : {-1, w}) {
      const int tx = x + dx;
      const int ty = y + yy;
      ++ring_n;
      if constexpr (detail::has_is_walkable<World>::value)
        open += world.is_walkable(tx, ty) ? 1.0 : 0.0;
      else open += 1.0;
    }
  }
  open /= std::max(1, ring_n);
  bc.future_breathe = detail::clamp(open, 0.0, 1.0);

  return score_build_site(bc, W);
}

// --------------------------- Combat / Targeting ------------------------------

struct TargetMetrics {
  Score dps             = 0.0;  // enemy damage per second
  Score reach_tiles     = 1.0;  // melee/ranged reach proxy
  Score ehp             = 1.0;  // effective HP (HP / mitigation)
  Score distance_tiles  = 0.0;  // from our pawn
  Score cover_level     = 0.0;  // 0=no cover, 1=full cover
  Score zone_danger     = 0.0;  // path/environmental danger
  Score focus_damage    = 0.0;  // ally DPS currently applied
};

inline Breakdown score_target(const TargetMetrics& tm, const Weights& W = {}) {
  Breakdown b;
  // Threat proxy: DPS * reach / EHP
  const Score threat = tm.dps * std::max<Score>(1.0, tm.reach_tiles) * detail::inv_safe(tm.ehp);
  const Score dist_pen = -tm.distance_tiles;                // nearer is better
  const Score cover_pen = -tm.cover_level;                  // shoot exposed targets
  const Score danger_pen = -tm.zone_danger;                 // avoid stepping into danger
  const Score focus_bonus = detail::logistic(tm.focus_damage, 0.5); // encourage finish-off

  b.add("threat", threat,       W.combat.threat);
  b.add("focus",  focus_bonus,  W.combat.focus_fire);
  b.add("dist",   dist_pen,     W.combat.distance);
  b.add("cover",  cover_pen,    W.combat.cover);
  b.add("danger", danger_pen,   W.combat.danger);
  return b;
}

// ----------------------------- Exploration ----------------------------------

struct ExplorePoint {
  int   x = 0, y = 0;
  Score frontier = 0.0;   // 0..1 (edge of known/unknown)
  Score zone_danger = 0.0;
};

inline Breakdown score_explore_point(const ExplorePoint& ep, Score dist_tiles,
                                     const Weights& W = {}) {
  Breakdown b;
  // Information gain proxy: frontier + (adapter visibility_gain if available)
  b.add("info",    ep.frontier,   W.explore.info_gain);
  b.add("distance",-dist_tiles,   W.explore.distance);
  b.add("danger", -ep.zone_danger,W.explore.danger);
  b.add("frontier",ep.frontier,   W.explore.frontier); // slight double-count ok
  return b;
}

template <typename World>
inline Breakdown score_explore_world(const World& world, int x, int y,
                                     int from_x, int from_y,
                                     Score frontier, Score zone_danger,
                                     const Weights& W = {}) {
  ExplorePoint ep{ x, y, frontier, zone_danger };
  Score dist = approx_path_cost(world, from_x, from_y, x, y, W.distance);
  Breakdown b = score_explore_point(ep, dist, W);
  if constexpr (detail::has_visibility_gain<World>::value) {
    Breakdown extra;
    extra.add("vis_gain", detail::clamp(world.visibility_gain(x,y), 0.0, 10.0), 0.5);
    b.total += extra.total;
    b.terms.insert(b.terms.end(), extra.terms.begin(), extra.terms.end());
  }
  return b;
}

// ------------------------- Multi-armed Bandit --------------------------------

struct UCB1 {
  // Classic UCB1 with simple tie/stability tweaks
  std::vector<Score> means;
  std::vector<int>   counts;
  int total = 0;
  Score c = 1.41421356237; // exploration constant

  explicit UCB1(int k = 0, Score exploration = 1.41421356237) : means(k,0.0), counts(k,0), total(0), c(exploration) {}

  int select() const {
    AI_HEURISTICS_ASSERT(!means.empty());
    int best = 0; Score best_ucb = -1e100;
    for (int i=0;i<(int)means.size();++i) {
      const int ni = std::max(1, counts[i]);
      const Score bonus = c * std::sqrt(std::log(std::max(1,total)) / ni);
      const Score ucb = means[i] + bonus;
      if (ucb > best_ucb) { best_ucb = ucb; best = i; }
    }
    return best;
  }
  void update(int arm, Score reward) {
    AI_HEURISTICS_ASSERT(arm >= 0 && arm < (int)means.size());
    ++total; ++counts[arm];
    const Score a = 1.0 / counts[arm];
    means[arm] += a * (reward - means[arm]);
  }
};

// Softmax sampler (Boltzmann exploration)
inline int softmax_select(const std::vector<Score>& values,
                          Score temperature = 0.5,
                          detail::XorShift* rng = nullptr) {
  AI_HEURISTICS_ASSERT(!values.empty());
  Score maxv = -1e100;
  for (Score v : values) maxv = std::max(maxv, v);
  Score sum = 0.0;
  std::vector<Score> probs(values.size());
  const Score invT = 1.0 / std::max(temperature, 1e-6);
  for (std::size_t i=0;i<values.size();++i) {
    const Score ex = std::exp((values[i] - maxv) * invT);
    probs[i] = ex;
    sum += ex;
  }
  const Score u = rng ? rng->uniform01() : std::generate_canonical<double, 53>(std::mt19937_64{123});
  Score acc = 0.0;
  for (std::size_t i=0;i<values.size();++i) {
    acc += probs[i] / sum;
    if (u <= acc) return static_cast<int>(i);
  }
  return static_cast<int>(values.size()-1);
}

// --------------------------- Convenience API ---------------------------------

// Unified task choice given a set of breakdown scores.
// Returns the index with maximal total score. Optionally logs best breakdown.
inline int argmax_breakdowns(const std::vector<Breakdown>& bs, Breakdown* best_out = nullptr) {
  AI_HEURISTICS_ASSERT(!bs.empty());
  int best = 0; Score bestS = -1e100;
  for (int i=0;i<(int)bs.size();++i) {
    if (bs[i].total > bestS) { bestS = bs[i].total; best = i; }
  }
  if (best_out) *best_out = bs[best];
  return best;
}

// Distance-only (fast)
inline Score score_distance_only(int x1,int y1,int x2,int y2,const DistanceWeights& W={}) {
  return -octile(x1,y1,x2,y2, W.manhattan, W.diagonal);
}

// Deprecated/compat convenience aliases (if older call sites exist)
inline Score distance_score(int x1,int y1,int x2,int y2) { return score_distance_only(x1,y1,x2,y2); }
inline Score utility_logistic(Score x, Score k=1.0, Score x0=0.0) { return detail::logistic(x,k,x0); }
inline Score time_decay(Score age_s, Score half_life_s) { return detail::exp_decay(age_s, half_life_s); }

// --------------------------- Caching wrappers --------------------------------

#if defined(AI_HEURISTICS_ENABLE_CACHE)

struct PathKey {
  int x1,y1,x2,y2;
  struct Hash {
    std::size_t operator()(const PathKey& k) const noexcept {
      std::size_t h = std::hash<int>{}(k.x1);
      h = detail::hash_combine(h, std::hash<int>{}(k.y1));
      h = detail::hash_combine(h, std::hash<int>{}(k.x2));
      h = detail::hash_combine(h, std::hash<int>{}(k.y2));
      return h;
    }
  };
  bool operator==(const PathKey& o) const noexcept {
    return x1==o.x1 && y1==o.y1 && x2==o.x2 && y2==o.y2;
  }
};

template <typename World>
class PathCostCache {
public:
  explicit PathCostCache(std::size_t cap = AI_HEURISTICS_CACHE_MAX_ITEMS,
                         Millis ttl = Millis(AI_HEURISTICS_CACHE_TTL_MS))
  : cache_(cap, ttl) {}

  Score get_or_compute(const World& world, int x1,int y1,int x2,int y2,
                       const DistanceWeights& W = {}) {
    PathKey key{x1,y1,x2,y2};
    if (auto v = cache_.get(key)) return *v;
    Score value = approx_path_cost(world, x1,y1,x2,y2,W);
    cache_.put(key, value);
    return value;
  }

  void clear() { cache_.clear(); }

private:
  TTLCache<PathKey, Score, PathKey::Hash> cache_;
};

#endif // AI_HEURISTICS_ENABLE_CACHE

// ----------------------------- Tuning profile --------------------------------

struct TuningProfile {
  // Difficulty or style modifiers (multiply weights)
  Score economy_bias  = 1.0; // >1 favors resources/jobs
  Score safety_bias   = 1.0; // >1 favors safety/combat avoidance
  Score expand_bias   = 1.0; // >1 favors exploration/building

  void apply(Weights& W) const {
    W.job.value    *= economy_bias;
    W.resource.intrinsic *= economy_bias;
    W.build.future_room  *= expand_bias;
    W.explore.info_gain  *= expand_bias;
    W.job.risk     *= safety_bias;
    W.resource.risk*= safety_bias;
    W.combat.danger*= safety_bias;
  }
};

// String config loader (simple key=value;key2=value2 parser)
inline bool apply_inline_config(Weights& W, const std::string& cfg) {
  // keys like job.value=1.2;distance.hazard_mul=3.5
  auto set = [&](const std::string& key, Score val)->bool {
    // Distance
    if (key=="distance.manhattan")   { W.distance.manhattan = val; return true; }
    if (key=="distance.diagonal")    { W.distance.diagonal  = val; return true; }
    if (key=="distance.hazard_mul")  { W.distance.hazard_mul= val; return true; }
    if (key=="distance.congestion")  { W.distance.congestion= val; return true; }
    if (key=="distance.cutoff_bias") { W.distance.cutoff_bias=val; return true; }
    // Job
    if (key=="job.value")   { W.job.value = val; return true; }
    if (key=="job.urgency") { W.job.urgency = val; return true; }
    if (key=="job.distance"){ W.job.distance = val; return true; }
    if (key=="job.risk")    { W.job.risk = val; return true; }
    if (key=="job.skill")   { W.job.skill_match = val; return true; }
    if (key=="job.roi")     { W.job.roi = val; return true; }
    if (key=="job.fresh")   { W.job.freshness = val; return true; }
    if (key=="job.cluster") { W.job.cluster = val; return true; }
    if (key=="job.blocking"){ W.job.blocking = val; return true; }
    // Resource
    if (key=="resource.intrinsic"){ W.resource.intrinsic = val; return true; }
    if (key=="resource.scarcity"){  W.resource.scarcity  = val; return true; }
    if (key=="resource.perish"){    W.resource.perish    = val; return true; }
    if (key=="resource.risk"){      W.resource.risk      = val; return true; }
    if (key=="resource.distance"){  W.resource.distance  = val; return true; }
    if (key=="resource.extraction"){W.resource.extraction= val; return true; }
    if (key=="resource.hauling"){   W.resource.hauling   = val; return true; }
    // Combat
    if (key=="combat.threat"){     W.combat.threat = val; return true; }
    if (key=="combat.focus"){      W.combat.focus_fire = val; return true; }
    if (key=="combat.distance"){   W.combat.distance = val; return true; }
    if (key=="combat.cover"){      W.combat.cover = val; return true; }
    if (key=="combat.danger"){     W.combat.danger = val; return true; }
    // Build
    if (key=="build.flatness"){  W.build.flatness = val; return true; }
    if (key=="build.access"){    W.build.access = val; return true; }
    if (key=="build.proximity"){ W.build.proximity = val; return true; }
    if (key=="build.hazards"){   W.build.hazards = val; return true; }
    if (key=="build.overlap"){   W.build.overlap = val; return true; }
    if (key=="build.future"){    W.build.future_room = val; return true; }
    // Explore
    if (key=="explore.info"){    W.explore.info_gain = val; return true; }
    if (key=="explore.distance"){W.explore.distance = val; return true; }
    if (key=="explore.danger"){  W.explore.danger   = val; return true; }
    if (key=="explore.frontier"){W.explore.frontier = val; return true; }
    // Needs
    if (key=="need.hunger"){ W.need.hunger = val; return true; }
    if (key=="need.rest"){   W.need.rest   = val; return true; }
    if (key=="need.mood"){   W.need.mood   = val; return true; }
    if (key=="need.hygiene"){W.need.hygiene= val; return true; }

    return false;
  };

  std::size_t i = 0;
  while (i < cfg.size()) {
    // parse key
    std::size_t eq = cfg.find('=', i);
    if (eq == std::string::npos) break;
    std::string key = cfg.substr(i, eq - i);
    // parse value
    std::size_t sep = cfg.find_first_of(";\n\r\t ", eq + 1);
    std::string val = cfg.substr(eq + 1, sep == std::string::npos ? std::string::npos : sep - (eq + 1));
    try {
      double v = std::stod(val);
      set(key, v);
    } catch (...) { /* ignore parse errors */ }
    if (sep == std::string::npos) break;
    i = sep + 1;
    while (i < cfg.size() && (cfg[i] == ';' || cfg[i] == ' ' || cfg[i] == '\n' || cfg[i] == '\r' || cfg[i] == '\t')) ++i;
  }
  return true;
}

// ------------------------------- Examples ------------------------------------

// Example: choosing among candidate jobs (precomputed path costs)
inline int choose_job_index(const std::vector<JobFeatures>& jobs, const Weights& W,
                            Breakdown* out = nullptr) {
  std::vector<Breakdown> b; b.reserve(jobs.size());
  for (auto& jf : jobs) b.emplace_back(score_job(jf, W));
  return argmax_breakdowns(b, out);
}

// Example: choose combat target index
inline int choose_target_index(const std::vector<TargetMetrics>& ts, const Weights& W,
                               Breakdown* out = nullptr) {
  std::vector<Breakdown> b; b.reserve(ts.size());
  for (auto& t : ts) b.emplace_back(score_target(t, W));
  return argmax_breakdowns(b, out);
}

// Example: combine needs with job score (simple linear blending)
inline Score blended_utility(const Breakdown& jobScore, const Breakdown& needsScore,
                             Score needs_influence = 0.25) {
  // When needs are bad (large), we reduce willingness to work
  const Score needs = std::max<Score>(0.0, needsScore.total);
  return jobScore.total - needs_influence * needs;
}

} // namespace colony::ai

