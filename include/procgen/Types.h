#pragma once
#include <cstdint>
#include <utility>

namespace procgen {
    struct IV2 { int x, y; };
    struct FV2 { float x, y; };
    inline bool in_bounds(int x, int y, int w, int h) {
        return (unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h;
    }
}
