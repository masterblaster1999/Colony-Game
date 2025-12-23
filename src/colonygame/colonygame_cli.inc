//======================================================================================
// CLI parsing
//======================================================================================
struct LaunchOptions {
    std::optional<UINT> width;
    std::optional<UINT> height;
    std::optional<bool> fullscreen;
    std::optional<bool> vsync;
    std::optional<std::wstring> profile;
    std::optional<std::wstring> lang;
    std::optional<bool> skipIntro;
    std::optional<bool> safeMode;
    std::optional<uint64_t> seed;
    std::optional<std::wstring> configFile;
    std::optional<bool> rawInput; // NEW
    bool validateOnly = false;
};

static bool StartsWith(const std::wstring& s, const std::wstring& p) {
    if (s.size() < p.size()) return false;
    return std::equal(p.begin(), p.end(), s.begin());
}
static std::optional<std::wstring> ValueOrNext(const std::vector<std::wstring>& args, size_t& i) {
    const auto& a = args[i];
    size_t eq = a.find(L'=');
    if (eq != std::wstring::npos) return a.substr(eq+1);
    if (i+1 < args.size()) {
        const auto& nxt = args[i+1];
        if (!(StartsWith(nxt, L"-") || StartsWith(nxt, L"--"))) { ++i; return nxt; }
    }
    return std::nullopt;
}
static bool ParseBoolFlag(const std::optional<std::wstring>& v, bool fallback) {
    if (!v.has_value()) return fallback;
    return ParseBool(*v, fallback);
}
static LaunchOptions ParseArgs(int argc, wchar_t** argv) {
    LaunchOptions opt;
    std::vector<std::wstring> args; args.reserve(argc);
    for (int i=0;i<argc;++i) args.push_back(argv[i]);

    for (size_t i=1;i<args.size();++i) {
        const auto& a = args[i];
        if (a == L"-h" || a == L"--help") {
            MessageBoxW(nullptr,
                L"Colony Game — Windows Build\n\n"
                L"Options:\n"
                L"  --config <file>\n"
                L"  --profile <name>\n"
                L"  --lang <code>\n"
                L"  --res <WxH>\n"
                L"  --width <px>\n"
                L"  --height <px>\n"
                L"  --fullscreen [true|false]\n"
                L"  --vsync [true|false]\n"
                L"  --seed <n|random>\n"
                L"  --safe-mode\n"
                L"  --skip-intro\n"
                L"  --raw-input [true|false]\n"
                L"  --validate\n", L"Help", MB_OK|MB_ICONINFORMATION);
            ExitProcess(0);
        } else if (a == L"--validate") {
            opt.validateOnly = true;
        } else if (StartsWith(a, L"--config")) {
            if (auto v = ValueOrNext(args, i)) opt.configFile = *v;
        } else if (StartsWith(a, L"--profile")) {
            if (auto v = ValueOrNext(args, i)) opt.profile = *v;
        } else if (StartsWith(a, L"--lang")) {
            if (auto v = ValueOrNext(args, i)) opt.lang = *v;
        } else if (StartsWith(a, L"--res")) {
            if (auto v = ValueOrNext(args, i)) {
                if (auto r = ParseRes(*v)) { opt.width = r->first; opt.height = r->second; }
            }
        } else if (StartsWith(a, L"--width")) {
            if (auto v = ValueOrNext(args, i)) { UINT w=(UINT)_wtoi(v->c_str()); if (w>0) opt.width=w; }
        } else if (StartsWith(a, L"--height")) {
            if (auto v = ValueOrNext(args, i)) { UINT h=(UINT)_wtoi(v->c_str()); if (h>0) opt.height=h; }
        } else if (StartsWith(a, L"--fullscreen")) {
            auto v = ValueOrNext(args, i); opt.fullscreen = ParseBoolFlag(v, true);
        } else if (StartsWith(a, L"--vsync")) {
            auto v = ValueOrNext(args, i); opt.vsync = ParseBoolFlag(v, true);
        } else if (StartsWith(a, L"--skip-intro")) {
            opt.skipIntro = true;
        } else if (StartsWith(a, L"--safe-mode")) {
            opt.safeMode = true;
        } else if (StartsWith(a, L"--raw-input")) {
            auto v = ValueOrNext(args, i); opt.rawInput = ParseBoolFlag(v, true);
        } else if (StartsWith(a, L"--seed")) {
            if (auto v = ValueOrNext(args, i)) {
                std::wstring s=*v; std::wstring tl=s; std::transform(tl.begin(),tl.end(),tl.begin(),::towlower);
                if (tl==L"random" || s.empty()) opt.seed.reset();
                else {
                    wchar_t* end=nullptr; unsigned long long val = wcstoull(s.c_str(), &end, 10);
                    if (end && *end==0) opt.seed = (uint64_t)val;
                }
            }
        }
    }
    return opt;
}
static Config MakeEffectiveConfig(const Config& file, const LaunchOptions& cli) {
    Config eff=file;
    if (cli.width) eff.width = *cli.width;
    if (cli.height) eff.height = *cli.height;
    if (cli.fullscreen) eff.fullscreen = *cli.fullscreen;
    if (cli.vsync) eff.vsync = *cli.vsync;
    if (cli.profile && !cli.profile->empty()) eff.profile = *cli.profile;
    if (cli.lang && !cli.lang->empty()) eff.lang = *cli.lang;
    if (cli.skipIntro) eff.skipIntro = *cli.skipIntro;
    if (cli.safeMode) eff.safeMode = *cli.safeMode;
    if (cli.rawInput) eff.rawInput = *cli.rawInput;
    if (cli.seed.has_value()) eff.seed = cli.seed;
    return eff;
}

//======================================================================================
// Validate installation
//======================================================================================
static bool ValidateInstallation(std::wstring* messageOut=nullptr) {
    auto cwd = util::ExeDir();
    auto assets = util::JoinPath(cwd, L"assets");
    bool ok = util::DirExists(assets);
    if (!ok) {
        if (messageOut) *messageOut = L"assets\\ not found next to the executable.";
        return false;
    }
    // Light subfolders (non-fatal if missing, but warn)
    auto core = util::JoinPath(assets, L"core");
    auto locale = util::JoinPath(assets, L"locale");
    std::wstring dummy;
    if (!util::DirExists(core)) dummy += L"Missing assets\\core. ";
    if (!util::DirExists(locale)) dummy += L"Missing assets\\locale. ";
    if (messageOut) *messageOut = dummy;
    return true;
}

