// platform/win/LauncherPreflightWin.cpp

#ifndef UNICODE
#   define UNICODE
#endif
#ifndef _UNICODE
#   define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include "platform/win/LauncherPreflightWin.h"

#include <windows.h> // GetEnvironmentVariableW
#include <sstream>

#include "platform/win/LauncherLoggingWin.h"

namespace fs = std::filesystem;

namespace winlaunch
{
    bool CheckEssentialFiles(const fs::path& root,
                             std::wstring&   errorOut,
                             std::wofstream& log)
    {
        struct Group
        {
            std::vector<fs::path> anyOf;
            const wchar_t*        label;
        };

        // At least one path in each group must exist.
        const std::vector<Group> groups = {
            // Content roots (allow "resources" as well as "assets" / "res").
            {
                { root / L"assets", root / L"res", root / L"resources" },
                L"Content (assets, res, or resources)"
            },
            // Shader roots (either legacy or new location).
            {
                { root / L"renderer" / L"Shaders", root / L"shaders" },
                L"Shaders (renderer/Shaders or shaders)"
            }
        };

        std::wstringstream missing;
        bool ok = true;

        for (const auto& g : groups)
        {
            bool found = false;

            for (const auto& p : g.anyOf)
            {
                if (fs::exists(p))
                {
                    WriteLog(log, L"[Launcher] Found: " + p.wstring());
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                ok = false;
                missing << L" - " << g.label << L"\n";
            }
        }

        if (!ok)
        {
            errorOut =
                L"Missing required content folders:\n\n" +
                missing.str() +
                L"\nPlease verify your installation directory contains the folders above.";
        }

        return ok;
    }

    std::optional<fs::path> EnvExeOverride()
    {
        wchar_t buf[1024];
        const DWORD n = ::GetEnvironmentVariableW(L"COLONY_GAME_EXE", buf, 1024);
        if (n != 0 && n < 1024)
            return fs::path(buf);

        return std::nullopt;
    }

    fs::path FindGameExecutable(const fs::path&         exeDir,
                                const std::wstring&     cliExeOverride,
                                std::wofstream&         log,
                                std::vector<fs::path>*  outCandidates)
    {
        std::vector<fs::path> candidates;

        // CLI override has highest priority.
        if (!cliExeOverride.empty())
            candidates.push_back(exeDir / cliExeOverride);

        // Environment override is next.
        if (auto envExe = EnvExeOverride())
        {
            if (envExe->is_absolute())
                candidates.push_back(*envExe);
            else
                candidates.push_back(exeDir / *envExe);
        }

        // Common target names (both old and new), plus a bin/ variant.
        candidates.push_back(exeDir / L"ColonyGame.exe");
        candidates.push_back(exeDir / L"Colony-Game.exe");
        candidates.push_back(exeDir / L"Colony.exe");
        candidates.push_back(exeDir / L"bin" / L"ColonyGame.exe");

        if (outCandidates)
            *outCandidates = candidates;

        for (const auto& c : candidates)
        {
            if (fs::exists(c))
            {
                WriteLog(log, L"[Launcher] Using game executable: " + c.wstring());
                return c;
            }
        }

        return fs::path{};
    }

    std::wstring BuildExeNotFoundMessage(const std::vector<fs::path>& candidates)
    {
        std::wstring msg = L"Could not find the game executable.\nTried:\n";
        for (const auto& c : candidates)
        {
            msg += L" - " + c.wstring() + L"\n";
        }
        return msg;
    }
}
