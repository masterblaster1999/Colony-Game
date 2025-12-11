// src/platform/win/WinUtils.h
#pragma once
#include <filesystem>
#include <windows.h>

inline std::filesystem::path GetExecutableDir()
{
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path exe = buf;
    exe.remove_filename();
    return exe;
}
