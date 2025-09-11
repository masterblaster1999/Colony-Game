#include "platform/resource_path.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
    std::filesystem::path exe_dir() {
        // Use a dynamic buffer in case of long paths.
        std::wstring buffer(32768, L'\0');
        DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(),
                                         static_cast<DWORD>(buffer.size()));
        if (len == 0) return {};
        buffer.resize(len);
        std::filesystem::path exe{buffer};
        return exe.parent_path();
    }
}

namespace platform {
    std::filesystem::path resource_root() {
        auto dir = exe_dir();
        if (!dir.empty()) {
            auto candidate = dir / L"resources";
            if (std::filesystem::exists(candidate)) return candidate;

            // common layout: <repo>/bin/Game.exe and <repo>/resources/...
            auto parent_candidate = dir.parent_path() / L"resources";
            if (std::filesystem::exists(parent_candidate)) return parent_candidate;
        }
        // fallback to CWD/resources for dev runs
        return std::filesystem::current_path() / L"resources";
    }
}
