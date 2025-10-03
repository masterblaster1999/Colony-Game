#pragma once
#include <optional>
#include "IGridMap.h"
#include "Heuristics.h"

namespace colony::nav {

struct AStarOptions {
    DiagonalPolicy diagonals = DiagonalPolicy::AllowedIfNoCut;
};

std::optional<Path> FindPathAStar(
    const IGridMap& map,
    const Coord& start,
    const Coord& goal,
    const AStarOptions& opt = {});

} // namespace colony::nav
