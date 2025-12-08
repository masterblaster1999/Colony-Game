#pragma once

// Self-contained: pull in the public API so types are COMPLETE here.
#include <pathfinding/Jps.hpp>   // IGrid, Cell, JpsOptions, jps_find_path()

#include <limits>

namespace colony::path {
namespace detail {

// Internal per-cell bookkeeping used by the JPS/A* search.
struct Node {
    int   x = 0, y = 0;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    int   parent = -1;  // index of parent cell, -1 for start
    int   px = 0, py = 0; // parent's coordinates (for pruning directions)
    bool  opened = false;
    bool  closed = false;
};

// Priority-queue item; define comparison so std::priority_queue is a min-heap on f.
struct PQItem {
    int   index = 0;
    float f     = 0.0f;

    // std::priority_queue is a max-heap by default; invert to pop smallest f first.
    bool operator<(const PQItem& other) const noexcept {
        return f > other.f;
    }
};

} // namespace detail
} // namespace colony::path
