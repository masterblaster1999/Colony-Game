#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class JobSystem {
public:
    using JobFn = std::function<void()>;

    explicit JobSystem(unsigned workers = std::thread::hardware_concurrency())
    {
        if (workers == 0) workers = 2;
        m_running = true;
        m_workers.reserve(workers);
        for (unsigned i=0; i<workers; ++i) {
            m_workers.emplace_back([this]{ this->WorkerLoop(); });
        }
    }
    ~JobSystem() { Stop(); }

    void Stop() {
        bool expected = true;
        if (m_running.compare_exchange_strong(expected, false)) {
            {
                std::lock_guard<std::mutex> lk(m_mx);
                // nothing
            }
            m_cv.notify_all();
            for (auto& t : m_workers) if (t.joinable()) t.join();
        }
    }

    // Lower 'priority' means run earlier. You can also bin by kind (I/O vs CPU).
    void Submit(JobFn fn, int priority = 0) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_q.emplace(Task{priority, std::move(fn)});
        m_cv.notify_one();
    }

private:
    struct Task {
        int priority;
        JobFn fn;
        bool operator<(const Task& o) const noexcept { return priority > o.priority; } // min-heap behavior
    };

    void WorkerLoop() {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lk(m_mx);
                m_cv.wait(lk, [this]{ return !m_running || !m_q.empty(); });
                if (!m_running && m_q.empty()) return;
                task = std::move(const_cast<Task&>(m_q.top()));
                m_q.pop();
            }
            task.fn();
        }
    }

    std::atomic<bool> m_running {false};
    std::priority_queue<Task> m_q; // guarded by m_mx
    std::mutex m_mx;
    std::condition_variable m_cv;
    std::vector<std::thread> m_workers;
};
