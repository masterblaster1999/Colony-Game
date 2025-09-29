#pragma once
#include <string>
#include <windows.h>
#include <shlobj_core.h>

inline std::wstring GetSavedGamesDir()
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DEFAULT, nullptr, &path)))
    {
        result.assign(path);
        CoTaskMemFree(path);
    }
    return result; // e.g., C:\Users\<User>\Saved Games
}
