#include "Navigator.h"

namespace colony::nav {

std::optional<Path> Navigator::FindPath(const Coord& start, const Coord& goal) const {
#if defined(COLONY_NAV_ENABLE_HPAJPS)
    bool useHPA = opt_.useHPAJPS;
#else
    bool useHPA = false;
#endif
    if (useHPA) {
        auto p = cluster_.FindPath(start, goal);
        if (p) return p; // success
        // Fallback to local JPS
        JPSOptions jopt; jopt.diagonals = opt_.cluster.diagonals;
        p = FindPathJPS(map_, start, goal, jopt);
        if (p) return p;
    }
    // Final fallback to classic A*
    return FindPathAStar(map_, start, goal, opt_.astar);
}

} // namespace colony::nav
