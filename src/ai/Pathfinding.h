#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace colony::ai {

struct Point {
    int x{};
    int y{};
};

inline bool operator==(const Point& a, const Point& b) noexcept {
    return a.x == b.x && a.y == b.y;
}

// Hash so Point works in unordered_map
struct PointHash {
    std::size_t operator()(const Point& p) const noexcept {
        // Simple, fast hash good enough for grid coords
        return (static_cast<std::size_t>(static_cast<uint32_t>(p.x)) << 32)
             ^ static_cast<std::size_t>(static_cast<uint32_t>(p.y));
    }
};

using Path = std::vector<Point>;

// Minimal grid interface to decouple pathfinder from your map representation.
// You can implement this over your tilemap now or later.
struct IGrid {
    virtual ~IGrid() = default;
    virtual bool isWalkable(int x, int y) const = 0;
    virtual int  width()  const = 0;
    virtual int  height() const = 0;
};

// A* on 4-connected grid (no diagonals).
Path aStar(const IGrid& grid, Point start, Point goal);

} // namespace colony::ai
