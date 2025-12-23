//======================================================================================
// App paths / Config
//======================================================================================
static const wchar_t* kAppName = L"MarsColonySim";

struct AppPaths {
    std::wstring configDir;     // %APPDATA%\MarsColonySim
    std::wstring dataDir;       // %LOCALAPPDATA%\MarsColonySim
    std::wstring savesDir;      // dataDir\Saves
    std::wstring logsDir;       // dataDir\Logs
    std::wstring modsDir;       // dataDir\Mods
    std::wstring screenshotsDir;// dataDir\Screenshots
    std::wstring defaultConfig; // configDir\settings.ini
};

static AppPaths ComputePaths() {
    AppPaths p;
    std::wstring appdata  = util::GetEnv(L"APPDATA");
    std::wstring localapp = util::GetEnv(L"LOCALAPPDATA");
    if (appdata.empty())  appdata  = util::KnownFolderPath(FOLDERID_RoamingAppData);
    if (localapp.empty()) localapp = util::KnownFolderPath(FOLDERID_LocalAppData);
    p.configDir      = util::JoinPath(appdata,  kAppName);
    p.dataDir        = util::JoinPath(localapp, kAppName);
    p.savesDir       = util::JoinPath(p.dataDir, L"Saves");
    p.logsDir        = util::JoinPath(p.dataDir, L"Logs");
    p.modsDir        = util::JoinPath(p.dataDir, L"Mods");
    p.screenshotsDir = util::JoinPath(p.dataDir, L"Screenshots");
    p.defaultConfig  = util::JoinPath(p.configDir, L"settings.ini");
    util::EnsureDir(p.configDir);
    util::EnsureDir(p.dataDir);
    util::EnsureDir(p.savesDir);
    util::EnsureDir(p.logsDir);
    util::EnsureDir(p.modsDir);
    util::EnsureDir(p.screenshotsDir);
    return p;
}

// NEW: Debug helper that *uses* util::Quoted and util::OpenInExplorer.
// It only does anything if specific environment variables are set.
static void DebugOpenFoldersIfRequested(const AppPaths& paths)
{
    // If COLONY_DEBUG_OPEN_SAVES is set, open the saves folder and log it.
    {
        std::wstring env = util::GetEnv(L"COLONY_DEBUG_OPEN_SAVES");
        if (!env.empty()) {
            util::OpenInExplorer(paths.savesDir);
            std::wstring msg = L"[Debug] Opened save folder: " +
                               util::Quoted(paths.savesDir);
            g_log.Line(msg);
        }
    }

    // If COLONY_DEBUG_OPEN_LOGS is set, open the logs folder and log it.
    {
        std::wstring env = util::GetEnv(L"COLONY_DEBUG_OPEN_LOGS");
        if (!env.empty()) {
            util::OpenInExplorer(paths.logsDir);
            std::wstring msg = L"[Debug] Opened log folder: " +
                               util::Quoted(paths.logsDir);
            g_log.Line(msg);
        }
    }
}

struct Config {
    UINT width  = 1280;
    UINT height = 720;
    bool fullscreen = false;
    bool vsync      = true;
    bool skipIntro  = false;
    bool safeMode   = false;
    bool rawInput   = true;   // NEW: enable WM_INPUT raw mouse by default
    std::wstring profile = L"default";
    std::wstring lang    = L"en-US";
    std::optional<uint64_t> seed;
};

static std::wstring ReadFileW(const std::wstring& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return L"";
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Interpret config as UTF‑8
    return util::Widen(bytes);
}
static bool WriteFileW(const std::wstring& path, const std::wstring& content) {
    // Write config as UTF‑8 to match ReadFileW
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;
    std::string bytes = util::Narrow(content);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return true;
}
static void WriteDefaultConfig(const std::wstring& file, const Config& c) {
    std::wstringstream out;
    out << L"# Mars Colony Simulation - settings.ini\r\n"
        << L"# Windows game generated\r\n\r\n"
        << L"[Display]\r\n"
        << L"resolution=" << c.width << L"x" << c.height << L"\r\n"
        << L"fullscreen=" << (c.fullscreen ? L"true" : L"false") << L"\r\n"
        << L"vsync=" << (c.vsync ? L"true" : L"false") << L"\r\n\r\n"
        << L"[General]\r\n"
        << L"profile=" << c.profile << L"\r\n"
        << L"lang="    << c.lang    << L"\r\n\r\n"
        << L"[Startup]\r\n"
        << L"skip_intro=" << (c.skipIntro ? L"true" : L"false") << L"\r\n"
        << L"safe_mode="  << (c.safeMode  ? L"true" : L"false") << L"\r\n"
        << L"seed="       << (c.seed ? std::to_wstring(*c.seed) : L"") << L"\r\n\r\n"
        << L"[Input]\r\n"
        << L"raw_input="  << (c.rawInput ? L"true" : L"false") << L"\r\n";
    WriteFileW(file, out.str());
}
static bool ParseBool(const std::wstring& s, bool fallback=false) {
    std::wstring t = s; std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    if (t==L"1" || t==L"true" || t==L"yes" || t==L"on"  || t==L"enable" || t==L"enabled") return true;
    if (t==L"0" || t==L"false"|| t==L"no"  || t==L"off" || t==L"disable"|| t==L"disabled") return false;
    return fallback;
}
static std::optional<uint64_t> ParseU64(const std::wstring& s) {
    if (s.empty()) return std::nullopt;
    wchar_t* end = nullptr;
    unsigned long long v = wcstoull(s.c_str(), &end, 10);
    if (!end || *end!=0) return std::nullopt;
    return (uint64_t)v;
}
static std::optional<std::pair<UINT,UINT>> ParseRes(const std::wstring& v) {
    size_t x = v.find(L'x');
    if (x == std::wstring::npos) return std::nullopt;
    UINT w = (UINT)_wtoi(v.substr(0, x).c_str());
    UINT h = (UINT)_wtoi(v.substr(x+1).c_str());
    if (w==0 || h==0) return std::nullopt;
    return std::make_pair(w,h);
}
static Config LoadConfig(const std::wstring& file, bool createIfMissing, const Config& defaults) {
    if (!util::FileExists(file)) {
        if (createIfMissing) WriteDefaultConfig(file, defaults);
        return defaults;
    }
    Config c = defaults;
    std::wistringstream in(ReadFileW(file));
    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::wstring t = line;
        auto posc = t.find_first_of(L"#;/");
        if (posc != std::wstring::npos && (t[posc] == L'#' || t[posc]==L';' || (t[posc]==L'/' && posc+1<t.size() && t[posc+1]==L'/')))
            t = t.substr(0, posc);
        auto pos = t.find(L'=');
        if (pos == std::wstring::npos) continue;
        std::wstring key = t.substr(0,pos); std::wstring val = t.substr(pos+1);
        key.erase(0, key.find_first_not_of(L" \t\r\n")); key.erase(key.find_last_not_of(L" \t\r\n")+1);
        val.erase(0, val.find_first_not_of(L" \t\r\n")); val.erase(val.find_last_not_of(L" \t\r\n")+1);
        std::wstring k; k.resize(key.size());
        std::transform(key.begin(), key.end(), k.begin(), ::towlower);

        if (k == L"resolution") {
            if (auto r = ParseRes(val)) { c.width = r->first; c.height = r->second; }
        } else if (k == L"fullscreen") {
            c.fullscreen = ParseBool(val, c.fullscreen);
        } else if (k == L"vsync") {
            c.vsync = ParseBool(val, c.vsync);
        } else if (k == L"profile") {
            if (!val.empty()) c.profile = val;
        } else if (k == L"lang") {
            if (!val.empty()) c.lang = val;
        } else if (k == L"skip_intro") {
            c.skipIntro = ParseBool(val, c.skipIntro);
        } else if (k == L"safe_mode") {
            c.safeMode = ParseBool(val, c.safeMode);
        } else if (k == L"seed") {
            c.seed = ParseU64(val);
        } else if (k == L"raw_input") {
            c.rawInput = ParseBool(val, c.rawInput);
        }
    }
    return c;
}

