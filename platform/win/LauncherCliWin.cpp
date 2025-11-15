// platform/win/LauncherCliWin.cpp

// Keep Windows headers tidy and predictable in this TU only.
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>   // GetCommandLineW, LocalFree
#include <shellapi.h>  // CommandLineToArgvW

#include <cwchar>      // _wcsicmp, _wcsnicmp, wcslen
#include <string>
#include <string_view>
#include <vector>

#include "platform/win/LauncherCliWin.h"

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

namespace
{
    // Parse the current process command line into a vector of wide strings.
    std::vector<std::wstring> ParseCommandLineOnce()
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

        std::vector<std::wstring> args;
        if (!argv || argc <= 0)
            return args;

        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i)
        {
            args.emplace_back(argv[i]);
        }

        ::LocalFree(argv);
        return args;
    }

    // Cached command line arguments for this process.
    const std::vector<std::wstring>& GetArgs()
    {
        static const std::vector<std::wstring> kArgs = ParseCommandLineOnce();
        return kArgs;
    }

    inline bool ArgEquals(const std::wstring& arg, const wchar_t* name)
    {
        return ::_wcsicmp(arg.c_str(), name) == 0;
    }

    inline bool ArgEquals(const std::wstring& arg, const std::wstring& name)
    {
        return ::_wcsicmp(arg.c_str(), name.c_str()) == 0;
    }

    // Build canonical flag names for convenience:
    // - if name starts with '-' or '/', we treat it literally;
    // - otherwise we match --name, -name, /name.
    struct FlagNames
    {
        std::wstring literal; // as given, if it starts with '-' or '/'
        std::wstring longDash;
        std::wstring shortDash;
        std::wstring slash;
    };

    FlagNames BuildFlagNames(const wchar_t* name)
    {
        FlagNames result;
        if (!name || !*name)
            return result;

        if (*name == L'-' || *name == L'/')
        {
            result.literal = name;
        }
        else
        {
            result.longDash  = L"--";
            result.longDash += name;

            result.shortDash  = L"-";
            result.shortDash += name;

            result.slash  = L"/";
            result.slash += name;
        }
        return result;
    }

    bool MatchesAnyFlagForm(const std::wstring& arg, const FlagNames& f)
    {
        if (!f.literal.empty() && ArgEquals(arg, f.literal))
            return true;

        if (!f.longDash.empty() && ArgEquals(arg, f.longDash))
            return true;

        if (!f.shortDash.empty() && ArgEquals(arg, f.shortDash))
            return true;

        if (!f.slash.empty() && ArgEquals(arg, f.slash))
            return true;

        return false;
    }

    // Same as above but with "=value" appended (for --key=value style).
    struct FlagValuePrefixes
    {
        std::wstring literalEq;
        std::wstring longDashEq;
        std::wstring shortDashEq;
        std::wstring slashEq;
    };

    FlagValuePrefixes BuildFlagValuePrefixes(const wchar_t* name)
    {
        FlagValuePrefixes result;
        if (!name || !*name)
            return result;

        if (*name == L'-' || *name == L'/')
        {
            result.literalEq = name;
            result.literalEq += L"=";
        }
        else
        {
            result.longDashEq  = L"--";
            result.longDashEq += name;
            result.longDashEq += L"=";

            result.shortDashEq  = L"-";
            result.shortDashEq += name;
            result.shortDashEq += L"=";

            result.slashEq  = L"/";
            result.slashEq += name;
            result.slashEq += L"=";
        }
        return result;
    }

    bool TryMatchPrefix(const std::wstring& arg, const std::wstring& prefix, std::wstring& outValue)
    {
        if (prefix.empty())
            return false;

        if (arg.size() <= prefix.size())
            return false;

        if (::_wcsnicmp(arg.c_str(), prefix.c_str(),
                        static_cast<unsigned>(prefix.size())) != 0)
        {
            return false;
        }

        outValue.assign(arg.begin() + static_cast<std::ptrdiff_t>(prefix.size()), arg.end());
        return true;
    }
} // anonymous namespace

// -----------------------------------------------------------------------------
// Public API (declared in LauncherCliWin.h)
// -----------------------------------------------------------------------------

// Quotes a single argument according to the rules used by CreateProcess/CommandLineToArgvW.
// See MS docs on CommandLineToArgvW for the backslash + quote behaviour. :contentReference[oaicite:1]{index=1}
std::wstring QuoteArgWindows(const std::wstring& arg)
{
    if (arg.empty())
        return L"\"\"";

    bool needsQuotes = false;
    for (wchar_t ch : arg)
    {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\v' || ch == L'"')
        {
            needsQuotes = true;
            break;
        }
    }

    if (!needsQuotes)
        return arg;

    std::wstring result;
    result.reserve(arg.size() + 2);
    result.push_back(L'"');

    std::size_t backslashCount = 0;

    for (wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++backslashCount;
        }
        else if (ch == L'"')
        {
            // We have a quote preceded by some backslashes.
            // Each backslash is doubled, then we add one more to escape the quote.
            result.append(backslashCount * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashCount = 0;
        }
        else
        {
            // Normal character; flush pending backslashes as-is.
            if (backslashCount > 0)
            {
                result.append(backslashCount, L'\\');
                backslashCount = 0;
            }
            result.push_back(ch);
        }
    }

    // At the end of the argument, any remaining backslashes must be doubled
    // because they precede the closing quote.
    if (backslashCount > 0)
    {
        result.append(backslashCount * 2, L'\\');
    }

    result.push_back(L'"');
    return result;
}

// Builds a single command line string for a child process by re‑using the
// current process's arguments (excluding argv[0]).
std::wstring BuildChildArguments()
{
    const auto& args = GetArgs();
    if (args.size() <= 1)
        return std::wstring();

    std::wstring result;
    bool first = true;

    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::wstring& a = args[i];

        // If you have launcher‑only flags you *don’t* want to forward to the
        // child process, filter them here, e.g.:
        //
        // if (ArgEquals(a, L"--launcher-only"))
        //     continue;

        if (!first)
            result.push_back(L' ');

        result += QuoteArgWindows(a);
        first = false;
    }

    return result;
}

// Looks for an argument of the form:
//
//   --name value
//   -name value
//   /name value
//   --name=value
//   -name=value
//   /name=value
//
// or, if `name` already starts with '-' or '/', uses it literally.
bool TryGetArgValue(const wchar_t* name, std::wstring& out)
{
    const auto& args = GetArgs();
    if (!name || args.size() <= 1)
        return false;

    const FlagNames flags   = BuildFlagNames(name);
    const FlagValuePrefixes prefixes = BuildFlagValuePrefixes(name);

    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::wstring& a = args[i];

        // key=value style
        if (TryMatchPrefix(a, prefixes.literalEq, out) ||
            TryMatchPrefix(a, prefixes.longDashEq, out) ||
            TryMatchPrefix(a, prefixes.shortDashEq, out) ||
            TryMatchPrefix(a, prefixes.slashEq, out))
        {
            return true;
        }

        // key value style
        if (MatchesAnyFlagForm(a, flags))
        {
            if (i + 1 < args.size())
            {
                out = args[i + 1];
                return true;
            }
        }
    }

    return false;
}

// Returns true if any of these forms is present:
//
//   name
//   --name
//   -name
//   /name
//
// If `name` starts with '-' or '/', it is used literally instead.
bool HasFlag(const wchar_t* name)
{
    const auto& args = GetArgs();
    if (!name || args.size() <= 1)
        return false;

    const FlagNames flags = BuildFlagNames(name);

    for (std::size_t i = 1; i < args.size(); ++i)
    {
        if (MatchesAnyFlagForm(args[i], flags))
            return true;
    }

    return false;
}
