#pragma once
#include "GridTypes.hpp"
#include <vector>

namespace colony::pf {

struct Path {
    std::vector<IVec2> points;
    [[nodiscard]] bool empty() const noexcept { return points.empty(); }
    [[nodiscard]] size_t length() const noexcept { return points.size(); }
};

inline Path reconstruct(NodeId goal, NodeId start, int w, const std::vector<StepCost>& cost) {
    Path out;
    if (goal == kInvalid) return out;
    NodeId cur = goal;
    while (cur != kInvalid) {
        auto p = from_id(cur, w);
        out.points.push_back(p);
        if (cur == start) break;
        cur = cost[cur].parent;
    }
    std::reverse(out.points.begin(), out.points.end());
    return out;
}

} // namespace colony::pf
