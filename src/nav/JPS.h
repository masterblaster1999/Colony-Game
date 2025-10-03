#pragma once
#include <optional>
#include "IGridMap.h"
#include "Heuristics.h"

namespace colony::nav {

struct JPSOptions {
    DiagonalPolicy diagonals = DiagonalPolicy::AllowedIfNoCut;
    // Optional bounding box to restrict search (e.g., current cluster + border).
    // If min==max=={0,0}, treat as unlimited.
    Coord bboxMin{0,0};
    Coord bboxMax{0,0};
    bool hasBBox = false;
};

std::optional<Path> FindPathJPS(
    const IGridMap& map,
    const Coord& start,
    const Coord& goal,
    const JPSOptions& opt = {});

} // namespace colony::nav
