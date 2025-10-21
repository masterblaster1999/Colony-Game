#pragma once
#include <algorithm>
#include <limits>
#include <type_traits>

namespace cg {

// Generic MinMax (works for float, double, int, etc.)
template <class T>
struct MinMaxT {
    T min;
    T max;

    constexpr void add(const T& v) noexcept {
        min = std::min(min, v);
        max = std::max(max, v);
    }
    constexpr void merge(const MinMaxT& o) noexcept {
        min = std::min(min, o.min);
        max = std::max(max, o.max);
    }
};

// Your code already refers to cg::MinMax (non-templated). Keep it as float.
using MinMax = MinMaxT<float>;

// Helpers
constexpr inline MinMax empty_minmax() noexcept {
    return { std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity() };
}

constexpr inline MinMax make_minmax(float a, float b) noexcept {
    return { std::min(a, b), std::max(a, b) };
}

} // namespace cg
