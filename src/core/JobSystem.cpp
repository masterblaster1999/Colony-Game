#include "JobSystem.h"

#include <algorithm>   // std::max
#include <exception>   // std::exception

namespace core {

void JobSystem::Start(unsigned workerCount) {
    if (m_running.exchange(true)) return;
    if (workerCount == 0) workerCount = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < workerCount; ++i) {
        m_workers.emplace_back(&JobSystem::WorkerLoop, this);
    }
}

void JobSystem::Stop() {
    if (!m_running.exchange(false)) return;
    m_cv.notify_all();
    for (auto& t : m_workers) if (t.joinable()) t.join();
    m_workers.clear();
}

void JobSystem::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push(std::move(job));
    }
    m_cv.notify_one();
}

void JobSystem::WorkerLoop() {
    while (m_running) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]{ return !m_running || !m_jobs.empty(); });
            if (!m_running) break;
            job = std::move(m_jobs.front());
            m_jobs.pop();
        }
        try {
            job();
        } catch (const std::exception&) {
            // Swallow exceptions so a single bad job doesn't terminate the worker thread.
            // Consider wiring this up to your logger if you want visibility.
        } catch (...) {
            // Swallow non-standard exceptions.
        }
    }
}

} // namespace core
