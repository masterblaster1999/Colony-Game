--- a/src/ai/Pathfinding.cpp
+++ b/src/ai/Pathfinding.cpp
@@ -1,0 +1,138 @@
+#include "Pathfinding.hpp"
+
+// Keep this TU robust in MSVC Unity builds on Windows (min/max macros, etc.)
+#ifdef _WIN32
+#  ifndef NOMINMAX
+#    define NOMINMAX
+#  endif
+#  ifndef WIN32_LEAN_AND_MEAN
+#    define WIN32_LEAN_AND_MEAN
+#  endif
+#endif
+
+#include <array>
+#include <cassert>
+#include <utility>
+
+// Avoid any dx/dy confusion entirely: use a local integer abs + Manhattan.
+static inline int iabs(int v) noexcept { return v < 0 ? -v : v; }
+static inline int manhattan(const Point& a, const Point& b) noexcept {
+    return iabs(a.x - b.x) + iabs(a.y - b.y);
+}
+
+PFResult aStar(const GridView& g, Point start, Point goal,
+               std::vector<Point>& out, int maxExpandedNodes) {
+    out.clear();
+
+    // Basic validation
+    if (g.w <= 0 || g.h <= 0) return PFResult::NoPath;
+    if (!g.walkable || !g.cost) return PFResult::NoPath;
+    if (!g.inBounds(start.x, start.y)) return PFResult::NoPath;
+    if (!g.inBounds(goal.x, goal.y)) return PFResult::NoPath;
+    if (!g.walkable(start.x, start.y)) return PFResult::NoPath;
+    if (!g.walkable(goal.x, goal.y)) return PFResult::NoPath;
+    if (start.x == goal.x && start.y == goal.y) {
+        out.push_back(start);
+        return PFResult::Found;
+    }
+
+    const int N = g.w * g.h;
+    // Function-style call avoids min/max macro interference from <Windows.h>.
+    constexpr int INF = (std::numeric_limits<int>::max)();
+
+    std::vector<int>     gCost(N, INF);
+    std::vector<int>     parent(N, -1);
+    std::vector<uint8_t> closed(N, 0);
+
+    const int startIdx = g.index(start.x, start.y);
+    const int goalIdx  = g.index(goal.x, goal.y);
+
+    auto h = [&](int x, int y) -> int { return manhattan({x, y}, goal); };
+
+    struct Node { int idx; int f; };
+    struct ByF  { bool operator()(const Node& a, const Node& b) const noexcept { return a.f > b.f; } };
+
+    std::priority_queue<Node, std::vector<Node>, ByF> open;
+    open.push({ startIdx, h(start.x, start.y) });
+    gCost[startIdx] = 0;
+
+    int expanded = 0;
+    while (!open.empty()) {
+        const Node node = open.top();
+        open.pop();
+        const int cur = node.idx;
+        if (closed[cur]) continue;
+        closed[cur] = 1;
+
+        if (maxExpandedNodes >= 0 && ++expanded > maxExpandedNodes)
+            return PFResult::Aborted;
+
+        if (cur == goalIdx) {
+            // Reconstruct path
+            std::vector<Point> rev;
+            for (int p = cur; p != -1; p = parent[p]) rev.push_back(g.fromIndex(p));
+            out.assign(rev.rbegin(), rev.rend());
+            return PFResult::Found;
+        }
+
+        const Point p = g.fromIndex(cur);
+        const int nbrX[4] = { p.x + 1, p.x - 1, p.x,     p.x     };
+        const int nbrY[4] = { p.y,     p.y,     p.y + 1, p.y - 1 };
+
+        for (int i = 0; i < 4; ++i) {
+            const int nx = nbrX[i], ny = nbrY[i];
+            if (!g.inBounds(nx, ny) || !g.walkable(nx, ny)) continue;
+
+            const int nIdx = g.index(nx, ny);
+            if (closed[nIdx]) continue;
+
+            // Use function-call form to dodge min/max macro collisions.
+            const int stepCost = (std::max)(1, g.cost(nx, ny));
+            const int tentative = gCost[cur] + stepCost;
+            if (tentative < gCost[nIdx]) {
+                parent[nIdx] = cur;
+                gCost[nIdx] = tentative;
+                const int f = tentative + h(nx, ny);
+                open.push({ nIdx, f });
+            }
+        }
+    }
+    return PFResult::NoPath;
+}
