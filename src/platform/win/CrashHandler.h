#pragma once
#ifdef _WIN32
#include <string>
namespace winboot {
void InstallCrashHandler(const std::wstring& appName);
}
#endif
