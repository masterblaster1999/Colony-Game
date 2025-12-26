#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <stdexcept>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace colony {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency())
        : m_stop(false) 
    {
        if (threadCount == 0) threadCount = 1;
        m_workers.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) {
            m_workers.emplace_back([this] {
                for (;;) {
                    Task task;
                    {
                        std::unique_lock lk(m_mutex);
                        m_cv.wait(lk, [this] { return m_stop.load(std::memory_order_acquire) || !m_tasks.empty(); });
                        if (m_stop.load(std::memory_order_acquire) && m_tasks.empty()) return;
                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard lk(m_mutex);
            m_stop.store(true, std::memory_order_release);
        }
        m_cv.notify_all();
        for (auto& t : m_workers) if (t.joinable()) t.join();
    }

    // Submit any callable; returns std::future<R>.
    template<class F, class...Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using R = std::invoke_result_t<F, Args...>;

        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto pkg   = std::make_shared<std::packaged_task<R()>>(std::move(bound));
        std::future<R> fut = pkg->get_future();

        {
            std::lock_guard lk(m_mutex);
            if (m_stop.load(std::memory_order_acquire)) {
                throw std::runtime_error("ThreadPool is stopping");
            }
            m_tasks.emplace([pkg]() { (*pkg)(); });
        }
        m_cv.notify_one();
        return fut;
    }

    std::size_t size() const noexcept { return m_workers.size(); }

private:
    using Task = std::function<void()>;

    std::vector<std::thread> m_workers;
    std::queue<Task>         m_tasks;
    std::mutex               m_mutex;
    std::condition_variable  m_cv;
    std::atomic<bool>        m_stop;
};

} // namespace colony
