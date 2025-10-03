#pragma once
#include "GridTypes.h"

namespace colony::nav {

// Minimal adapter to your map/tile data.
// Implement this around your existing map to route passability and optional per-cell cost.
struct IGridMap {
    virtual ~IGridMap() = default;
    virtual int32_t Width()  const = 0;
    virtual int32_t Height() const = 0;
    // Whether a tile can be stood on / traversed.
    virtual bool IsPassable(int32_t x, int32_t y) const = 0;

    // Optional: additional traversal cost for entering (x,y). Default 0.
    // Algorithms will add straight/diagonal base costs.
    virtual float ExtraCost(int32_t, int32_t) const { return 0.0f; }
};

inline bool InBounds(const IGridMap& m, int32_t x, int32_t y) {
    return x >= 0 && y >= 0 && x < m.Width() && y < m.Height();
}

} // namespace colony::nav
