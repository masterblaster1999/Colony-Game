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
#include <type_traits>

// Notes (Windows/MSVC):
// - C4430 “missing type specifier – int assumed” typically means a type used in a
//   declaration wasn’t visible; making this header self-contained prevents that. :contentReference[oaicite:0]{index=0}
// - MSVC supports __has_include since VS 2017 15.3, which we use above. :contentReference[oaicite:1]{index=1}
// - [[nodiscard]] is a C++17 attribute; MSVC warns if results are discarded. :contentReference[oaicite:2]{index=2}

namespace colony::path {
namespace detail {

// Compile-time sanity checks for the platform/toolchain used.
static_assert(sizeof(int) >= 4, "colony::path requires int to be at least 32 bits.");
// Node is intended to be trivially copyable for fast vector movement.
struct Node;
static_assert(std::is_trivially_copyable_v<int>, "int must be trivially copyable (sanity).");

// A conventional sentinel for “no parent”.
inline constexpr int kNoParent = -1;

// Per-cell bookkeeping for the JPS/A* search.
struct Node {
    int   x = 0, y = 0;                       // grid coordinates
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    int   parent = kNoParent;                 // parent node index
    int   px = 0,   py = 0;                   // parent's coordinates (for pruning)
    bool  opened = false;
    bool  closed = false;
};

// priority_queue is a max-heap; invert comparison for min-heap on f.
struct PQItem {
    int   index = -1;
    float f     = 0.0f;
    bool operator<(const PQItem& rhs) const noexcept { return f > rhs.f; }
};

// ---- Helper declarations (definitions live in Jps.cpp) ----
[[nodiscard]] int   idx(int x, int y, int W);
[[nodiscard]] bool  in_bounds(const IGrid& g, int x, int y);
[[nodiscard]] bool  passable (const IGrid& g, int x, int y);
[[nodiscard]] bool  can_step (const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o);

[[nodiscard]] float heuristic (int x0, int y0, int x1, int y1, const JpsOptions& o);   // use octile for 8-neigh
[[nodiscard]] float dist_cost (int x0, int y0, int x1, int y1, const JpsOptions& o);
[[nodiscard]] float tiebreak  (int x, int y, int sx, int sy, int gx, int gy);          // small ε-bias to goal

[[nodiscard]] bool  has_forced_neighbors_straight(const IGrid& g, int x, int y, int dx, int dy);
[[nodiscard]] bool  has_forced_neighbors_diag   (const IGrid& g, int x, int y, int dx, int dy);

void  pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                  const JpsOptions& o, std::vector<std::pair<int,int>>& out);

[[nodiscard]] bool  jump(const IGrid& g, int x, int y, int dx, int dy,
                         int gx, int gy, const JpsOptions& o, int& outx, int& outy);

[[nodiscard]] bool  los_supercover(const IGrid& g, int x0, int y0, int x1, int y1,
                                   const JpsOptions& o);

} // namespace detail
} // namespace colony::path
