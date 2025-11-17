#include "util/win_utils.h"

#include <Windows.h>

namespace util
{
    std::string Quoted(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        out.append(s);
        out.push_back('"');
        return out;
    }

    void OpenInExplorer(const std::filesystem::path& path)
    {
        const std::wstring wpath = path.wstring();

        // Check attributes to see if this is a directory
        const DWORD attrs = GetFileAttributesW(wpath.c_str());
        const bool isDir =
            attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDir)
        {
            // Just open the directory
            ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }

        // Otherwise, ask Explorer to select the file
        std::wstring args = L"/select,";
        args += wpath;

        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
} // namespace util
