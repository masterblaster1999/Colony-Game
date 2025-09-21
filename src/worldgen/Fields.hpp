#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <algorithm>

namespace colony::worldgen {

template<typename T>
class Grid {
public:
    Grid() = default;
    Grid(int w, int h) : w_(w), h_(h), data_(static_cast<size_t>(w)*h) {}

    void resize(int w, int h) { w_ = w; h_ = h; data_.assign(static_cast<size_t>(w)*h, T{}); }

    [[nodiscard]] int width() const noexcept  { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }

    [[nodiscard]] T&       at(int x, int y)       { assert(x>=0 && x<w_ && y>=0 && y<h_); return data_[static_cast<size_t>(y)*w_ + x]; }
    [[nodiscard]] const T& at(int x, int y) const { assert(x>=0 && x<w_ && y>=0 && y<h_); return data_[static_cast<size_t>(y)*w_ + x]; }

    [[nodiscard]] T*       raw()       noexcept { return data_.data(); }
    [[nodiscard]] const T* raw() const noexcept { return data_.data(); }

    void fill(const T& v) { std::fill(data_.begin(), data_.end(), v); }

private:
    int w_ = 0, h_ = 0;
    std::vector<T> data_;
};

} // namespace colony::worldgen
