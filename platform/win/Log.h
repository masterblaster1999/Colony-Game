#pragma once
#include <string>

namespace logsys {
    void Init();                   // creates logs/, rotates previous log
    void Write(const wchar_t* s);  // thread-safe, adds timestamp & OutputDebugString
    std::wstring FormatLastError(unsigned long e = 0); // formats GetLastError
}

#define LOGI(msg) do { logsys::Write(L"[INFO]  " L##msg); } while(0)
#define LOGW(msg) do { logsys::Write(L"[WARN]  " L##msg); } while(0)
#define LOGE(msg) do { logsys::Write(L"[ERROR] " L##msg); } while(0)
