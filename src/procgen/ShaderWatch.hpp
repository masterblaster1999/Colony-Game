#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <functional>
#include <atomic>

// Hot reload using ReadDirectoryChangesW (Windows doc & examples). :contentReference[oaicite:7]{index=7}
class ShaderWatch {
public:
    using Callback = std::function<void(const std::wstring&)>;
    ShaderWatch(const std::wstring& dir, Callback cb): m_dir(dir), m_cb(cb) {}
    ~ShaderWatch(){ stop(); }
    void start(){
        if (m_running.exchange(true)) return;
        m_thr = std::thread([this]{ run(); });
    }
    void stop(){
        if (!m_running.exchange(false)) return;
        CancelIoEx(m_dirHandle, nullptr);
        if (m_thr.joinable()) m_thr.join();
        if (m_dirHandle) { CloseHandle(m_dirHandle); m_dirHandle=nullptr; }
    }
private:
    void run(){
        m_dirHandle = CreateFileW(m_dir.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (m_dirHandle==INVALID_HANDLE_VALUE) return;

        BYTE buffer[4096];
        OVERLAPPED ov{}; HANDLE evt = CreateEventW(nullptr, TRUE, FALSE, nullptr); ov.hEvent = evt;

        while (m_running) {
            DWORD bytes=0; ResetEvent(evt);
            BOOL ok = ReadDirectoryChangesW(m_dirHandle, buffer, sizeof(buffer), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_FILE_NAME, &bytes, &ov, nullptr);
            if (!ok) break;
            WaitForSingleObject(evt, INFINITE);
            FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buffer;
            do {
                std::wstring fname(info->FileName, info->FileNameLength/sizeof(WCHAR));
                if (fname.rfind(L".cso")!=std::wstring::npos) m_cb(m_dir + L"\\" + fname);
                info = (info->NextEntryOffset) ? (FILE_NOTIFY_INFORMATION*)((BYTE*)info + info->NextEntryOffset) : nullptr;
            } while(info && m_running);
        }
        CloseHandle(evt);
    }
    std::wstring m_dir;
    Callback m_cb;
    std::thread m_thr;
    std::atomic<bool> m_running{false};
    HANDLE m_dirHandle = nullptr;
};
