// src/path/jps.cpp
#include "jps.h" // declares colony::path::IGrid, Cell, JpsOptions, jps_find_path

namespace colony::path {

std::vector<Cell>
jps_find_path(IGrid const& grid, Cell start, Cell goal, JpsOptions const& opts)
{
    // If you already have an internal JPS routine, call it here.
    // Otherwise temporarily fallback to A* so the link succeeds:
    // return astar_find_path(grid, start, goal, opts.as_astar());

    return real_jps_impl(grid, start, goal, opts); // your actual implementation
}

} // namespace colony::path
