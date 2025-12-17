// pathfinding/JpsCore.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace colony::path {

struct GridView {
    int width  = 0;
    int height = 0;

    // Return true if (x,y) is blocked.
    std::function<bool(int, int)> isBlocked;

    bool inBounds(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < width && y < height;
    }

    bool passable(int x, int y) const noexcept {
        return inBounds(x, y) && !isBlocked(x, y);
    }
};

inline std::uint64_t Pack(int x, int y) noexcept {
    // Pack two 32-bit signed ints into one 64-bit key.
    return (std::uint64_t(static_cast<std::uint32_t>(x)) << 32) |
           std::uint64_t(static_cast<std::uint32_t>(y));
}

// Legacy distance helper (D=1, D2=sqrt(2)).
float Octile(int x0, int y0, int x1, int y1, bool allowDiagonal) noexcept;

// Jump Point Search primitives (legacy + extended dontCrossCorners versions).
std::optional<std::pair<int, int>> Jump(
    const GridView& g, int x, int y, int dx, int dy,
    int goalX, int goalY, bool allowDiagonal);

std::optional<std::pair<int, int>> Jump(
    const GridView& g, int x, int y, int dx, int dy,
    int goalX, int goalY, bool allowDiagonal, bool dontCrossCorners);

void PruneNeighbors(
    const GridView& g, int x, int y, int dx, int dy,
    bool allowDiagonal, std::vector<std::pair<int, int>>& outDirs);

void PruneNeighbors(
    const GridView& g, int x, int y, int dx, int dy,
    bool allowDiagonal, bool dontCrossCorners,
    std::vector<std::pair<int, int>>& outDirs);

// Back-compat pathfinding overloads (existing API).
std::vector<std::pair<int, int>> FindPathJPS(
    const GridView& grid, int sx, int sy, int gx, int gy, bool allowDiagonal);

std::vector<std::pair<int, int>> FindPathJPS(
    const GridView& grid, int sx, int sy, int gx, int gy,
    bool allowDiagonal, bool dontCrossCorners);

// New overload: options actually drive the algorithm.
std::vector<std::pair<int, int>> FindPathJPS(
    const GridView& grid, int sx, int sy, int gx, int gy,
    bool allowDiagonal, bool dontCrossCorners,
    float costStraight, float costDiagonal,
    float heuristicWeight,
    bool tieBreakCross);

} // namespace colony::path
