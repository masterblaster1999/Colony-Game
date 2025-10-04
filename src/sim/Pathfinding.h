#pragma once
#include <vector>
#include <utility>

namespace sim {

using Cell = std::pair<int,int>;
using Path = std::vector<Cell>;

class Pathfinder {
public:
    Path FindPath(const Cell& start, const Cell& goal); // TODO: JPS/HPA*
};

} // namespace sim
