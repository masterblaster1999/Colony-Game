#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace colony {

// Tiny, dependency-free job system for Windows builds.
class JobSystem {
public:
    using Job = std::function<void()>;

    // A handle you can wait on; multiple jobs can share the same handle.
    class JobHandle {
        friend class JobSystem;
        struct State {
            std::atomic<uint32_t> remaining{0};
            std::mutex m;
            std::condition_variable cv;
        };
        JobSystem* owner_{nullptr};
        std::shared_ptr<State> state_;

        JobHandle(JobSystem* owner, std::shared_ptr<State> s) : owner_(owner), state_(std::move(s)) {}
    public:
        JobHandle() = default;
        bool valid() const noexcept { return static_cast<bool>(state_); }
        bool is_done() const noexcept {
            return !state_ || state_->remaining.load(std::memory_order_acquire) == 0;
        }
        // Waits until all jobs associated with this handle finish.
        void wait();
    };

    // Construct N worker threads (defaults to hardware_concurrency - 1, min 1).
    explicit JobSystem(size_t thread_count = std::thread::hardware_concurrency());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Submit a single job. Returns a handle you can wait() on.
    template<class Fn, class... Args>
    JobHandle submit(Fn&& fn, Args&&... args) {
        auto s = std::make_shared<JobHandle::State>();
        s->remaining.store(1, std::memory_order_relaxed);
        Job j = [f = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...),
                 s, this]() mutable {
            run_guarded(std::move(f));
            finish_one(s);
        };
        enqueue(std::move(j));
        return JobHandle(this, std::move(s));
    }

    // parallel_for over [first, last), grain controls chunk size (>=1).
    template<class Index, class Func>
    JobHandle parallel_for(Index first, Index last, Index grain, Func body) {
        if (last <= first) return {};
        if (grain < 1) grain = 1;

        const uint64_t total = static_cast<uint64_t>(last - first);
        uint32_t chunks = static_cast<uint32_t>((total + grain - 1) / grain);

        auto s = std::make_shared<JobHandle::State>();
        s->remaining.store(chunks, std::memory_order_relaxed);

        for (uint64_t b = 0; b < total; b += grain) {
            Index bgn = static_cast<Index>(first + b);
            Index end = static_cast<Index>(std::min<uint64_t>(b + grain, total) + first);
            Job j = [=, s, this]() {
                // run chunk
                for (Index i = bgn; i < end; ++i) body(i);
                finish_one(s);
            };
            enqueue(std::move(j));
        }
        return JobHandle(this, std::move(s));
    }

    // Optional: drain all currently queued jobs (main thread helps).
    void flush();

    // Number of worker threads.
    size_t workers() const noexcept { return workers_.size(); }

private:
    // --- Queue ---
    struct Queue {
        std::mutex m;
        std::condition_variable cv;
        std::deque<Job> dq;
        bool stopping = false;

        void push(Job j) {
            {
                std::lock_guard<std::mutex> lk(m);
                dq.push_back(std::move(j));
            }
            cv.notify_one(); // notify while holding lock is also safe; we unlocked just above
        }

        // Blocks until a job is available or stopping is true.
        bool pop(Job& out) {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&]{ return stopping || !dq.empty(); });
            if (!dq.empty()) {
                out = std::move(dq.front());
                dq.pop_front();
                return true;
            }
            return false; // stopping && empty
        }

        bool try_pop(Job& out) {
            std::lock_guard<std::mutex> lk(m);
            if (dq.empty()) return false;
            out = std::move(dq.front());
            dq.pop_front();
            return true;
        }

        void stop() {
            {
                std::lock_guard<std::mutex> lk(m);
                stopping = true;
            }
            cv.notify_all();
        }

        bool empty() const {
            std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m));
            return dq.empty();
        }
    };

    // --- State ---
    Queue queue_;
    std::vector<std::thread> workers_;
    std::atomic<uint64_t> active_{0}; // jobs currently executing
    std::atomic<bool> quitting_{false};

    // --- Impl helpers ---
    void worker_loop(size_t index);
    void enqueue(Job j) { queue_.push(std::move(j)); }

    template<class F>
    void run_guarded(F&& f) noexcept {
        active_.fetch_add(1, std::memory_order_acq_rel);
        try { f(); } catch (...) { /* TODO: log if desired */ }
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }

    static void set_thread_name_win32(const wchar_t* name); // Windows-only nicety
    static void notify_done(std::shared_ptr<JobHandle::State>& s) {
        std::unique_lock<std::mutex> lk(s->m);
        s->cv.notify_all(); // notify while holding the lock is a safe default
    }

    static void finish_one(std::shared_ptr<JobHandle::State> const& s) {
        if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // last one finished
            notify_done(const_cast<std::shared_ptr<JobHandle::State>&>(s));
        }
    }
};

// ---- Inline wait ----
inline void JobSystem::JobHandle::wait() {
    if (!state_) return;
    // Help run jobs while waiting to avoid stalls:
    while (!is_done()) {
        Job j;
        if (owner_ && owner_->queue_.try_pop(j)) {
            owner_->run_guarded(std::move(j));
        } else {
            std::unique_lock<std::mutex> lk(state_->m);
            state_->cv.wait_for(lk, std::chrono::milliseconds(1), [&]{ return is_done(); });
        }
    }
}

} // namespace colony
