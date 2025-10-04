#pragma once
#include <vector>

namespace sim {

class MapGen {
public:
    void Generate(int w, int h, std::vector<float>& outHeights);
};

} // namespace sim
