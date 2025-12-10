// src/path/jps.cpp
#include <pathfinding/jps.h>   // the public API above

namespace colony::path {

std::vector<Cell>
jps_find_path(IGrid const& grid, Cell start, Cell goal, JpsOptions const& opts)
{
#ifdef COLONY_HAVE_REAL_JPS_IMPL
    // If you’ve got an internal implementation, call it here:
    return real_jps_impl(grid, start, goal, opts);
#else
    // TODO: replace with the real JPS or call your A* as a fallback.
    (void)grid; (void)start; (void)goal; (void)opts;
    return {};
#endif
}

} // namespace colony::path
