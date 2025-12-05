#pragma once
#include <cstddef>
#include <new>
#include <vector>
#include <mutex>
#include <type_traits>
#include <cassert>
#include <utility>

namespace colony::memory {

template <class T, std::size_t ChunkSize = 1024, bool ThreadSafe = false>
class ObjectPool {
    static_assert(ChunkSize > 0, "ChunkSize must be > 0");

    union Node {
        alignas(T) unsigned char storage[sizeof(T)];
        Node* next;
    };

    // Named no-op mutex type avoids MSVC parser issues with anonymous structs
    struct NoopMutex { void lock() noexcept {} void unlock() noexcept {} };

public:
    ObjectPool() = default;

    ~ObjectPool() {
        // Best-effort free of raw chunks; clients should 'drain' live objects first.
        for (void* mem : chunks_) {
            ::operator delete[](mem, std::align_val_t(alignof(Node)));
        }
    }

    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template <class... Args>
    T* create(Args&&... args) {
        if constexpr (ThreadSafe) lock_.lock();
        if (!free_) allocate_chunk_();
        Node* n = free_;
        free_   = free_->next;
        if constexpr (ThreadSafe) lock_.unlock();

        T* obj = reinterpret_cast<T*>(n->storage);
        ::new (obj) T(std::forward<Args>(args)...);
        return obj;
    }

    void destroy(T* obj) {
        if (!obj) return;
        obj->~T();
        Node* n = reinterpret_cast<Node*>(obj);
        if constexpr (ThreadSafe) {
            std::scoped_lock lk(lock_);
            n->next = free_;
            free_   = n;
        } else {
            n->next = free_;
            free_   = n;
        }
    }

    void reserve(std::size_t objects) {
        const std::size_t need = (objects + ChunkSize - 1) / ChunkSize;
        while (chunks_.size() < need) allocate_chunk_();
    }

private:
    void allocate_chunk_() {
        void* mem = ::operator new[](sizeof(Node) * ChunkSize,
                                     std::align_val_t(alignof(Node)));
        chunks_.push_back(mem);
        Node* base = reinterpret_cast<Node*>(mem);
        // Push freshly allocated nodes onto free list
        for (std::size_t i = 0; i < ChunkSize; ++i) {
            base[i].next = free_;
            free_ = &base[i];
        }
    }

    std::vector<void*> chunks_{};
    Node* free_ = nullptr;

    // MSVC-safe conditional lock type
    [[no_unique_address]] std::conditional_t<ThreadSafe, std::mutex, NoopMutex> lock_{};
};

} // namespace colony::memory
