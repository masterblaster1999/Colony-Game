#pragma once
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <windows.h>

class DirectoryWatcher {
public:
    using OnChange = std::function<void(const std::wstring& path)>;

    DirectoryWatcher(const std::wstring& dir, OnChange cb);
    ~DirectoryWatcher();

private:
    void run();

    std::wstring m_dir;
    OnChange     m_cb;
    std::thread  m_thread;
    std::atomic<bool> m_stop{false};
    HANDLE m_dirHandle = INVALID_HANDLE_VALUE;
};
