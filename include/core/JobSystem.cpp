#include "core/JobSystem.h"

#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#endif

#include <thread> // std::this_thread::yield (already pulled in by header, but harmless)

namespace core {

JobSystem::JobSystem(size_t thread_count) {
    // If caller passes 0, fall back to hardware_concurrency.
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }
    if (thread_count == 0) {
        thread_count = 1; // hardware_concurrency() may legally return 0
    }

    quitting_.store(false, std::memory_order_relaxed);

    workers_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this, i] {
            worker_loop(i);
        });
    }
}

JobSystem::~JobSystem() {
    // Try to finish all currently queued work first; the main thread will help.
    flush();

    // Signal workers to shut down and wake any that are blocked in pop().
    quitting_.store(true, std::memory_order_release);
    queue_.stop();

    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void JobSystem::worker_loop(size_t /*index*/) {
    // Optional: name worker threads for easier debugging / profiling.
    set_thread_name_win32(L"JobWorker");

    Job job;
    for (;;) {
        if (quitting_.load(std::memory_order_acquire)) {
            break;
        }

        // Blocks until a job is available or stop() is called.
        if (!queue_.pop(job)) {
            // queue_.stop() was called and the queue is empty.
            break;
        }

        run_guarded(std::move(job));
    }
}

void JobSystem::flush() {
    // Drain the queue and wait for all active jobs to finish.
    for (;;) {
        Job job;

        // First try to steal a job from the queue and execute it on this thread.
        if (queue_.try_pop(job)) {
            run_guarded(std::move(job));
            continue;
        }

        // If there is no queued work and no active jobs, we are done.
        if (queue_.empty() &&
            active_.load(std::memory_order_acquire) == 0) {
            break;
        }

        // Nothing to do locally but other threads are still working.
        // Yield so worker threads can make progress without busy-waiting.
        std::this_thread::yield();
    }
}

void JobSystem::set_thread_name_win32(const wchar_t* name) {
#if defined(_WIN32)
    // Use SetThreadDescription when available (Windows 10+).
    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    if (!kernel) {
        return;
    }

    using SetThreadDescriptionFn = HRESULT (WINAPI *)(HANDLE, PCWSTR);
    auto setThreadDescription =
        reinterpret_cast<SetThreadDescriptionFn>(
            ::GetProcAddress(kernel, "SetThreadDescription"));

    if (setThreadDescription) {
        const wchar_t* finalName =
            (name && *name) ? name : L"JobWorker";

        setThreadDescription(::GetCurrentThread(), finalName);
    }
#else
    (void)name;
#endif
}

} // namespace core
