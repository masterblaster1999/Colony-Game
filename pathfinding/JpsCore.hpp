// pathfinding/JpsCore.hpp
#pragma once

// Make this header self-contained: ensure IGrid/JpsOptions/Cell are COMPLETE here.
#if __has_include(<pathfinding/Jps.hpp>)
  #include <pathfinding/Jps.hpp>   // defines colony::path::{IGrid, Cell, JpsOptions}
#elif __has_include("pathfinding/Jps.hpp")
  #include "pathfinding/Jps.hpp"
#elif __has_include("pathfinding/JpsTypes.hpp")
  #include "pathfinding/JpsTypes.hpp"
#elif __has_include(<pathfinding/JpsTypes.hpp>)
  #include <pathfinding/JpsTypes.hpp>
#else
  #error "JpsCore.hpp requires pathfinding/Jps.hpp (or legacy pathfinding/JpsTypes.hpp)."
#endif

#include <vector>
#include <utility>
#include <queue>
#include <limits>
#include <cstddef>

namespace colony::path {
namespace detail {

struct Node {
    int   x = 0, y = 0;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    int   parent = -1;
    int   px = 0,   py = 0;
    bool  opened = false;
    bool  closed = false;
};

struct PQItem {
    int   index = -1;
    float f     = 0.0f;
    bool operator<(const PQItem& rhs) const noexcept { return f > rhs.f; }
};

// ---- Helper declarations (definitions live in Jps.cpp) ----
int   idx(int x, int y, int W);
bool  in_bounds(const IGrid& g, int x, int y);
bool  passable (const IGrid& g, int x, int y);
bool  can_step (const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o);

float heuristic (int x0, int y0, int x1, int y1, const JpsOptions& o);   // use octile for 8-neigh
float dist_cost (int x0, int y0, int x1, int y1, const JpsOptions& o);
float tiebreak  (int x, int y, int sx, int sy, int gx, int gy);          // small ε-bias to goal

bool  has_forced_neighbors_straight(const IGrid& g, int x, int y, int dx, int dy);
bool  has_forced_neighbors_diag   (const IGrid& g, int x, int y, int dx, int dy);

void  pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                  const JpsOptions& o, std::vector<std::pair<int,int>>& out);

bool  jump(const IGrid& g, int x, int y, int dx, int dy,
           int gx, int gy, const JpsOptions& o, int& outx, int& outy);

bool  los_supercover(const IGrid& g, int x0, int y0, int x1, int y1,
                     const JpsOptions& o);

} // namespace detail
} // namespace colony::path
