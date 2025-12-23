//======================================================================================
// Windows Application Recovery & Restart (ARR)
// Register early so WER can call our recovery callback on crash/hang and restart us.
// Docs: RegisterApplicationRestart, RegisterApplicationRecoveryCallback, ApplicationRecoveryInProgress/Finished.
//======================================================================================
static DWORD WINAPI Colony_RecoveryCallback(PVOID /*ctx*/) {
    // Correct type: PBOOL -> BOOL*
    BOOL cancel = FALSE;
    // Ping WER; if the user canceled recovery, bail out quickly.
    // (Return value is HRESULT; pbCancelled conveys the cancel status.)
    (void)ApplicationRecoveryInProgress(&cancel);
    if (cancel) {
        ApplicationRecoveryFinished(FALSE);
        return 0;
    }

    // Write a tiny recovery marker/autosave into a stable, per-user location:
    // %LOCALAPPDATA%\MarsColonySim\Recovery\autosave.json
    std::wstring base = util::KnownFolderPath(FOLDERID_LocalAppData);
    if (base.empty()) base = util::ExeDir();
    std::wstring dir  = util::JoinPath(util::JoinPath(base, kAppName), L"Recovery");
    util::EnsureDir(dir);
    std::wstring file = util::JoinPath(dir, L"autosave.json");

    const std::wstring jsonW = L"{\"recovered\":true,\"reason\":\"WER\",\"version\":1}\n";
    const std::string  bytes = util::Narrow(jsonW);

    HANDLE h = CreateFileW(file.c_str(),
                           GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                           nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0; (void)WriteFile(h, bytes.data(), (DWORD)bytes.size(), &w, nullptr);
        FlushFileBuffers(h);
        CloseHandle(h);
    }

    ApplicationRecoveryFinished(TRUE); // signal success
    return 0;
}

static void InstallWindowsARR() {
    // Request OS to restart us after crash/hang/update with a recognizable flag.
    // Register before failure; WER restarts only if we've run ≥ 60 seconds.
    RegisterApplicationRestart(L"--restarted", 0);
    // Register recovery callback; ping interval 60s (we ping immediately in the callback).
    RegisterApplicationRecoveryCallback(&Colony_RecoveryCallback, nullptr, 60 * 1000, 0);
}

static bool WasRestartedByWer() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool restarted = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--restarted") == 0) { restarted = true; break; }
        }
        LocalFree(argv);
    }
    return restarted;
}

//======================================================================================
// XInput Dynamic Loader (no link dependency) + helpers
//======================================================================================
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);

static HMODULE            g_xiDLL            = nullptr;
static PFN_XInputGetState g_XInputGetState   = nullptr;
static PFN_XInputSetState g_XInputSetState   = nullptr;

static void LoadXInput()
{
    if (g_XInputGetState) return;
    const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll" };
    for (auto* name : dlls) {
        g_xiDLL = LoadLibraryW(name);
        if (!g_xiDLL) continue;
        g_XInputGetState = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(g_xiDLL, "XInputGetState"));
        g_XInputSetState = reinterpret_cast<PFN_XInputSetState>(GetProcAddress(g_xiDLL, "XInputSetState"));
        if (g_XInputGetState && g_XInputSetState) break;
        FreeLibrary(g_xiDLL); g_xiDLL = nullptr;
    }
}

static void UnloadXInput()
{
    g_XInputGetState = nullptr;
    g_XInputSetState = nullptr;
    if (g_xiDLL) { FreeLibrary(g_xiDLL); g_xiDLL = nullptr; }
}

// Normalize thumb value with proper dead-zone handling.
static float NormalizeThumb(SHORT v, SHORT deadzone)
{
    int iv = (int)v;
    int sign = (iv < 0) ? -1 : 1;
    int mag  = (iv < 0) ? -iv : iv;
    if (mag <= deadzone) return 0.0f;
    const float out = float(mag - deadzone) / float(32767 - deadzone);
    return sign * (out > 1.0f ? 1.0f : out);
}

//======================================================================================
// DPI: Enable Per‑Monitor v2 + helpers (graceful fallback for older systems)
//======================================================================================

// Don’t redefine DPI_AWARENESS_CONTEXT. Call SetProcessDpiAwarenessContext dynamically with HANDLE.
// If the SDK doesn’t provide the PMv2 constant, define a local one we pass to the function.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    #define COLONY_DPI_CTX_PMV2 ((HANDLE)-4)
#else
    #define COLONY_DPI_CTX_PMV2 DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#endif

static void EnablePerMonitorDpiV2()
{
    // Prefer Per‑Monitor v2 (Windows 10+) via SetProcessDpiAwarenessContext; fall back to SetProcessDPIAware.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetProcCtxFn = BOOL (WINAPI*)(HANDLE); // avoid referencing DPI_AWARENESS_CONTEXT type
        auto pSet = reinterpret_cast<SetProcCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (pSet) {
            if (pSet(COLONY_DPI_CTX_PMV2)) {
                return; // success
            }
        }
    }
    // Oldest fallback — at least avoid bitmap stretching.
    SetProcessDPIAware();
}

