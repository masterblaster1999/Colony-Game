#pragma once
#include <string>

struct AppConfig {
    bool forceWarp     = false;     // force software fallback
    bool useD3D12      = true;      // allow toggling 11/12
    bool rawInputSink  = false;     // receive input in background
    std::wstring logDir;
};

bool LoadAppConfig(AppConfig& out, const std::wstring& exeDir);
