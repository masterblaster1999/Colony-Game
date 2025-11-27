#pragma once
#include <cstddef>
#include <new>
#include <vector>
#include <mutex>
#include <type_traits>
#include <cassert>

namespace colony::memory {

// Fixed-size object pool with free-list, for long-lived graph/hierarchy nodes.
// Thread safety is optional (default off) to keep pathfinding hot paths lock-free when used per-worker.
template <class T, std::size_t ChunkSize = 1024, bool ThreadSafe = false>
class ObjectPool {
    static_assert(ChunkSize > 0, "ChunkSize must be > 0");

    union Node {
        alignas(T) unsigned char storage[sizeof(T)];
        Node* next;
    };

    struct Chunk {
        Node* data{nullptr}; // contiguous block
    };

public:
    ObjectPool() = default;
    ~ObjectPool() {
        // Destroy any still-checked-out objects conservatively:
        // We don't know which are alive without tracking; require the client to 'drain' in normal paths.
        for (auto* c : chunks_) {
            // Try best-effort: nothing to do (we don't know which were destructed).
            ::operator delete[](c, std::align_val_t(alignof(Node)));
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template <class... Args>
    T* create(Args&&... args) {
        if constexpr (ThreadSafe) lock_.lock();
        if (!free_) allocate_chunk_();
        Node* n = free_;
        free_ = free_->next;
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
            free_ = n;
        } else {
            n->next = free_;
            free_ = n;
        }
    }

    // Optional: reserve capacity ahead of time (reduces contention during spikes)
    void reserve(std::size_t objects) {
        std::size_t need = (objects + ChunkSize - 1) / ChunkSize;
        while (chunks_.size() < need) allocate_chunk_();
    }

private:
    std::vector<void*> chunks_;
    Node* free_{nullptr};
    [[no_unique_address]] std::conditional_t<ThreadSafe, std::mutex, struct { void lock(){} void unlock(){}; }> lock_{};

    void allocate_chunk_() {
        // Allocate raw memory for ChunkSize nodes, properly aligned
        void* mem = ::operator new[](sizeof(Node) * ChunkSize, std::align_val_t(alignof(Node)));
        chunks_.push_back(mem);
        Node* base = reinterpret_cast<Node*>(mem);
        // Push all onto free list
        for (std::size_t i = 0; i < ChunkSize; ++i) {
            base[i].next = free_;
            free_ = &base[i];
        }
    }
};

} // namespace colony::memory
