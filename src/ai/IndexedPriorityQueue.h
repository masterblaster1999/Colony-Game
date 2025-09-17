#pragma once
#include <vector>
#include <limits>
#include <cstddef>
#include <algorithm>
#include <cstdint>

// A minimal indexable min-heap keyed by 64-bit integer priorities.
// - Nodes are addressed by integer indices [0..N).
// - Supports O(log n) push-or-decrease and pop_min.
// - No dynamic allocation per operation; only vectors (cache friendly).
// - Integer keys provide deterministic ordering across Windows/MSVC builds.
class IndexedPriorityQueue {
public:
    using Index = int;
    using Key   = std::uint64_t;

    explicit IndexedPriorityQueue(std::size_t capacity = 0) {
        reset(capacity);
    }

    void reset(std::size_t capacity) {
        heap_.clear();
        heap_.shrink_to_fit();
        heap_.reserve(capacity);

        pos_.assign(capacity, kNotInHeap);
        key_.assign(capacity, kInf);
    }

    bool empty() const noexcept { return heap_.empty(); }
    std::size_t size() const noexcept { return heap_.size(); }

    // Ensure we can address index 'i'
    void ensure(Index i) {
        const std::size_t need = static_cast<std::size_t>(i) + 1;
        if (need > pos_.size()) {
            pos_.resize(need, kNotInHeap);
            key_.resize(need, kInf);
        }
    }

    // Insert if new; if already present and 'k' is lower, decrease the key.
    // Returns true if the heap was modified (inserted or decreased).
    bool push_or_decrease(Index i, Key k) {
        ensure(i);
        if (pos_[i] == kNotInHeap) {
            key_[i] = k;
            heap_.push_back(i);
            pos_[i] = static_cast<Index>(heap_.size() - 1);
            sift_up(pos_[i]);
            return true;
        }
        if (k < key_[i]) {
            key_[i] = k;
            sift_up(pos_[i]);
            return true;
        }
        return false;
    }

    // Pop the index with the lowest key.
    Index pop_min() {
        const Index minIdx = heap_.front();
        const Index last = heap_.back();
        heap_.pop_back();
        pos_[minIdx] = kNotInHeap;

        if (!heap_.empty()) {
            heap_.front() = last;
            pos_[last] = 0;
            sift_down(0);
        }
        return minIdx;
    }

    bool contains(Index i) const {
        return i >= 0 && static_cast<std::size_t>(i) < pos_.size() && pos_[i] != kNotInHeap;
    }

    Key key(Index i) const {
        return key_.at(static_cast<std::size_t>(i));
    }

private:
    static constexpr Index kNotInHeap = -1;
    static constexpr Key   kInf       = std::numeric_limits<Key>::max();

    // heap_ stores node indices; pos_[i] = position of i in heap_ (or -1); key_[i] = priority
    std::vector<Index> heap_;
    std::vector<Index> pos_;
    std::vector<Key>   key_;

    void sift_up(std::size_t i) {
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (key_[heap_[i]] < key_[heap_[p]]) {
                std::swap(heap_[i], heap_[p]);
                pos_[heap_[i]] = static_cast<Index>(i);
                pos_[heap_[p]] = static_cast<Index>(p);
                i = p;
            } else break;
        }
    }

    void sift_down(std::size_t i) {
        const std::size_t n = heap_.size();
        for (;;) {
            const std::size_t l = 2 * i + 1;
            const std::size_t r = l + 1;
            std::size_t s = i;
            if (l < n && key_[heap_[l]] < key_[heap_[s]]) s = l;
            if (r < n && key_[heap_[r]] < key_[heap_[s]]) s = r;
            if (s == i) break;
            std::swap(heap_[i], heap_[s]);
            pos_[heap_[i]] = static_cast<Index>(i);
            pos_[heap_[s]] = static_cast<Index>(s);
            i = s;
        }
    }
};
