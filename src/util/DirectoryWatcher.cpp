#include "DirectoryWatcher.h"
#include <vector>

DirectoryWatcher::DirectoryWatcher(const std::wstring& dir, OnChange cb)
: m_dir(dir), m_cb(std::move(cb)) {
    m_dirHandle = CreateFileW(m_dir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (m_dirHandle != INVALID_HANDLE_VALUE) {
        m_thread = std::thread(&DirectoryWatcher::run, this);
    }
}

DirectoryWatcher::~DirectoryWatcher() {
    m_stop = true;
    if (m_dirHandle != INVALID_HANDLE_VALUE) CancelIoEx(m_dirHandle, nullptr);
    if (m_thread.joinable()) m_thread.join();
    if (m_dirHandle != INVALID_HANDLE_VALUE) CloseHandle(m_dirHandle);
}

void DirectoryWatcher::run() {
    std::vector<BYTE> buffer(16 * 1024);
    DWORD bytes;
    OVERLAPPED ov{}; ov.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    while (!m_stop) {
        if (!ReadDirectoryChangesW(m_dirHandle, buffer.data(), (DWORD)buffer.size(),
            FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            &bytes, &ov, nullptr)) break;

        HANDLE handles[1] = { ov.hEvent };
        if (WaitForMultipleObjects(1, handles, FALSE, 250) == WAIT_OBJECT_0) {
            FILE_NOTIFY_INFORMATION* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
            do {
                std::wstring changed(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                if (m_cb) m_cb(changed);
                if (!fni->NextEntryOffset) break;
                fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<BYTE*>(fni) + fni->NextEntryOffset);
            } while (true);
        }
    }
    CloseHandle(ov.hEvent);
}
