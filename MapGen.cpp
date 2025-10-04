#include "MapGen.h"
#include <cmath>

namespace sim {

void MapGen::Generate(int w, int h, std::vector<float>& out) {
    out.resize(w*h);
    for (int y=0; y<h; ++y)
        for (int x=0; x<w; ++x)
            out[y*w + x] = 0.5f + 0.5f * std::sin(0.05f * float(x + y)); // placeholder
}

} // namespace sim
