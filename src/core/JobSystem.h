#pragma once
#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace core {

class JobSystem {
public:
    JobSystem() = default;
    ~JobSystem() { Stop(); }

    void Start(unsigned workerCount = 0);
    void Stop();

    void Enqueue(std::function<void()> job);

private:
    void WorkerLoop();

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_jobs;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{false};
};

} // namespace core
