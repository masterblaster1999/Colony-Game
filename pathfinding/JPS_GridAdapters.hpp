#pragma once
// JPS_GridAdapters.hpp — helpers to adapt your existing obstacle representation.
//
// 1) LambdaGrid: wrap any bool(int x,int y) functor + dimensions.
// 2) MaskGrid  : wrap a width*height U8 mask (0=free, nonzero=blocked).
//
// Both provide:
//   bool passable(int x,int y) const;
//   bool allowDiagonal() const;
//
// Example:
//   auto isFree = [&](int x,int y){ return tiles[y*W+x] == 0; };
//   jps::LambdaGrid grid{W,H,isFree,true};
//   jps::Options opt{true,false,false};
//   std::vector<jps::Succ> succ;
//   jps::expand_ids(grid, opt, curId, parentIdOrNull, goalId, W, succ);

#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif
#include <vector>
#include <cstdint>
#include <algorithm>

namespace jps {

struct Bounds {
    int W{0}, H{0};
    constexpr bool in(int x,int y) const noexcept {
        return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H;
    }
};

// 1) LambdaGrid — generic adapter
template <class Functor>
class LambdaGrid {
public:
    LambdaGrid(int W, int H, Functor f, bool allowDiag)
        : m_bounds{W,H}, m_fun(f), m_allowDiag(allowDiag) {}

    bool passable(int x, int y) const {
        if (!m_bounds.in(x,y)) return false;
        return m_fun(x,y);
    }
    bool allowDiagonal() const { return m_allowDiag; }

private:
    Bounds  m_bounds;
    Functor m_fun;
    bool    m_allowDiag{true};
};

// 2) MaskGrid — 0 = free, nonzero = blocked
class MaskGrid {
public:
    MaskGrid(int W, int H, const std::uint8_t* data, bool allowDiag)
        : m_bounds{W,H}, m_data(data), m_allowDiag(allowDiag) {}

    bool passable(int x, int y) const {
        if (!m_bounds.in(x,y)) return false;
        return m_data[static_cast<size_t>(y)*m_bounds.W + x] == 0;
    }
    bool allowDiagonal() const { return m_allowDiag; }

private:
    Bounds               m_bounds;
    const std::uint8_t*  m_data;
    bool                 m_allowDiag{true};
};

} // namespace jps
