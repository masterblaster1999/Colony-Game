#include "Rivers.hpp"
#include <queue>
#include <algorithm>
#include <cmath>

namespace pcg {

static inline int idx(int x,int y,int W){return y*W+x;}
static const int DX[8]={1,1,0,-1,-1,-1,0,1};
static const int DY[8]={0,1,1,1,0,-1,-1,-1};

void compute_flow_accumulation(const std::vector<float>& h, int W, int H, std::vector<float>& out) {
    out.assign(W*H, 1.0f); // each cell contributes at least 1
    // Create list of cells sorted by height descending (top -> down)
    std::vector<int> order(W*H);
    for (int i=0;i<W*H;++i) order[i]=i;
    std::sort(order.begin(), order.end(), [&](int a,int b){ return h[a] > h[b]; });

    for (int o=0;o<(int)order.size();++o) {
        int i = order[o];
        int x = i % W, y = i / W;
        // flow to steepest descent neighbor
        float best = h[i]; int bx = x, by = y;
        for (int k=0;k<8;++k) {
            int nx = x+DX[k], ny = y+DY[k];
            if (nx<0||ny<0||nx>=W||ny>=H) continue;
            if (h[idx(nx,ny,W)] < best) { best = h[idx(nx,ny,W)]; bx = nx; by = ny; }
        }
        if (bx==x && by==y) continue; // sink
        out[idx(bx,by,W)] += out[i];
    }
}

void carve_rivers(std::vector<float>& h, const std::vector<float>& flow,
                  int W,int H, [[maybe_unused]] float cellSize, float flowThresh, std::vector<uint8_t>& river) {
    (void)cellSize; // keep signature; not used (yet)
    river.assign(W*H, 0u);
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
        int i = idx(x,y,W);
        if (flow[i] >= flowThresh) {
            river[i] = 1u;
            // carve a shallow channel
            h[i] -= std::min(2.0f, 0.02f * std::sqrt(flow[i]));
        }
    }
    // Optional: smooth pass (naive)
    for (int y=1;y<H-1;++y) for (int x=1;x<W-1;++x) {
        int i=idx(x,y,W);
        if (!river[i]) continue;
        float sum=0, cnt=0;
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
            int j=idx(x+dx,y+dy,W); sum+=h[j]; cnt++;
        }
        h[i] = 0.25f * h[i] + 0.75f * (sum/cnt);
    }
}

} // namespace pcg
