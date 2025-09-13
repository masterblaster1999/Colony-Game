#pragma once
#ifdef _WIN32
#include <string>
namespace winboot {
bool AcquireSingleInstance(const std::wstring& mutexName, bool allowMultipleInstancesForDev);
}
#endif
