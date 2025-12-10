// src/path/jps.cpp
//
// Robust include: try several likely locations for the JPS public header.
// MSVC will print a one-line note telling you which path was used.

#if __has_include(<pathfinding/jps.h>)
  #include <pathfinding/jps.h>
  #pragma message("jps.cpp: including <pathfinding/jps.h>")
#elif __has_include("pathfinding/jps.h")
  #include "pathfinding/jps.h"
  #pragma message("jps.cpp: including \"pathfinding/jps.h\"")
#elif __has_include("../../pathfinding/jps.h")
  #include "../../pathfinding/jps.h"
  #pragma message("jps.cpp: including \"../../pathfinding/jps.h\"")
#elif __has_include("jps.h")
  #include "jps.h"
  #pragma message("jps.cpp: including \"jps.h\"")
#elif __has_include("../../include/colony/path/jps.h")
  #include "../../include/colony/path/jps.h"
  #pragma message("jps.cpp: including \"../../include/colony/path/jps.h\"")
#else
  #error "Cannot locate jps.h. Add ${CMAKE_SOURCE_DIR}/pathfinding to your target_include_directories or add a forwarding header at src/path/jps.h."
#endif

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
