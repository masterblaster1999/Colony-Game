#include "PathResolver.h"
#include "platform/win/WinHeaders.hpp"
#include <filesystem>
#include <array>
#include <string>

namespace fs = std::filesystem;

namespace {
    fs::path exeDir()
    {
        std::array<wchar_t, 32768> buf{}; // supports long paths
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0 || len >= buf.size()) {
            // Fallback: current_path if API failed
            return fs::current_path();
        }
        return fs::path(buf.data()).parent_path();
    }

    void debugOut(const std::wstring& s)
    {
        ::OutputDebugStringW(s.c_str());
    }

    void showErrorBox(const std::wstring& s)
    {
        ::MessageBoxW(nullptr, s.c_str(), L"Colony Game – Startup", MB_ICONERROR | MB_OK);
    }
}

namespace platform::win {

    static fs::path g_resRoot;

    bool VerifyResourceRoot()
    {
        // We expect res/ next to the .exe after our CMake copy step.
        g_resRoot = exeDir() / L"res";
        if (fs::exists(g_resRoot) && fs::is_directory(g_resRoot))
            return true;

        // As a safety net, probe parents (useful if run from out/build/subdir manually).
        fs::path probe = exeDir();
        for (int i = 0; i < 3 && !fs::exists(g_resRoot); ++i) {
            probe = probe.parent_path();
            fs::path candidate = probe / L"res";
            if (fs::exists(candidate) && fs::is_directory(candidate)) {
                g_resRoot = candidate;
                break;
            }
        }

        if (!(fs::exists(g_resRoot) && fs::is_directory(g_resRoot))) {
            std::wstring msg = L"Could not locate the 'res' folder.\nExpected at: " 
                             + (exeDir() / L"res").wstring() + L"\n\n"
                             L"Make sure assets are deployed next to the executable.";
            debugOut(msg + L"\n");
            showErrorBox(msg);
            return false;
        }
        return true;
    }

    void BootstrapWorkingDir()
    {
        const fs::path dir = exeDir();
        ::SetCurrentDirectoryW(dir.c_str()); // ensure relative paths resolve from .exe folder
        VerifyResourceRoot(); // triggers helpful error if assets are missing
    }

    fs::path ResourcePath(std::wstring_view relUnderRes)
    {
        if (g_resRoot.empty())
            VerifyResourceRoot();
        return g_resRoot / fs::path(relUnderRes);
    }

} // namespace platform::win
