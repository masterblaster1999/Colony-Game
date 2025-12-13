// platform/win/LauncherLoggingWin.cpp

#include "platform/win/LauncherLoggingWin.h"

#include "platform/win/PathUtilWin.h"
#include "platform/win/WinCommon.h" // Windows headers + safe defines

#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // Safe filename timestamp: YYYYMMDD_HHMMSS_mmm
    std::wstring TimestampForFilename()
    {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);

        wchar_t buf[64]{};
        // Example: 20251213_093015_123
        swprintf_s(buf,
                   L"%04u%02u%02u_%02u%02u%02u_%03u",
                   st.wYear,
                   st.wMonth,
                   st.wDay,
                   st.wHour,
                   st.wMinute,
                   st.wSecond,
                   st.wMilliseconds);

        return std::wstring(buf);
    }

    // Best-effort pruning: keep newest N rotated logs that match launcher_*.log
    void PruneRotatedLogs(const fs::path& dir, std::size_t keep_count)
    {
        if (dir.empty() || keep_count == 0)
            return;

        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
            return;

        struct Entry
        {
            fs::path           path;
            fs::file_time_type time;
        };

        std::vector<Entry> entries;
        for (const auto& it : fs::directory_iterator(dir, ec))
        {
            if (ec)
                break;

            if (!it.is_regular_file(ec))
                continue;

            const fs::path p = it.path();

            if (p.extension() != L".log")
                continue;

            const std::wstring name = p.filename().wstring();

            // rotated logs: launcher_YYYY..._pid....log
            if (name.rfind(L"launcher_", 0) != 0)
                continue;

            entries.push_back(Entry{p, it.last_write_time(ec)});
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.time > b.time; // newest first
        });

        for (std::size_t i = keep_count; i < entries.size(); ++i)
        {
            fs::remove(entries[i].path, ec); // ignore failures
        }
    }

    void DebugOutLine(const std::wstring& line)
    {
        std::wstring s = line;
        s.push_back(L'\n');
        ::OutputDebugStringW(s.c_str());
    }
} // namespace

std::filesystem::path LogsDir()
{
    // winpath::writable_data_dir() already creates %LOCALAPPDATA%\ColonyGame (best effort)
    const fs::path base = winpath::writable_data_dir();
    if (base.empty())
        return {};

    const fs::path logs = base / L"logs";

    std::error_code ec;
    fs::create_directories(logs, ec); // best effort; ignore failures

    return logs;
}

std::wofstream OpenLogFile()
{
    const fs::path dir = LogsDir();
    if (dir.empty())
    {
        ::OutputDebugStringW(L"[Launcher][Log] LogsDir() empty; logging disabled.\n");
        return {};
    }

    const fs::path mainLog = dir / L"launcher.log";

    // Rotate existing launcher.log if present
    {
        std::error_code ec;
        if (fs::exists(mainLog, ec))
        {
            const DWORD pid = ::GetCurrentProcessId();

            fs::path rotated =
                dir / (L"launcher_" + TimestampForFilename() + L"_pid" + std::to_wstring(pid) + L".log");

            // Best-effort rename. If it fails (locked, permissions), we'll just overwrite.
            fs::rename(mainLog, rotated, ec);
        }

        // Keep newest 20 rotated logs.
        PruneRotatedLogs(dir, 20);
    }

    std::wofstream log;
    log.open(mainLog, std::ios::out | std::ios::trunc);

    if (!log)
    {
        ::OutputDebugStringW(L"[Launcher][Log] Failed to open %LOCALAPPDATA%\\ColonyGame\\logs\\launcher.log\n");
        return {};
    }

    // Small header (kept minimal so it won't break anyone grepping old patterns).
    WriteLog(log, L"[Launcher] Log opened. pid=" + std::to_wstring(::GetCurrentProcessId()));
    return log;
}

void WriteLog(std::wofstream& log, const std::wstring& line)
{
    WriteLog(static_cast<std::wostream&>(log), line);
}

void WriteLog(std::wostream& log, const std::wstring& line)
{
    // Always mirror to the debugger (useful even if the file isn't open).
    DebugOutLine(line);

    if (!log.good())
        return;

    log << line << L"\n";
    log.flush();
}
