// platform/win/WinPlatformPaths.h (add)
inline std::wstring SavedGamesDirW()
{
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DEFAULT, nullptr, &p))) {
        std::wstring out(p);
        CoTaskMemFree(p);
        out += L"\\Colony-Game";
        CreateDirectoryW(out.c_str(), nullptr);
        return out;
    }
    return L".";
}
