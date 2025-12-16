#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory_resource>
#include <new>
#include <algorithm>
#include <type_traits>
#include <utility>

#if !defined(_WIN32)
#  error "This arena implementation is tuned for Windows builds (_aligned_malloc/_aligned_free)."
#endif
#include <malloc.h> // _aligned_malloc/_aligned_free

namespace colony::memory {

// Align 'n' up to 'a' (a must be a power of two)
inline std::size_t align_up(std::size_t n, std::size_t a) {
    return (n + (a - 1)) & ~(a - 1);
}

// Align pointer-sized integer 'n' up to 'a' (a must be a power of two)
inline std::uintptr_t align_up_ptr(std::uintptr_t n, std::size_t a) {
    const std::uintptr_t mask = static_cast<std::uintptr_t>(a - 1u);
    return (n + mask) & ~mask;
}

class Arena {
public:
    explicit Arena(std::size_t defaultBlockBytes = 1u << 20,
                   std::size_t alignment = alignof(std::max_align_t))
        : defaultBlockBytes_(defaultBlockBytes), alignment_(alignment) {
        add_block(defaultBlockBytes_);
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept { move_from(std::move(other)); }
    Arena& operator=(Arena&& other) noexcept {
        if (this != &other) { release(); move_from(std::move(other)); }
        return *this;
    }

    ~Arena() { release(); }

    void* allocate(std::size_t bytes, std::size_t align = 0) {
        if (align == 0) align = alignment_;
        bytes = align_up(bytes, align);

        Block& b = blocks_.back();

        // IMPORTANT: Align the *address* (base + used), not just b.used.
        const auto base    = reinterpret_cast<std::uintptr_t>(b.ptr);
        const auto current = base + static_cast<std::uintptr_t>(b.used);
        const auto aligned = align_up_ptr(current, align);
        const std::size_t offset = static_cast<std::size_t>(aligned - base);

        if (offset + bytes > b.capacity) {
            // Need a new block; grow geometrically to reduce churn.
            // Also include worst-case alignment padding so that an aligned allocation
            // can always fit even if the underlying block base isn't aligned to `align`.
            const std::size_t cap = std::max(defaultBlockBytes_, bytes + (align - 1u));
            add_block(cap);
            return allocate(bytes, align);
        }

        void* p = static_cast<void*>(b.ptr + offset);
        b.used = offset + bytes;
        return p;
    }

    template <class T, class... Args>
    T* make(Args&&... args) {
        void* p = allocate(sizeof(T), alignof(T));
        return ::new (p) T(std::forward<Args>(args)...);
    }

    // Releases all allocations but keeps the last block to avoid re-malloc on next search
    void reset(bool keepFirstBlock = true) {
        if (blocks_.empty()) return;
        if (keepFirstBlock) {
            for (std::size_t i = 1; i < blocks_.size(); ++i) _aligned_free(blocks_[i].ptr);
            blocks_.resize(1);
            blocks_[0].used = 0;
        } else {
            release();
            add_block(defaultBlockBytes_);
        }
    }

private:
    struct Block {
        std::byte* ptr{};
        std::size_t capacity{};
        std::size_t used{};
    };

    std::vector<Block> blocks_;
    std::size_t defaultBlockBytes_;
    std::size_t alignment_;

    void add_block(std::size_t bytes) {
        auto* mem = static_cast<std::byte*>(_aligned_malloc(bytes, alignment_));
        if (!mem) throw std::bad_alloc{};
        blocks_.push_back(Block{mem, bytes, 0});
    }

    void release() {
        for (auto& b : blocks_) _aligned_free(b.ptr);
        blocks_.clear();
    }

    void move_from(Arena&& o) {
        blocks_ = std::move(o.blocks_);
        defaultBlockBytes_ = o.defaultBlockBytes_;
        alignment_ = o.alignment_;
        o.blocks_.clear();
    }
};

// A polymorphic memory_resource that allocates out of an Arena (deallocations are no-ops).
class ArenaResource final : public std::pmr::memory_resource {
public:
    explicit ArenaResource(Arena& a) : arena_(&a) {}

protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        return arena_->allocate(bytes, alignment); // NOLINT
    }
    void do_deallocate(void*, std::size_t, std::size_t) override {
        // no-op (freed en masse by Arena::reset)
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    Arena* arena_;
};

} // namespace colony::memory
