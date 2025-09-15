#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>

namespace win {

std::filesystem::path ExecutableDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        // Fallback: current directory if something went wrong
        return std::filesystem::current_path();
    }
    std::filesystem::path exe = std::filesystem::path(std::wstring(buf, n));
    return exe.parent_path();
}

void SetWorkingDirToExecutableDir() {
    std::filesystem::current_path(ExecutableDir());
}

} // namespace win
