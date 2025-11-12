#include "core/JobSystem.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <processthreadsapi.h>
#endif

namespace colony {

static void set_thread_background_priority() {
#ifdef _WIN32
    // Keep workers polite; adjust if you need more throughput.
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

void JobSystem::set_thread_name_win32(const wchar_t* name) {
#ifdef _WIN32
    // Per MS docs, SetThreadDescription is available via runtime dynamic linking
    // on Windows 10 version 1607 / Server 2016; load from KernelBase.dll.
    using SetThreadDescriptionFn = HRESULT (WINAPI *)(HANDLE, PCWSTR);
    HMODULE mod = ::GetModuleHandleW(L"KernelBase.dll");
    if (!mod) mod = ::GetModuleHandleW(L"Kernel32.dll");
    auto pSetThreadDescription = reinterpret_cast<SetThreadDescriptionFn>(
        ::GetProcAddress(mod, "SetThreadDescription"));
    if (pSetThreadDescription) {
        pSetThreadDescription(::GetCurrentThread(), name);
    }
#else
    (void)name;
#endif
}

JobSystem::JobSystem(size_t thread_count) {
    size_t hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 4;
    if (thread_count == 0) thread_count = hc > 1 ? hc - 1 : 1;

    workers_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
}

JobSystem::~JobSystem() {
    // Drain outstanding work (let main thread help as needed).
    flush();

    // Signal workers to stop and join.
    quitting_.store(true, std::memory_order_release);
    queue_.stop(); // wakes all workers
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void JobSystem::worker_loop(size_t index) {
#ifdef _WIN32
    wchar_t name[64];
    swprintf_s(name, L"JobWorker #%zu", index);
    set_thread_name_win32(name);
#endif
    set_thread_background_priority();

    for (;;) {
        Job j;
        if (!queue_.pop(j)) {
            // stopping && empty
            break;
        }
        run_guarded(std::move(j));
    }
}

void JobSystem::flush() {
    // Execute until queue is empty and no jobs are running.
    for (;;) {
        if (queue_.empty() && active_.load(std::memory_order_acquire) == 0) {
            return;
        }
        Job j;
        if (queue_.try_pop(j)) {
            run_guarded(std::move(j)); // let main thread help
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

} // namespace colony
