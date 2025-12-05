#include "pathfinding/Jps.hpp"
#include <cassert>
#include <iostream>
#include <doctest/doctest.h>

using namespace colony::path;

struct TestGrid : IGrid {
    int W,H; std::vector<uint8_t> occ;
    TestGrid(int w,int h):W(w),H(h),occ(w*h,0){}
    int width() const override { return W; }
    int height() const override { return H; }
    bool walkable(int x,int y) const override {
        if (x<0||y<0||x>=W||y>=H) return false;
        return occ[y*W+x]==0;
    }
};

int main() {
    TestGrid g(10,10);
    // Block a small obstacle to force a turn
    g.occ[5*10+4]=1; g.occ[5*10+5]=1; g.occ[5*10+6]=1;

    JpsOptions opt; opt.allowDiagonal = true; opt.dontCrossCorners = true;
    auto path = jps_find_path(g, {1,5},{8,5}, opt);
    assert(!path.empty());
    assert(path.front().x==1 && path.front().y==5);
    assert(path.back().x==8 && path.back().y==5);
    std::cout << "JPS basic test OK. Points: " << path.size() << "\n";
    return 0;
}
