#include "AppConfig.h"
#include <windows.h>
#include <pathcch.h>
#include <fstream>

bool LoadAppConfig(AppConfig& out, const std::wstring& exeDir) {
    wchar_t cfgPath[MAX_PATH]{};
    PathCchCombine(cfgPath, MAX_PATH, exeDir.c_str(), L"assets\\config\\app.ini");

    std::wifstream f(cfgPath);
    if (!f) return false;
    std::wstring key, eq, val;
    while (f >> key >> eq >> val) {
        if (key == L"forceWarp")    out.forceWarp = (val == L"1" || val == L"true");
        else if (key == L"useD3D12") out.useD3D12 = (val != L"0" && val != L"false");
        else if (key == L"rawInputSink") out.rawInputSink = (val == L"1" || val == L"true");
    }
    return true;
}
