// Mars Colony Simulation — SDL2 Launcher (C++17, single file)
// Hooks engine/windowing with SDL2 and a minimal renderer.
//
// Build (GCC/Clang):  g++ -std=c++17 -O2 -o MarsColonyLauncher Launcher_SDL.cpp `sdl2-config --cflags --libs`
// Build (MSVC via CMake): see CMakeLists.txt below

// ------------------------------- Standard lib --------------------------------
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cctype>
#include <limits>
#include <cmath>

namespace fs = std::filesystem;

// --------------------------------- SDL2 --------------------------------------
// Tell SDL we're providing our own main()
#define SDL_MAIN_HANDLED

#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  elif __has_include(<SDL.h>)
#    include <SDL.h>
#  else
#    error "SDL2 headers not found. Install SDL2 development headers and adjust include paths."
#  endif
#else
#  include <SDL.h> // Fallback; adjust include paths if needed
#endif

// ============================= Compile-time Platform =========================
static const char* PlatformName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

// =============================== Small Utilities =============================
namespace util {
inline std::string ltrim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){ return !std::isspace(ch); }));
    return s;
}
inline std::string rtrim(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){ return !std::isspace(ch); }).base(), s.end());
    return s;
}
inline std::string trim(std::string s) { return rtrim(ltrim(std::move(s))); }

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

inline bool parse_bool(const std::string& v, bool fallback=false) {
    std::string s = to_lower(trim(v));
    if (s=="1" || s=="true" || s=="yes" || s=="on"  || s=="enable" || s=="enabled") return true;
    if (s=="0" || s=="false"|| s=="no"  || s=="off" || s=="disable"|| s=="disabled") return false;
    return fallback;
}
inline std::optional<unsigned> parse_uint(const std::string& v) {
    try {
        if (v.empty()) return std::nullopt;
        size_t idx = 0;
        unsigned long val = std::stoul(v, &idx, 10);
        if (idx != v.size()) return std::nullopt;
        if (val > std::numeric_limits<unsigned>::max()) return std::nullopt;
        return static_cast<unsigned>(val);
    } catch (...) { return std::nullopt; }
}

struct Resolution { unsigned w=1280, h=720; };
inline std::optional<Resolution> parse_resolution(const std::string& s) {
    auto x = s.find('x');
    if (x == std::string::npos) return std::nullopt;
    auto w = parse_uint(s.substr(0, x));
    auto h = parse_uint(s.substr(x+1));
    if (!w || !h || *w==0 || *h==0) return std::nullopt;
    return Resolution{*w, *h};
}

inline std::string timestamp_compact() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    std::time_t t = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

inline std::string read_text_file(const fs::path& p) {
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
inline bool write_text_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << content;
    return static_cast<bool>(out);
}
} // namespace util

// ================================== Logging ==================================
enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    Logger() = default;
    ~Logger() { try { if (file_.is_open()) file_.flush(); } catch (...) {} }

    bool open(const fs::path& logfile, bool mirror_to_console = true) {
        fs::create_directories(logfile.parent_path());
        file_.open(logfile, std::ios::out | std::ios::app);
        mirror_ = mirror_to_console;
        ready_ = file_.is_open();
        return ready_;
    }

    void log(LogLevel lvl, const std::string& msg) {
        if (!ready_) return;
        const char* tag = lvl==LogLevel::Debug ? "DEBUG" :
                          lvl==LogLevel::Info  ? " INFO" :
                          lvl==LogLevel::Warn  ? " WARN" : "ERROR";
        std::ostringstream line;
        line << "[" << util::timestamp_compact() << "][" << tag << "] " << msg << "\n";
        file_ << line.str();
        file_.flush();
        if (mirror_) std::cerr << line.str();
    }

    void debug(const std::string& m) { log(LogLevel::Debug, m); }
    void info (const std::string& m) { log(LogLevel::Info , m); }
    void warn (const std::string& m) { log(LogLevel::Warn , m); }
    void error(const std::string& m) { log(LogLevel::Error, m); }

private:
    std::ofstream file_;
    bool mirror_ = true;
    bool ready_  = false;
};

static Logger g_log;

// ================================ App Paths ==================================
struct AppPaths {
    fs::path home;
    fs::path configDir;
    fs::path dataDir;
    fs::path savesDir;
    fs::path logsDir;
    fs::path modsDir;
    fs::path screenshotsDir;
    fs::path defaultConfigFile() const { return configDir / "settings.ini"; }
};

static AppPaths compute_paths(const std::string& appName) {
    AppPaths p;
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
    const char* appdata = std::getenv("APPDATA");
    const char* local = std::getenv("LOCALAPPDATA");
    p.home = home ? fs::path(home) : fs::path(".");
    fs::path cfgRoot = appdata ? fs::path(appdata) : p.home / "AppData" / "Roaming";
    fs::path datRoot = local  ? fs::path(local)  : p.home / "AppData" / "Local";
    p.configDir      = cfgRoot / appName;
    p.dataDir        = datRoot / appName;
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    p.home = home ? fs::path(home) : fs::path(".");
    fs::path base   = p.home / "Library" / "Application Support" / appName;
    p.configDir     = base / "Config";
    p.dataDir       = base / "Data";
#else // Linux and others
    const char* home = std::getenv("HOME");
    const char* xdg_conf = std::getenv("XDG_CONFIG_HOME");
    const char* xdg_data = std::getenv("XDG_DATA_HOME");
    p.home = home ? fs::path(home) : fs::path(".");
    p.configDir = xdg_conf ? fs::path(xdg_conf) / appName : p.home / ".config" / appName;
    p.dataDir   = xdg_data ? fs::path(xdg_data) / appName : p.home / ".local" / "share" / appName;
#endif
    p.savesDir       = p.dataDir / "Saves";
    p.logsDir        = p.dataDir / "Logs";
    p.modsDir        = p.dataDir / "Mods";
    p.screenshotsDir = p.dataDir / "Screenshots";
    return p;
}

static void ensure_directories(const AppPaths& p) {
    fs::create_directories(p.configDir);
    fs::create_directories(p.dataDir);
    fs::create_directories(p.savesDir);
    fs::create_directories(p.logsDir);
    fs::create_directories(p.modsDir);
    fs::create_directories(p.screenshotsDir);
}

// ================================ Configuration ==============================
struct Config {
    // Core presentation
    unsigned width  = 1280;
    unsigned height = 720;
    bool fullscreen = false;
    bool vsync      = true;

    // General
    std::string profile = "default";
    std::string lang    = "en-US";

    // Startup
    bool skipIntro = false;
    bool safeMode  = false;

    // RNG seed (optional). If empty → random at launch.
    std::optional<uint64_t> seed;
};

static void write_default_config(const fs::path& file, const Config& c) {
    std::ostringstream out;
    out << "# Mars Colony Simulation - settings.ini\n"
        << "# Lines beginning with #, ;, or // are comments\n\n"
        << "[Display]\n"
        << "resolution=" << c.width << "x" << c.height << "\n"
        << "fullscreen=" << (c.fullscreen ? "true" : "false") << "\n"
        << "vsync=" << (c.vsync ? "true" : "false") << "\n\n"
        << "[General]\n"
        << "profile=" << c.profile << "\n"
        << "lang="    << c.lang    << "\n\n"
        << "[Startup]\n"
        << "skip_intro=" << (c.skipIntro ? "true" : "false") << "\n"
        << "safe_mode="  << (c.safeMode  ? "true" : "false") << "\n"
        << "seed="       << (c.seed ? std::to_string(*c.seed) : "") << "\n";
    util::write_text_file(file, out.str());
}

static Config load_config(const fs::path& file, bool create_if_missing = true) {
    Config cfg{};
    if (!fs::exists(file)) {
        if (create_if_missing) {
            write_default_config(file, cfg);
        }
        return cfg;
    }

    std::istringstream in(util::read_text_file(file));
    std::string line;
    while (std::getline(in, line)) {
        std::string s = util::trim(line);
        if (s.empty()) continue;
        if (util::starts_with(s, "#") || util::starts_with(s, ";") || util::starts_with(s, "//")) continue;
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = util::to_lower(util::trim(s.substr(0, eq)));
        std::string val = util::trim(s.substr(eq + 1));

        if (key == "resolution") {
            if (auto r = util::parse_resolution(val)) { cfg.width = r->w; cfg.height = r->h; }
        } else if (key == "fullscreen") {
            cfg.fullscreen = util::parse_bool(val, cfg.fullscreen);
        } else if (key == "vsync") {
            cfg.vsync = util::parse_bool(val, cfg.vsync);
        } else if (key == "profile") {
            if (!val.empty()) cfg.profile = val;
        } else if (key == "lang") {
            if (!val.empty()) cfg.lang = val;
        } else if (key == "skip_intro") {
            cfg.skipIntro = util::parse_bool(val, cfg.skipIntro);
        } else if (key == "safe_mode") {
            cfg.safeMode = util::parse_bool(val, cfg.safeMode);
        } else if (key == "seed") {
            if (val.empty()) { cfg.seed.reset(); }
            else {
                try { cfg.seed = static_cast<uint64_t>(std::stoull(val)); }
                catch (...) { cfg.seed.reset(); }
            }
        }
    }
    return cfg;
}

// ================================ CLI Options ================================
struct LaunchOptions {
    // Parsed from CLI (optional overrides)
    std::optional<unsigned> width;
    std::optional<unsigned> height;
    std::optional<bool> fullscreen;
    std::optional<bool> vsync;

    std::optional<std::string> profile;
    std::optional<std::string> lang;

    std::optional<bool> skipIntro;
    std::optional<bool> safeMode;
    std::optional<uint64_t> seed;  // "random" on CLI maps to empty → then random

    std::optional<fs::path> configFile;
    bool validateOnly = false;
};

static void print_usage(const char* exe) {
    std::cout <<
R"(Mars Colony Simulation — Launcher (SDL2)

Usage:
  )" << exe << R"( [options]

Options:
  -h, --help                 Show this help and exit
  --config <file>            Use a specific settings.ini path
  --profile <name>           Player profile (default: "default")
  --lang <code>              Language code (e.g., en-US, es-ES)
  --res <WxH>                Resolution (e.g., 1920x1080)
  --width <px>               Override width only
  --height <px>              Override height only
  --fullscreen [true|false]  Fullscreen toggle
  --vsync [true|false]       VSync toggle
  --seed <n|random>          Fixed RNG seed or "random"
  --safe-mode                Start with conservative graphics/features
  --skip-intro               Skip intro/splash on launch
  --validate                 Validate installation and exit

Examples:
  )" << exe << R"( --res 1920x1080 --fullscreen --profile Commander --seed random
  )" << exe << R"( --validate
)";
}

static LaunchOptions parse_args(int argc, char** argv) {
    LaunchOptions opt;
    auto value_or_next = [&](const std::string& arg, int& i) -> std::optional<std::string> {
        auto eq = arg.find('=');
        if (eq != std::string::npos) return arg.substr(eq+1);
        if (i+1 < argc) {
            std::string nxt = argv[i+1];
            if (!util::starts_with(nxt, "-")) { ++i; return nxt; }
        }
        return std::nullopt;
    };

    for (int i=1; i<argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (a == "--validate") {
            opt.validateOnly = true;
        } else if (util::starts_with(a, "--config")) {
            if (auto v = value_or_next(a, i)) opt.configFile = fs::path(*v);
        } else if (util::starts_with(a, "--profile")) {
            if (auto v = value_or_next(a, i)) opt.profile = *v;
        } else if (util::starts_with(a, "--lang")) {
            if (auto v = value_or_next(a, i)) opt.lang = *v;
        } else if (util::starts_with(a, "--res")) {
            if (auto v = value_or_next(a, i)) {
                if (auto r = util::parse_resolution(*v)) { opt.width = r->w; opt.height = r->h; }
            }
        } else if (util::starts_with(a, "--width")) {
            if (auto v = value_or_next(a, i)) { if (auto w = util::parse_uint(*v)) opt.width = *w; }
        } else if (util::starts_with(a, "--height")) {
            if (auto v = value_or_next(a, i)) { if (auto h = util::parse_uint(*v)) opt.height = *h; }
        } else if (util::starts_with(a, "--fullscreen")) {
            if (auto v = value_or_next(a, i)) opt.fullscreen = util::parse_bool(*v, true);
            else opt.fullscreen = true;
        } else if (util::starts_with(a, "--vsync")) {
            if (auto v = value_or_next(a, i)) opt.vsync = util::parse_bool(*v, true);
            else opt.vsync = true;
        } else if (util::starts_with(a, "--skip-intro")) {
            opt.skipIntro = true;
        } else if (util::starts_with(a, "--safe-mode")) {
            opt.safeMode = true;
        } else if (util::starts_with(a, "--seed")) {
            if (auto v = value_or_next(a, i)) {
                std::string s = util::to_lower(*v);
                if (s == "random" || s.empty()) {
                    opt.seed.reset();
                } else {
                    try { opt.seed = static_cast<uint64_t>(std::stoull(*v)); }
                    catch (...) { /* ignore, leave unset */ }
                }
            }
        } else {
            std::cerr << "Warning: Unrecognized option: " << a << "\n";
        }
    }
    return opt;
}

// Merge CLI overrides (highest precedence) into config defaults/file.
static Config make_effective_config(const Config& file, const LaunchOptions& cli) {
    Config eff = file;
    if (cli.width)  eff.width  = *cli.width;
    if (cli.height) eff.height = *cli.height;
    if (cli.fullscreen) eff.fullscreen = *cli.fullscreen;
    if (cli.vsync)      eff.vsync = *cli.vsync;
    if (cli.profile && !cli.profile->empty()) eff.profile = *cli.profile;
    if (cli.lang    && !cli.lang->empty())    eff.lang    = *cli.lang;
    if (cli.skipIntro) eff.skipIntro = *cli.skipIntro;
    if (cli.safeMode)  eff.safeMode  = *cli.safeMode;
    if (cli.seed.has_value()) eff.seed = cli.seed; // may be empty to force random
    return eff;
}

// ================================ Crash Handling =============================
static volatile std::sig_atomic_t g_should_quit = 0;

static void signal_handler(int sig) {
    g_log.warn("Received signal " + std::to_string(sig) + " — requesting shutdown.");
    g_should_quit = 1;
}

static void terminate_handler() {
    try {
        auto ex = std::current_exception();
        if (ex) std::rethrow_exception(ex);
    } catch (const std::exception& e) {
        g_log.error(std::string("Unhandled exception: ") + e.what());
    } catch (...) {
        g_log.error("Unhandled unknown exception.");
    }
    std::abort();
}

// ============================== Bootstrap Stubs ==============================
static void print_splash(bool skipIntro) {
    if (!skipIntro) {
        std::cout <<
R"(   __  ___                 ______      _                       
  /  |/  /___  ____  ____ / ____/___  (_)___  ____  ___  _____
 / /|_/ / __ \/ __ \/ __ `/ /   / __ \/ / __ \/ __ \/ _ \/ ___/
/ /  / / /_/ / / / / /_/ / /___/ /_/ / / / / / / / /  __/ /    
/_/  /_/\____/_/ /_/\__,_/\____/\____/_/_/ /_/_/ /_/\___/_/     

             Mars Colony Simulation — Launcher
)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    } else {
        std::cout << "Mars Colony Simulation — Launcher (intro skipped)\n";
    }
}

static bool validate_installation(Logger& log) {
    bool ok = true;
    auto cwd = fs::current_path();
    fs::path assetsLocal = cwd / "assets";
    if (!fs::exists(assetsLocal)) {
        log.warn("Assets folder not found at: " + assetsLocal.string());
        ok = false;
    } else {
        // Basic expected subfolders (customize as you build out your project)
        std::vector<std::string> expected = {"core", "locale"};
        for (const auto& sub : expected) {
            if (!fs::exists(assetsLocal / sub)) {
                log.warn("Expected assets subfolder missing: " + (assetsLocal / sub).string());
            }
        }
        log.info("Assets found: " + assetsLocal.string());
    }
    return ok;
}

// ------------------------------- Engine State --------------------------------
struct EngineContext {
    Config config;
    AppPaths paths;
    uint64_t seed = 0;
};

// Global SDL handles for this simple launcher
static SDL_Window*   g_window   = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static int           g_win_w    = 0;
static int           g_win_h    = 0;
static bool          g_fullscreen = false;

// Build meta (shown in logs and title)
static constexpr const char* kAppName = "MarsColonySim";
static constexpr const char* kVersion = "0.2.0-SDL";

// Forward decls
static bool InitializeEngine(const EngineContext& ctx);
static bool PreloadAssets(const EngineContext& ctx);
static int  RunGameLoop(const EngineContext& ctx);
static void ShutdownEngine();

// --------------------------------- Engine ------------------------------------
static bool InitializeEngine(const EngineContext& ctx) {
    g_log.info("InitializeEngine(SDL2): begin");

    if (SDL_WasInit(0) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            g_log.error(std::string("SDL_Init failed: ") + SDL_GetError());
            return false;
        }
    }

    // Nice-to-have hints
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, ctx.config.safeMode ? "0" : "1"); // 0=nearest, 1=linear
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"); // keep compositing stable on Linux
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"); // don't force foreground switch on launch
#endif

    g_win_w = static_cast<int>(ctx.config.width);
    g_win_h = static_cast<int>(ctx.config.height);

    Uint32 wflags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    g_window = SDL_CreateWindow(
        "Mars Colony Simulation",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        g_win_w, g_win_h, wflags
    );
    if (!g_window) {
        g_log.error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return false;
    }

    g_fullscreen = ctx.config.fullscreen;
    if (g_fullscreen) {
        if (SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            g_log.warn(std::string("Fullscreen request failed (continuing windowed): ") + SDL_GetError());
            g_fullscreen = false;
        }
    }

    Uint32 rflags = (ctx.config.safeMode ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED);
    if (ctx.config.vsync) rflags |= SDL_RENDERER_PRESENTVSYNC;

    g_renderer = SDL_CreateRenderer(g_window, -1, rflags);
    if (!g_renderer && !ctx.config.safeMode) {
        g_log.warn(std::string("Hardware renderer failed: ") + SDL_GetError() + " — retrying with software.");
        g_renderer = SDL_CreateRenderer(g_window, -1,
            SDL_RENDERER_SOFTWARE | (ctx.config.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    }
    if (!g_renderer) {
        g_log.error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        return false;
    }

    SDL_RendererInfo info{};
    SDL_GetRendererInfo(g_renderer, &info);

    std::ostringstream title;
    title << "Mars Colony Simulation — " << kVersion
          << "  [" << PlatformName() << "]"
          << "  (" << g_win_w << "x" << g_win_h
          << (g_fullscreen ? ", fullscreen" : ", windowed")
          << (ctx.config.vsync ? ", vsync on" : ", vsync off") << ")";
    SDL_SetWindowTitle(g_window, title.str().c_str());

    g_log.info(std::string("Renderer: ") + (info.name ? info.name : "(unknown)"));
    g_log.info(std::string("Flags: ") +
               ((info.flags & SDL_RENDERER_SOFTWARE) ? "software " : "") +
               ((info.flags & SDL_RENDERER_ACCELERATED) ? "accelerated " : "") +
               ((info.flags & SDL_RENDERER_PRESENTVSYNC) ? "vsync " : "") +
               ((info.flags & SDL_RENDERER_TARGETTEXTURE) ? "target-texture " : ""));
    g_log.info("InitializeEngine(SDL2): ok");
    return true;
}

static bool PreloadAssets(const EngineContext& ctx) {
    (void)ctx;
    // Stub: load textures/fonts/audio here. For now just log a step.
    g_log.info("PreloadAssets(): begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    g_log.info("PreloadAssets(): ok");
    return true;
}

// Utility: set the animated Mars-like background color
static void set_mars_clear_color(SDL_Renderer* r, double t, uint64_t seed) {
    // Mild animation based on time + seed; keep it Mars‑red and calm.
    double phase = t * 0.5 + (seed & 0xFFFF) * 0.0001;
    auto clampu8 = [](double v) -> Uint8 {
        if (v < 0.0) v = 0.0;
        if (v > 255.0) v = 255.0;
        return static_cast<Uint8>(v + 0.5);
    };
    Uint8 R = clampu8(170.0 + 80.0  * (0.5 + 0.5 * std::sin(phase)));
    Uint8 G = clampu8(30.0  + 25.0  * (0.5 + 0.5 * std::sin(phase * 0.8 + 1.234)));
    Uint8 B = clampu8(25.0  + 18.0  * (0.5 + 0.5 * std::sin(phase * 0.6 + 2.468)));
    SDL_SetRenderDrawColor(r, R, G, B, 255);
}

static int RunGameLoop(const EngineContext& ctx) {
    g_log.info("RunGameLoop(): start");

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    auto lastTitle = t0;

    int frames = 0;
    double fps = 0.0;

    // For windowed mode, remember a preferred size to restore when leaving fullscreen
    int windowed_w = g_win_w;
    int windowed_h = g_win_h;

    while (!g_should_quit) {
        // --- Handle events ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                g_should_quit = 1;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    g_should_quit = 1;
                } else if (e.key.keysym.sym == SDLK_F11) {
                    // Toggle fullscreen desktop
                    g_fullscreen = !g_fullscreen;
                    if (!g_fullscreen) {
                        SDL_SetWindowFullscreen(g_window, 0);
                        SDL_SetWindowSize(g_window, windowed_w, windowed_h);
                    } else {
                        // Remember windowed size before going fullscreen
                        SDL_GetWindowSize(g_window, &windowed_w, &windowed_h);
                        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    g_log.info(std::string("Fullscreen: ") + (g_fullscreen ? "on" : "off"));
                }
            } else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    g_win_w = e.window.data1;
                    g_win_h = e.window.data2;
                }
            }
        }

        // --- Update timing + title ---
        auto now = clock::now();
        std::chrono::duration<double> sinceStart = now - t0;
        std::chrono::duration<double> sinceTitle = now - lastTitle;

        // --- Render ---
        set_mars_clear_color(g_renderer, sinceStart.count(), ctx.seed);
        SDL_RenderClear(g_renderer);

        // Ground band (simple horizon)
        SDL_Rect ground{0, static_cast<int>(g_win_h * 0.62), g_win_w, g_win_h};
        // Slightly darker ground
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(g_renderer, &r, &g, &b, &a);
        Uint8 gr = static_cast<Uint8>(std::min(255, r / 2 + 30));
        Uint8 gg = static_cast<Uint8>(g / 2);
        Uint8 gb = static_cast<Uint8>(b / 2);
        SDL_SetRenderDrawColor(g_renderer, gr, gg, gb, 255);
        SDL_RenderFillRect(g_renderer, &ground);

        SDL_RenderPresent(g_renderer);
        frames++;

        // Update FPS in title roughly once per second
        if (sinceTitle.count() >= 1.0) {
            fps = frames / sinceTitle.count();
            frames = 0;
            lastTitle = now;

            std::ostringstream title;
            title << "Mars Colony Simulation — " << kVersion
                  << "  [" << PlatformName() << "]  "
                  << g_win_w << "x" << g_win_h
                  << (g_fullscreen ? "  Fullscreen" : "  Windowed")
                  << (ctx.config.vsync ? "  VSync" : "  No VSync")
                  << "  —  " << std::fixed << std::setprecision(0) << fps << " FPS";
            SDL_SetWindowTitle(g_window, title.str().c_str());
        }

        // If vsync is off, add a very small delay to avoid 100% CPU in tiny scenes
        if (!ctx.config.vsync) SDL_Delay(1);
    }

    g_log.info("RunGameLoop(): exiting");
    return 0;
}

static void ShutdownEngine() {
    g_log.info("ShutdownEngine(): begin");
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window = nullptr; }
    SDL_Quit();
    g_log.info("ShutdownEngine(): ok");
}

// ================================== main() ===================================
int main(int argc, char** argv) {
    // Basic meta
    const std::string     kBuildStamp = util::timestamp_compact();

    // Parse CLI early (so we can honor --config before reading config)
    LaunchOptions cli = parse_args(argc, argv);

    // Resolve paths and logging
    AppPaths paths = compute_paths(kAppName);
    try { ensure_directories(paths); }
    catch (const std::exception& e) {
        std::cerr << "Failed to create app directories: " << e.what() << "\n";
        return 2;
    }

    const fs::path logfile = paths.logsDir / (std::string(kAppName) + "-" + kBuildStamp + ".log");
    if (!g_log.open(logfile, /*mirror_to_console*/ true)) {
        std::cerr << "Failed to open log file at " << logfile << "\n";
        return 3;
    }

    // Install crash/termination hooks
    std::set_terminate(terminate_handler);
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    g_log.info(std::string("Launcher starting: ") + kAppName + " " + kVersion +
               " on " + PlatformName());
    g_log.info("Log file: " + logfile.string());

    print_splash(cli.skipIntro.value_or(false));

    // Load config (from CLI-specified path or default)
    fs::path cfgFile = cli.configFile.value_or(paths.defaultConfigFile());
    Config fileCfg = load_config(cfgFile, /*create_if_missing*/ true);
    Config cfg     = make_effective_config(fileCfg, cli);

    // Persist default config if we just created it
    if (!fs::exists(cfgFile)) write_default_config(cfgFile, fileCfg);

    // Early validate-only mode
    if (cli.validateOnly) {
        bool ok = validate_installation(g_log);
        std::cout << (ok ? "Validation OK\n" : "Validation FAILED\n");
        g_log.info(std::string("Validation result: ") + (ok ? "OK" : "FAILED"));
        return ok ? 0 : 4;
    }

    // Establish seed
    uint64_t seed = 0;
    if (cfg.seed.has_value()) {
        seed = *cfg.seed;
    } else {
        std::random_device rd;
        seed = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    }

    EngineContext ctx{cfg, paths, seed};

    // Basic install sanity
    if (!validate_installation(g_log)) {
        g_log.warn("Continuing despite validation warnings/errors.");
    }

    // Bootstrap sequence
    if (g_should_quit) { g_log.warn("Startup aborted by signal."); return 130; }
    if (!InitializeEngine(ctx)) { g_log.error("Engine initialization failed."); return 5; }

    if (g_should_quit) { g_log.warn("Startup aborted by signal."); ShutdownEngine(); return 130; }
    if (!PreloadAssets(ctx))   { g_log.error("Asset preload failed."); ShutdownEngine(); return 6; }

    if (g_should_quit) { g_log.warn("Startup aborted by signal."); ShutdownEngine(); return 130; }
    int rc = RunGameLoop(ctx);

    ShutdownEngine();
    g_log.info("Launcher exiting with code " + std::to_string(rc));
    return rc;
}
