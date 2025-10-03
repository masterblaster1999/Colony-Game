#pragma once
#include <optional>
#include "IGridMap.h"
#include "HPACluster.h"
#include "ASTAR.h"

namespace colony::nav {

// Compile-time switch (default ON) in CMake: COLONY_NAV_ENABLE_HPAJPS
// If OFF, Navigator will route to A* directly.
class Navigator {
public:
    struct Options {
        ClusterGridSettings cluster{32,32, DiagonalPolicy::AllowedIfNoCut, 4};
        AStarOptions astar{};
        bool useHPAJPS = true; // overridden by compile flag
    };

    explicit Navigator(const IGridMap& map, Options opt = {})
        : map_(map), opt_(opt), cluster_(map, opt.cluster) {}

    std::optional<Path> FindPath(const Coord& start, const Coord& goal) const;

private:
    const IGridMap& map_;
    Options opt_;
    mutable ClusterGrid cluster_;
};

} // namespace colony::nav
