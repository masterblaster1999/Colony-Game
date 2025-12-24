// ========================== Tests ==========================
#ifdef NAV2D_TESTS
#include <random>
static int test_all() {
    using namespace nav2d;
    // Build a test grid with a wall & two doorways
    Grid g(40,20);
    for (int y=0;y<20;++y) { g.setBlocked(20,y,true); }
    g.setBlocked(20,5,false); g.setBlocked(20,12,false); // doors

    Planner p(&g);
    SearchParams sp; sp.allow_diagonal=true; sp.allow_corner_cutting=false;

    // A* path
    auto prA = p.findPath({2,2},{37,17}, sp);
    if (!prA.success) { std::cout << "A* failed\n"; return 1; }

    // JPS should match cost on unit grid
#if NAV2D_ENABLE_JPS
    auto spJ = sp; spJ.prefer_jps=true;
    auto prJ = p.findPath({2,2},{37,17}, spJ);
    if (!prJ.success || std::abs(prJ.cost - prA.cost) > 1e-4f) { std::cout << "JPS mismatch\n"; return 2; }
#endif

    // Flow field to right-bottom corner
#if NAV2D_ENABLE_FLOWFIELD
    auto ff = p.computeFlowField({37,17}, true);
    Cell cur{2,2}; int steps=0; while (cur != Cell{37,17} && steps++ < 500) cur = ff.step(cur);
    if (cur != Cell{37,17}) { std::cout << "Flow field fail\n"; return 3; }
#endif

    // D* Lite replan with door swap
#if NAV2D_ENABLE_DSTARLITE
    auto pr1 = p.replan({2,2},{37,17}, sp);
    if (!pr1.success) { std::cout << "D* initial fail\n"; return 4; }
    // close y=5 door, open y=8
    g.setBlocked(20,5,true); g.setBlocked(20,8,false);
    p.notifyTerrainChanged({{20,5},{20,8}});
    auto pr2 = p.replan({2,2},{37,17}, sp);
    if (!pr2.success) { std::cout << "D* replan fail\n"; return 5; }
#endif

    // HPA* (if large enough, should find a route similar to A*)
#if NAV2D_ENABLE_HPA
    SearchParams spH = sp; spH.use_hpa=true; spH.hpa_cluster_size=8;
    auto prH = p.findPath({2,2},{37,17}, spH);
    if (!prH.success) { std::cout << "HPA* fail\n"; return 6; }
#endif

    std::cout << "OK\n";
    return 0;
}

int main(){ return test_all(); }
#endif // NAV2D_TESTS

