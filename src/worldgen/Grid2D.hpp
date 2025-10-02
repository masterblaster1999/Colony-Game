#pragma once
#include <vector>
#include <cassert>
#include <algorithm>
#include <cstddef>

namespace colony::worldgen {

// Simple, cache-friendly row-major 2D grid.
//  - Contiguous memory (row-major) for good locality.
//  - Debug-only bounds checks in at().
//  - rowPtr(y) gives a raw pointer to the start of the y-th row (fast inner loops).
template <class T>
class Grid2D {
public:
    using value_type = T;

    Grid2D() = default;

    Grid2D(int w, int h)
        : w_(w), h_(h), data_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {}

    Grid2D(int w, int h, const T& init)
        : w_(w), h_(h), data_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), init) {}

    [[nodiscard]] int width()  const noexcept { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    T*       data()       noexcept { return data_.data(); }
    const T* data() const noexcept { return data_.data(); }

    // Pointer to the start of a row (fast path for tight loops).
    T* rowPtr(int y) noexcept {
#ifndef NDEBUG
        assert(y >= 0 && y < h_);
#endif
        return data_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w_);
    }
    const T* rowPtr(int y) const noexcept {
#ifndef NDEBUG
        assert(y >= 0 && y < h_);
#endif
        return data_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w_);
    }

    // Checked element access in debug builds; unchecked in release.
    T& at(int x, int y) noexcept {
#ifndef NDEBUG
        assert(x >= 0 && x < w_ && y >= 0 && y < h_);
#endif
        return data_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
    }
    const T& at(int x, int y) const noexcept {
#ifndef NDEBUG
        assert(x >= 0 && x < w_ && y >= 0 && y < h_);
#endif
        return data_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
    }

    // Handy helper used by some stages (optional, cheap).
    T sampleClamped(int x, int y) const noexcept {
        if (x < 0) x = 0; else if (x >= w_) x = w_ - 1;
        if (y < 0) y = 0; else if (y >= h_) y = h_ - 1;
        return data_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
    }

    void fill(const T& v) { std::fill(data_.begin(), data_.end(), v); }

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<T> data_;
};

} // namespace colony::worldgen
