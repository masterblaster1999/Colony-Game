#include <cassert>
#include <vector>
#include "nav/IGridMap.h"
#include "nav/Navigator.h"

using namespace colony::nav;

struct TestGrid : IGridMap {
    int w,h; std::vector<uint8_t> pass;
    TestGrid(int W,int H):w(W),h(H),pass(W*H,1){}
    int32_t Width()  const override { return w; }
    int32_t Height() const override { return h; }
    bool IsPassable(int32_t x, int32_t y) const override { return pass[y*w + x] != 0; }
};

static void Line(TestGrid& g, int x0,int y0,int x1,int y1) {
    int dx = (x1>x0)-(x1<x0), dy=(y1>y0)-(y1<y0);
    int x=x0,y=y0; for(;;){ g.pass[y*g.w + x]=0; if(x==x1 && y==y1) break; x+=dx; y+=dy; }
}

int main() {
    {   // open grid
        TestGrid g(32,32);
        Navigator nav(g);
        auto p = nav.FindPath({0,0},{31,31});
        assert(p.has_value() && !p->points.empty());
        assert(p->points.front().x==0 && p->points.front().y==0);
        assert(p->points.back().x==31 && p->points.back().y==31);
    }
    {   // obstacle wall with a gap (tests corner cutting avoidance)
        TestGrid g(32,32);
        Line(g, 0,15, 30,15);
        g.pass[15*g.w + 16] = 1; // gap
        Navigator nav(g);
        auto p = nav.FindPath({4,10},{28,20});
        assert(p.has_value());
        // ensure path crosses y=15 near gap
        bool crossed=false;
        for (auto& c : p->points) if (c.y==15 && c.x==16) { crossed=true; break; }
        assert(crossed);
    }
    {   // multi-cluster: start/end in different clusters
        TestGrid g(96,96); // with 32x32 cluster, different clusters
        // carve a corridor
        for (int y=0;y<96;++y)
            for (int x=0;x<96;++x)
                g.pass[y*g.w + x] = (x==48 ? 1 : g.pass[y*g.w+x]);
        Navigator::Options opt;
        opt.cluster.clusterW=32; opt.cluster.clusterH=32; opt.cluster.portalStride=8;
        Navigator nav(g, opt);
        auto p = nav.FindPath({4,4},{90,90});
        assert(p.has_value());
    }
    return 0;
}
