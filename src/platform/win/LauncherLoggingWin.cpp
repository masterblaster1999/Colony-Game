// platform/win/LauncherLoggingWin.cpp
//
// Windows-only log helpers used by the launcher and (optionally) other startup paths
// such as AppMain / WinBootstrap / WinMain.
//
// Goals:
//  - Single source of truth for the logs directory: %LOCALAPPDATA%\ColonyGame\logs
//  - Per-process default log file naming (launcher.log for launcher; <exe>.log for others)
//  - Best-effort rotation + pruning of rotated logs
//  - Always mirror to OutputDebugStringW (Visual Studio Output window)
//  - Safe if OpenLogFile() is called multiple times in the same process:
//      * first call rotates + truncates (fresh log for this run)
//      * subsequent calls append and DO NOT rotate again

#include "platform/win/LauncherLoggingWin.h"

#include "platform/win/PathUtilWin.h"
#include "platform/win/WinCommon.h" // Windows headers + safe defines

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
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

    std::wstring ToLowerCopy(std::wstring s)
    {
        for (auto& ch : s)
        {
            // NOTE: std::towlower expects wint_t.
            ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
        }
        return s;
    }

    std::wstring SanitizeForFilename(std::wstring s)
    {
        // Windows forbids: < > : " / \ | ? *  and control chars.
        // Also avoid trailing spaces/dots.
        if (s.empty())
            return s;

        for (auto& ch : s)
        {
            // On Windows wchar_t is unsigned, so `ch >= 0` is redundant.
            const bool isCtrl = (ch < 32);
            switch (ch)
            {
                case L'<': case L'>': case L':': case L'"':
                case L'/': case L'\\': case L'|': case L'?': case L'*':
                    ch = L'_';
                    break;
                default:
                    if (isCtrl)
                        ch = L'_';
                    break;
            }
        }

        // Trim trailing spaces and dots (invalid in Windows filenames).
        while (!s.empty() && (s.back() == L' ' || s.back() == L'.'))
            s.pop_back();

        // Avoid crazy-long names.
        constexpr std::size_t kMaxLen = 64;
        if (s.size() > kMaxLen)
            s.resize(kMaxLen);

        return s;
    }

    std::wstring GetProcessExeStem()
    {
        // GetModuleFileNameW doesn't require any helper headers and works reliably for "current process".
        std::wstring buf;
        buf.resize(32768);

        DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};

        buf.resize(n);

        std::error_code ec;
        fs::path p(buf);
        std::wstring stem = p.stem().wstring();
        (void)ec;

        return stem;
    }

    // Decide the per-process base log name.
    // - If the exe name contains "launcher" (case-insensitive), keep legacy launcher.log naming.
    // - Otherwise use the exe stem (e.g. ColonyGame.log, Colony.exe, etc.)
    std::wstring DefaultLogBasename()
    {
        std::wstring stem = GetProcessExeStem();
        stem = SanitizeForFilename(stem);

        if (stem.empty())
            return L"launcher";

        const std::wstring lower = ToLowerCopy(stem);
        if (lower.find(L"launcher") != std::wstring::npos)
            return L"launcher"; // preserves existing launcher.log naming

        return stem;
    }

    // Best-effort pruning: keep newest N rotated logs that match "<prefix>*.log"
    // Example prefix: "launcher_" or "ColonyGame_"
    void PruneRotatedLogs(const fs::path& dir, const std::wstring& prefix, std::size_t keep_count)
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
            if (!prefix.empty() && name.rfind(prefix, 0) != 0)
                continue;

            entries.push_back(Entry{p, it.last_write_time(ec)});
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.time > b.time; // newest first
        });

        for (std::size_t i = keep_count; i < entries.size(); ++i)
        {
            (void)winpath::remove_with_retry(entries[i].path, &ec); // ignore failures
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
        ::OutputDebugStringW(L"[Log] LogsDir() empty; logging disabled.\n");
        return {};
    }

    const std::wstring baseName = DefaultLogBasename();
    const fs::path mainLog      = dir / (baseName + L".log");

    // If multiple startup paths call OpenLogFile() in the same process, avoid re-rotating.
    static std::wstring s_opened_main_log_path;
    const bool firstOpenForThisProcess =
        s_opened_main_log_path.empty() || s_opened_main_log_path != mainLog.wstring();

    if (firstOpenForThisProcess)
    {
        // Rotate existing <baseName>.log if present
        std::error_code ec;
        if (fs::exists(mainLog, ec))
        {
            const DWORD pid = ::GetCurrentProcessId();

            fs::path rotated = dir / (baseName + L"_" + TimestampForFilename() +
                                      L"_pid" + std::to_wstring(pid) + L".log");

            // Best-effort rename. If it fails (locked, permissions), we'll just overwrite.
            (void)winpath::rename_with_retry(mainLog, rotated, &ec);
        }

        // Keep newest 20 rotated logs for THIS baseName only.
        PruneRotatedLogs(dir, baseName + L"_", 20);

        s_opened_main_log_path = mainLog.wstring();
    }

    std::wofstream log;

    // First open: trunc (fresh log for this run)
    // Subsequent opens in the same process: append (avoid clobber/extra rotation)
    log.open(mainLog, std::ios::out | (firstOpenForThisProcess ? std::ios::trunc : std::ios::app));

    if (!log)
    {
        std::wstring msg = L"[Log] Failed to open log file: " + mainLog.wstring();
        DebugOutLine(msg);
        return {};
    }

    // Small header (kept minimal so it won't break anyone grepping old patterns).
    WriteLog(log,
             L"[Log] Opened. name=" + baseName +
             L" pid=" + std::to_wstring(::GetCurrentProcessId()) +
             L" file=" + mainLog.wstring());
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
