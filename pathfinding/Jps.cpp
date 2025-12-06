// pathfinding/Jps.cpp
#include "pathfinding/JpsCore.hpp"

namespace colony::path {

using namespace detail;

std::vector<Cell> jps_find_path(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
{
    const int W = grid.width();
    const int H = grid.height(); (void)H;

    if (W <= 0) return {};
    if (!passable(grid, start.x, start.y)) return {};
    if (!passable(grid, goal.x, goal.y))   return {};
    if (start.x == goal.x && start.y == goal.y) return {start};

    std::vector<Node> nodes(static_cast<std::size_t>(W) * static_cast<std::size_t>(grid.height()));

    auto push_open = [&](std::priority_queue<PQItem>& open, int i, float f) {
        open.push(PQItem{i, f});
        nodes[i].opened = true;
    };

    // start
    const int sidx = idx(start.x, start.y, W);
    nodes[sidx].x = start.x; nodes[sidx].y = start.y;
    nodes[sidx].g = 0.0f;
    nodes[sidx].f = opt.heuristicWeight * heuristic(start.x, start.y, goal.x, goal.y, opt);
    nodes[sidx].parent = -1; nodes[sidx].px = start.x; nodes[sidx].py = start.y;

    std::priority_queue<PQItem> open;
    push_open(open, sidx, nodes[sidx].f);

    std::vector<std::pair<int,int>> dirs;
    dirs.reserve(8);

    while (!open.empty()) {
        const int curr_i = open.top().index; open.pop();
        Node& n = nodes[curr_i];
        if (n.closed) continue;
        n.closed = true;

        if (curr_i == idx(goal.x, goal.y, W)) {
            auto path = reconstruct_path(nodes, curr_i, W);
            if (opt.smoothPath && path.size() > 2) {
                // greedy string pulling with LOS
                std::vector<Cell> smooth; smooth.push_back(path.front());
                std::size_t j = 1;
                while (j < path.size()) {
                    std::size_t k = j;
                    while (k + 1 < path.size() &&
                           los_supercover(grid, smooth.back().x, smooth.back().y,
                                          path[k+1].x, path[k+1].y, opt)) {
                        ++k;
                    }
                    smooth.push_back(path[k]);
                    j = k + 1;
                }
                return smooth;
            }
            return path;
        }

        // expand via JPS
        const int cx = n.x, cy = n.y;
        pruned_dirs(grid, cx, cy, n.px, n.py, opt, dirs);

        for (auto [dx,dy] : dirs) {
            int jx = 0, jy = 0;
            if (!jump(grid, cx, cy, dx, dy, goal.x, goal.y, opt, jx, jy))
                continue;

            const int ji = idx(jx, jy, W);
            const float tentative_g = n.g + dist_cost(cx, cy, jx, jy, opt);

            if (!nodes[ji].opened || tentative_g < nodes[ji].g) {
                nodes[ji].x = jx; nodes[ji].y = jy;
                nodes[ji].g = tentative_g;
                const float h = heuristic(jx, jy, goal.x, goal.y, opt) * opt.heuristicWeight;
                const float f =
                    tentative_g + h +
                    (opt.tieBreakCross ? tiebreak(jx, jy, start.x, start.y, goal.x, goal.y) : 0.0f);
                nodes[ji].f = f;
                nodes[ji].parent = curr_i;
                nodes[ji].px = cx; nodes[ji].py = cy;
                push_open(open, ji, f);
            }
        }
    }

    return {}; // no path
}

} // namespace colony::path
