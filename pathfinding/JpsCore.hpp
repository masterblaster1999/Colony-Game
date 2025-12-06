// pathfinding/JpsCore.hpp
#pragma once
#include <vector>
#include <utility> // std::pair

namespace colony::path {

// Forward declarations: we only declare helpers here.
struct IGrid;
struct JpsOptions;
struct Cell;

namespace detail {

// Lightweight node entry used by the open/closed sets.
struct Node {
    int   x = 0, y = 0;
    float g = 0.f;     // set in .cpp when used
    float f = 0.f;
    int   parent = -1; // parent index (y*W + x)
    int   px = 0, py = 0;
    bool  opened = false;
    bool  closed = false;
};

struct PQItem {
    int   index = -1;  // y*W + x
    float f     = 0.f;
    // min-heap via reversed comparator
    bool operator<(const PQItem& o) const { return f > o.f; }
};

// Helper API (definitions live in Jps.cpp)
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
