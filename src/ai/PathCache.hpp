// src/ai/PathCache.hpp
#pragma once

#include <unordered_map>
#include <vector>
#include <chrono>
#include <shared_mutex>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <climits>      // INT_MIN
#include <cstdlib>      // std::abs
#include <mutex>        // std::unique_lock

#include "Pathfinding.hpp" // GridView, Point, PFResult, aStar()

namespace colony::ai {

struct PathKey {
    int sx, sy, gx, gy;
    int gridStamp;               // bump when the map changes
    int budget;                  // maxExpandedNodes (-1 = unlimited)

    bool operator==(const PathKey& o) const noexcept {
        return sx==o.sx && sy==o.sy && gx==o.gx && gy==o.gy &&
               gridStamp==o.gridStamp && budget==o.budget;
    }
};

struct PathKeyHash {
    std::size_t operator()(const PathKey& k) const noexcept {
        // 64-bit mix (works fine on 32-bit too)
        auto h = std::size_t(1469598103934665603ULL);
        auto mix = [&](std::size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
        };
        mix(static_cast<std::size_t>(k.sx));
        mix(static_cast<std::size_t>(k.sy));
        mix(static_cast<std::size_t>(k.gx));
        mix(static_cast<std::size_t>(k.gy));
        mix(static_cast<std::size_t>(k.gridStamp));
        mix(static_cast<std::size_t>(k.budget));
        return h;
    }
};

class PathCache {
public:
    using Clock = std::chrono::steady_clock;
    using Millis = std::chrono::milliseconds;

    struct Entry {
        std::vector<Point> path;
        PFResult result{PFResult::NoPath};
        Clock::time_point expiry{};
    };

    explicit PathCache(std::size_t capacity = 4096, Millis ttl = Millis(250))
        : capacity_(capacity), ttl_(ttl), rng_(0xDA1CE5EED123457ULL) {}

    void set_capacity(std::size_t cap) noexcept { capacity_ = std::max<std::size_t>(16, cap); }
    void set_ttl(Millis ttl) noexcept { ttl_ = ttl; }

    void clear() {
        std::unique_lock lk(mu_);
        map_.clear();
    }

    // Optionally keep an internal “world version”
    void bump_grid_stamp() noexcept { ++defaultStamp_; }
    int  current_stamp() const noexcept { return defaultStamp_; }

    // Find or compute a path. If cached and valid, served immediately.
    PFResult find_or_compute(const GridView& g,
                             Point start, Point goal,
                             std::vector<Point>& out,
                             int maxExpandedNodes = -1,
                             int gridStamp = INT_MIN /* INT_MIN => use default */)
    {
        const int stamp = (gridStamp == INT_MIN) ? defaultStamp_ : gridStamp;
        const PathKey key{ start.x, start.y, goal.x, goal.y, stamp, maxExpandedNodes };

        // Fast optimistic read
        {
            std::shared_lock slk(mu_);
            auto it = map_.find(key);
            if (it != map_.end() && !expired_(it->second)) {
                if (validate_(g, it->second.path)) {
                    out = it->second.path;
                    return it->second.result;
                }
            }
        }

        // Compute fresh
        std::vector<Point> path;
        const PFResult r = aStar(g, start, goal, path, maxExpandedNodes);

        // Write-through
        {
            std::unique_lock ulk(mu_);
            if (map_.size() >= capacity_) evict_one_();
            map_[key] = Entry{ std::move(path), r, Clock::now() + ttl_ };
            out = map_[key].path; // path may be empty if NoPath/Aborted
        }
        return r;
    }

private:
    bool expired_(const Entry& e) const noexcept {
        return Clock::now() > e.expiry;
    }

    // Cheap sanity: all points in-bounds + walkable; successive steps are 4-neighbour adjacent.
    // (If you later support diagonals, extend adjacency check accordingly.)
    static bool validate_(const GridView& g, const std::vector<Point>& p) {
        if (p.empty()) return false;
        auto inb = [&](int x, int y){ return g.inBounds(x,y); };
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto& t = p[i];
            if (!inb(t.x, t.y) || !g.walkable(t.x, t.y))
                return false;
            if (i) {
                const auto& a = p[i-1];
                const int dx = std::abs(a.x - t.x);
                const int dy = std::abs(a.y - t.y);
                // 4-neighbour adjacency only; change to (dx<=1 && dy<=1 && (dx|dy)!=0) for 8-neighbour
                if (!((dx == 1 && dy == 0) || (dx == 0 && dy == 1)))
                    return false;
            }
        }
        return true;
    }

    void evict_one_() {
        // Deterministic, simple random eviction (no extra bookkeeping like full LRU)
        if (map_.empty()) return;
        const std::size_t n = map_.size();
        const std::size_t step = (rng_next_() % (n ? n : 1));
        auto it = map_.begin();
        std::advance(it, step);
        map_.erase(it);
    }

    std::uint64_t rng_next_() noexcept {
        // xorshift64*
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 7;
        rng_ ^= rng_ << 17;
        return rng_ ? rng_ : 0x9E3779B97F4A7C15ULL;
    }

private:
    std::unordered_map<PathKey, Entry, PathKeyHash> map_;
    std::size_t   capacity_;
    Millis        ttl_;
    int           defaultStamp_ = 0;
    std::uint64_t rng_;
    mutable std::shared_mutex mu_;
};

} // namespace colony::ai
