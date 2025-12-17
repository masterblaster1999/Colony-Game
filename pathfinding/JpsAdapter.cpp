--- a/pathfinding/JpsAdapter.cpp
+++ b/pathfinding/JpsAdapter.cpp
@@ -1,5 +1,5 @@
 // pathfinding/JpsAdapter.cpp
-// Bridges include/pathfinding/Jps.hpp public API to legacy JPS core (JpsCore.hpp / FindPathJPS).
+// Bridges include/pathfinding/Jps.hpp public API to the JPS core (pathfinding/JpsCore.hpp).

 #include "pathfinding/Jps.hpp"
 #include "pathfinding/JpsCore.hpp"

@@ -9,10 +9,9 @@
 #include <vector>

 namespace colony::path {
-namespace detail {
-namespace jps_adapter_detail {
+namespace detail::jps_adapter_detail {

-static inline int sign_i(int v) {
+static inline int sign_i(int v) noexcept {
     return (v > 0) - (v < 0);
 }

@@ -21,8 +20,9 @@
                     int x0, int y0,
                     int x1, int y1,
                     bool allowDiag,
-                    bool dontCrossCorners) {
-    if (!gv.walkable(x1, y1)) return false;
+                    bool dontCrossCorners)
+{
+    if (!gv.passable(x1, y1)) return false;

     const int dx = x1 - x0;
     const int dy = y1 - y0;

@@ -35,13 +35,13 @@
     if (!dontCrossCorners) return true;

     // Corner guard: for diagonal movement, require both adjacent orthogonals to be free.
-    return gv.walkable(x0 + dx, y0) && gv.walkable(x0, y0 + dy);
+    return gv.passable(x0 + dx, y0) && gv.passable(x0, y0 + dy);
 }

-} // namespace jps_adapter_detail
-} // namespace detail
+} // namespace detail::jps_adapter_detail

-std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt) {
+std::vector<Cell> jps_find_path_impl(const IGrid& grid, Cell start, Cell goal, const JpsOptions& opt)
+{
     // Validate endpoints (tests expect empty path if start OR goal blocked)
     if (!grid.walkable(start.x, start.y) || !grid.walkable(goal.x, goal.y)) {
         return {};
@@ -52,31 +52,33 @@
         return { start };
     }

-    // Wrap the public grid into the core GridView
+    // Wrap the public IGrid into the core GridView.
+    // GridView expects an `isBlocked(x,y)` callback (true => blocked).
     GridView gv{
         grid.width(),
         grid.height(),
-        [&](int x, int y) { return grid.walkable(x, y); }
+        [&grid](int x, int y) { return !grid.walkable(x, y); }
     };

-    FindPathJPS jps(gv);
-    jps.allowDiag        = opt.allowDiagonal;
-    jps.dontCrossCorners = opt.dontCrossCorners;
+    // JPS core returns a list of points as (x,y) pairs.
+    const auto jpPairs = FindPathJPS(gv,
+                                     start.x, start.y,
+                                     goal.x, goal.y,
+                                     opt.allowDiagonal,
+                                     opt.dontCrossCorners);

-    const auto jpPath = jps.find({ start.x, start.y }, { goal.x, goal.y });
-    if (jpPath.empty()) {
+    if (jpPairs.empty()) {
         return {};
     }

-    // Convert Point -> Cell
     std::vector<Cell> out;
-    out.reserve(jpPath.size());
-    for (const auto& p : jpPath) {
-        out.push_back(Cell{ p.x, p.y });
+    out.reserve(jpPairs.size());
+    for (const auto& p : jpPairs) {
+        out.push_back(Cell{ p.first, p.second });
     }

-    // Optional: expand “jump points” into dense step-by-step path
-    // (Your tests only assert endpoints, but dense paths are generally friendlier for movers.)
+    // Expand “jump points” into a dense, step-by-step path if requested.
+    // Tests only assert endpoints, but dense paths are often friendlier for movers.
     if (opt.returnDensePath && !opt.preferJumpPoints && out.size() >= 2) {
         std::vector<Cell> dense;
         dense.reserve(out.size() * 2);
@@ -99,7 +101,7 @@
                 if (!detail::jps_adapter_detail::step_ok(gv, x, y, nx, ny,
                                                         opt.allowDiagonal,
                                                         opt.dontCrossCorners)) {
-                    // If we can’t safely densify, fall back to jump points.
+                    // If we can't safely densify, fall back to jump points.
                     dense.clear();
                     break;
                 }
@@ -109,9 +111,7 @@
                 dense.push_back(Cell{ x, y });
             }

-            if (dense.empty()) {
-                break;
-            }
+            if (dense.empty()) break;
         }

         if (!dense.empty()) {
@@ -119,7 +119,7 @@
         }
     }

-    // Future: opt.smoothPath could do LOS-based string pulling here.
+    // TODO: If opt.smoothPath is true, run a LOS-based string pulling pass here.
     return out;
 }
