//======================================================================================
// Entry point and bootstrap
//======================================================================================
static int ValidateMain(const AppPaths& paths, const LaunchOptions& cli, const Config& eff) {
    std::wstring msg;
    bool ok = ValidateInstallation(&msg);
    // Optional: write settings if given
    if (cli.configFile) {
        WriteDefaultConfig(*cli.configFile, eff);
    } else {
        WriteDefaultConfig(paths.defaultConfig, eff);
    }
    // Return 0 on success like a validator should.
    return ok ? 0 : 4;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Prefer Per‑Monitor v2 DPI awareness (fallback handled internally).
    EnablePerMonitorDpiV2();

    // ARR: register restart and recovery very early, before heavy init.
    InstallWindowsARR();

    AppPaths paths = ComputePaths();

    // NEW: Debug: open saves/logs in Explorer if env vars are set.
    DebugOpenFoldersIfRequested(paths);

    // Log file
    std::wstring logFile = util::JoinPath(paths.logsDir, L"ColonyGame-" + util::NowStampCompact() + L".log");
    g_log.Open(logFile);
    g_log.Line(L"colonygame.exe starting …");

    // Note if this instance was restarted by WER.
    if (WasRestartedByWer()) {
        g_log.Line(L"[ARR] Restarted after crash/hang; will attempt to load recovery if present.");
    }

    // Parse CLI
    int argc=0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    LaunchOptions cli = ParseArgs(argc, argv);
    if (argv) LocalFree(argv);

    // Load config
    std::wstring cfgPath = cli.configFile.value_or(paths.defaultConfig);
    Config defaults;
    Config fileCfg = LoadConfig(cfgPath, /*create*/ true, defaults);
    Config eff = MakeEffectiveConfig(fileCfg, cli);

    // Seed fallback
    if (!eff.seed.has_value()) {
        std::random_device rd;
        eff.seed = (uint64_t(rd())<<32) ^ uint64_t(rd());
    }

    // Validate mode (no window)
    if (cli.validateOnly) {
        int rc = ValidateMain(paths, cli, eff);
        g_log.Line(util::Format(L"Validate exit code: %d", rc));
        CoUninitialize();
        return rc;
    }

    // Create/Run game
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);

    GameApp app(hInst, paths, eff);
    int rc = app.Run();

    // Clean up XInput loader
    UnloadXInput();

    g_log.Line(util::Format(L"Exit code: %d", rc));
    CoUninitialize();
    return rc;
}
