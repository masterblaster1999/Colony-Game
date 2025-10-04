#include "Pathfinding.h"

namespace sim {

Path Pathfinder::FindPath(const Cell& start, const Cell& goal) {
    // Stub: straight-line fallback
    Path p; p.push_back(start); p.push_back(goal); return p;
}

} // namespace sim
