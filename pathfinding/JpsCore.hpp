// pathfinding/JpsCore.hpp
#pragma once
#include <vector>
#include <utility> // std::pair

namespace colony::path {

// Public API types come from Jps.hpp; we only forward-declare here.
struct IGrid;
struct JpsOptions;
struct Cell;

namespace detail {

// ---- Helper declarations (definitions live in Jps.cpp) ----
int   idx(int x, int y, int W);

bool  in_bounds(const IGrid& g, int x, int y);
bool  passable (const IGrid& g, int x, int y);
bool  can_step (const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o);

float heuristic (int x0, int y0, int x1, int y1, const JpsOptions& o);
float dist_cost (int x0, int y0, int x1, int y1, const JpsOptions& o);
float tiebreak  (int x, int y, int sx, int sy, int gx, int gy);

bool  has_forced_neighbors_straight(const IGrid& g, int x, int y, int dx, int dy);
bool  has_forced_neighbors_diag    (const IGrid& g, int x, int y, int dx, int dy);

void  pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                  const JpsOptions& o, std::vector<std::pair<int,int>>& out);

bool  jump(const IGrid& g, int x, int y, int dx, int dy,
           int gx, int gy, const JpsOptions& o, int& outx, int& outy);

bool  los_supercover(const IGrid& g, int x0, int y0, int x1, int y1, const JpsOptions& o);

} // namespace detail
} // namespace colony::path
