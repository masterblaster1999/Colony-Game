#pragma once

#include <string>
#include <filesystem>

namespace util
{
    /// Quote a string with "..." (useful for logging or shell commands).
    std::string Quoted(const std::string& s);

    /// Open a path in Windows Explorer.
    /// - If it's a directory: opens the folder.
    /// - If it's a file: opens Explorer with the file selected.
    /// Windows only.
    void OpenInExplorer(const std::filesystem::path& path);
} // namespace util
