#pragma once
#include "GridTypes.hpp"
#include <string_view>
#include <random>

namespace colony::pf {

// Simple grid with 8-direction movement, no corner-cutting.
// Each cell has: 0 = blocked, 1 = free; optional extra cost multiplier (>=1).
class GridMap {
public:
    GridMap() = default;
    GridMap(int w, int h) : _b{w, h}, _walkable(static_cast<size_t>(w*h), 1), _cost(static_cast<size_t>(w*h), 1.0f) {}

    [[nodiscard]] const Bounds& bounds() const noexcept { return _b; }
    [[nodiscard]] int width()  const noexcept { return _b.w; }
    [[nodiscard]] int height() const noexcept { return _b.h; }

    // 0 = blocked, 1 = walkable
    void set_walkable(int x, int y, u8 v) { _walkable[to_id(x,y,_b.w)] = v; }
    [[nodiscard]] u8  walkable(int x, int y) const { return _walkable[to_id(x,y,_b.w)]; }

    // Per-tile additional multiplier cost (>=1). 1.0 = normal.
    void set_tile_cost(int x, int y, float mul) { _cost[to_id(x,y,_b.w)] = mul; }
    [[nodiscard]] float tile_cost(int x, int y) const { return _cost[to_id(x,y,_b.w)]; }

    [[nodiscard]] bool passable(int x, int y) const {
        return _b.contains(x,y) && _walkable[to_id(x,y,_b.w)] != 0;
    }

    // Diagonal step (x+dx,y+dy) is allowed only if it doesn't cut a corner.
    [[nodiscard]] bool can_step(int x, int y, int dx, int dy) const {
        const int nx = x + dx, ny = y + dy;
        if (!passable(nx, ny)) return false;
        if (dx != 0 && dy != 0) {
            // no corner-cutting: both orthogonal neighbors must be free
            if (!passable(x + dx, y) || !passable(x, y + dy)) return false;
        }
        return true;
    }

    // Movement cost for a single step (cardinal=1, diagonal=√2) times per-tile multiplier.
    [[nodiscard]] float step_cost(int x, int y, int dx, int dy) const {
        const float base = (dx == 0 || dy == 0) ? 1.0f : 1.41421356237f;
        return base * tile_cost(x+dx, y+dy);
    }

private:
    Bounds _b{};
    std::vector<u8>    _walkable;
    std::vector<float> _cost;
};

} // namespace colony::pf
