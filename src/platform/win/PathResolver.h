#pragma once
#include <filesystem>
#include <string_view>

namespace platform::win {

    // Call once, as early as possible (before loading files).
    void BootstrapWorkingDir();

    // Returns an absolute path to something under the res/ tree.
    // Example: ResourcePath(L"ui/main_menu.json");
    std::filesystem::path ResourcePath(std::wstring_view relUnderRes);

    // Optional: verify res/ exists; logs a debug string and message box on failure.
    bool VerifyResourceRoot();

} // namespace platform::win
