// src/game/GamePublic.hpp
//
// Public game API for Colony-Game (Windows-only).
// ──────────────────────────────────────────────────────────────────────────────
// ⚠️ Compatibility contract (do not break):
//   1) The FIRST TEN FIELDS of `cg::GameOptions` (up to `assetsDir`) must keep
//      the SAME names, types, and ORDER as in the original project.
//   2) New fields MUST be appended at the end with sensible defaults.
//   3) The game entry point stays:  int RunColonyGame(const GameOptions&).
//
// This header is header-only, MSVC/C++17-friendly, and avoids <windows.h>.
// It adds richer enums, validation, defaults, path handling, hashing, simple
// (de)serialization helpers, and optional callback hooks used by Windows
// launchers—WITHOUT forcing engine-side changes.
//
// Build requirement: C++17 (MSVC 2019/2022). Target: Windows only.

#pragma once

// -------------------------------- Platform guard ------------------------------
#if !defined(_WIN32)
#  error "GamePublic.hpp is Windows-only. Build this target on Windows/MSVC."
#endif

// --------------------------------- Includes ----------------------------------
#include <cstdint>
#include <cstdlib>      // std::getenv
#include <cctype>       // std::isalpha
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <type_traits>
#include <filesystem>
#include <utility>
#include <tuple>
#include <cstring>      // std::memcpy
#include <sstream>

// ------------------------------- Namespace -----------------------------------
namespace cg {

// ============================================================================
// Versioning & feature toggles
// ============================================================================

/// Increment on public ABI (breaking) changes to this header.
/// Adding fields at the end of GameOptions is non-breaking.
inline constexpr std::uint32_t kGamePublicVersion = 3; // 2 (2025-09-14), 3 -> added HDR/Accessibility/Hash/Serialization

#ifndef CG_ENABLE_EXTRAS
#  define CG_ENABLE_EXTRAS 1
#endif

#ifndef CG_PUBLIC_STRICT_VALIDATE
#  define CG_PUBLIC_STRICT_VALIDATE 0  // set to 1 to make Validate() stricter
#endif

// ============================================================================
// Small enums (scoped, stable, string-serializable)
// ============================================================================

enum class WindowMode       : std::uint8_t { Windowed = 0, Borderless = 1, FullscreenExclusive = 2 };
enum class GraphicsBackend  : std::uint8_t { Auto = 0, D3D11 = 1, D3D12 = 2 };
enum class VsyncMode        : std::uint8_t { Off = 0, On = 1, Adaptive = 2 };
enum class AntiAliasing     : std::uint8_t { None = 0, MSAAx2, MSAAx4, MSAAx8, TAA };
enum class Anisotropy       : std::uint8_t { x1 = 1, x2 = 2, x4 = 4, x8 = 8, x16 = 16 };
enum class TextureQuality   : std::uint8_t { Low = 0, Medium, High, Ultra };
enum class ShadowQuality    : std::uint8_t { Off = 0, Low, Medium, High, Ultra };
enum class PostFXQuality    : std::uint8_t { Off = 0, Low, Medium, High };
enum class Upscaler         : std::uint8_t { None = 0, FSR2 = 1 }; // keep vendor-agnostic
enum class TelemetryMode    : std::uint8_t { Off = 0, Minimal = 1, Full = 2 };
enum class Difficulty       : std::uint8_t { Story = 0, Normal = 1, Hard = 2, Brutal = 3 };
enum class LogLevel         : std::uint8_t { Trace = 0, Debug, Info, Warn, Error, Fatal };
enum class RunResult        : std::int32_t { Ok = 0, UserQuit = 1, FailedToInit = 10, CrashRecovered = 20 };

enum class HdrMode          : std::uint8_t { Off = 0, scRGB = 1, HDR10 = 2 };
enum class ColorSpace       : std::uint8_t { sRGB = 0, DisplayP3 = 1, Rec2020 = 2 };
enum class ScreenshotFormat : std::uint8_t { PNG = 0, JPG = 1, BMP = 2, DDS = 3 };
enum class FramePacingMode  : std::uint8_t { None = 0, Sleep = 1, BusyWait = 2, Hybrid = 3 };
enum class ThreadPriority   : std::uint8_t { Low = 0, Normal = 1, High = 2 };

enum class ColorBlindMode   : std::uint8_t { None = 0, Protanopia = 1, Deuteranopia = 2, Tritanopia = 3 };

// Bitflag helpers
template <typename E>
struct is_flag_enum : std::false_type {};
#define CG_ENABLE_FLAGS(EnumType) \
    template<> struct is_flag_enum<EnumType> : std::true_type {}

enum class SafeModeFlags : std::uint32_t {
    None               = 0,
    DisableMods        = 1u << 0,
    DisableShaders     = 1u << 1,
    ForceD3D11         = 1u << 2,
    NoPostFX           = 1u << 3,
    SoftwareCursor     = 1u << 4,
    SingleThreaded     = 1u << 5,
    DisableAudio       = 1u << 6
};
CG_ENABLE_FLAGS(SafeModeFlags);

enum class DebugFlags : std::uint32_t {
    None          = 0,
    Wireframe     = 1u << 0,
    NoCulling     = 1u << 1,
    NoShadows     = 1u << 2,
    ShowPhysics   = 1u << 3,
    ShowNavmesh   = 1u << 4,
    ShowAI        = 1u << 5,
    ShowPaths     = 1u << 6,
    ShowBounds    = 1u << 7
};
CG_ENABLE_FLAGS(DebugFlags);

// Flag operators
template <typename E, typename = std::enable_if_t<is_flag_enum<E>::value>>
constexpr E operator|(E a, E b) noexcept { return static_cast<E>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); }
template <typename E, typename = std::enable_if_t<is_flag_enum<E>::value>>
constexpr E operator&(E a, E b) noexcept { return static_cast<E>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)); }
template <typename E, typename = std::enable_if_t<is_flag_enum<E>::value>>
constexpr E& operator|=(E& a, E b) noexcept { a = a | b; return a; }
template <typename E, typename = std::enable_if_t<is_flag_enum<E>::value>>
constexpr bool any(E a) noexcept { return static_cast<std::uint32_t>(a) != 0; }

// ============================================================================
// Optional callback hooks (all optional; set to nullptr if unused)
// ============================================================================

struct GameCallbacks {
    void* user = nullptr;

    // Thread-safe if your implementation is.
    void (*log)(LogLevel level, const char* message, void* user) = nullptr;

    // Long-running stage notifications (e.g., "Loading shaders", 0..100%).
    void (*progress)(int percent, const char* stage, void* user) = nullptr;

    // Called when the game is about to exit. Return true to allow exit.
    bool (*confirm_exit)(void* user) = nullptr;

    // Telemetry event hook (name + JSON-ish payload).
    void (*telemetry_event)(const char* eventName, const char* payload, void* user) = nullptr;

    // Panic hook (fatal error); if set, the game may invoke before aborting.
    void (*panic)(const char* message, void* user) = nullptr;
};

namespace detail {
    inline GameCallbacks g_callbacks{};
}
inline void SetCallbacks(const GameCallbacks& cb) noexcept { detail::g_callbacks = cb; }
inline const GameCallbacks& GetCallbacks() noexcept { return detail::g_callbacks; }

// ============================================================================
// GameOptions (public, launcher-facing)
// *** The first 10 fields are legacy and MUST remain as-is (names/order). ***
// ============================================================================

struct GameOptions {
    // ---- Legacy contract (DO NOT REORDER/RENAME) ----------------------------
    int         width      = 1280;
    int         height     = 720;
    bool        fullscreen = false;          // superseded by windowMode if set by launcher
    bool        vsync      = true;           // maps to vsyncMode = On if true
    bool        safeMode   = false;          // legacy master switch (see safeModeFlags)
    std::uint64_t seed     = 0;              // 0 -> auto-seed by game

    std::string profile    = "default";
    std::string lang       = "en-US";

    // Absolute or env-expanded (launcher may resolve them).
    std::string saveDir;    // e.g. %LOCALAPPDATA%\\ColonyGame\\Saves
    std::string assetsDir;  // e.g. ".\\res"

    // ---- Extended fields (append-only from here) ----------------------------

    // Window / presentation
    WindowMode       windowMode         = WindowMode::Windowed;
    int              windowPosX         = -1;      // -1 => OS default
    int              windowPosY         = -1;
    float            renderScale        = 1.0f;    // internal scaling multiplier
    VsyncMode        vsyncMode          = VsyncMode::On;
    std::uint32_t    targetFrameRate    = 0;       // 0 => uncapped
    bool             dpiAware           = true;
    bool             highDpiMouse       = true;
    FramePacingMode  framePacing        = FramePacingMode::Hybrid;
    int              monitorIndex       = -1;      // -1 => primary

    // HDR / color
    HdrMode          hdrMode            = HdrMode::Off;
    ColorSpace       colorSpace         = ColorSpace::sRGB;
    int              hdrMaxNits         = 1000;    // UI tonemapping hint

    // Renderer
    GraphicsBackend  backend            = GraphicsBackend::Auto;
    AntiAliasing     aa                 = AntiAliasing::None;
    Anisotropy       aniso              = Anisotropy::x8;
    TextureQuality   textureQuality     = TextureQuality::High;
    ShadowQuality    shadowQuality      = ShadowQuality::Medium;
    PostFXQuality    postFx             = PostFXQuality::Medium;
    Upscaler         upscaler           = Upscaler::None;
    float            sharpness          = 0.3f;    // 0..1 post-upscale sharpening
    int              adapterOrdinal     = -1;      // -1 => auto
    bool             preferDiscreteGpu  = true;

    // Input
    bool             rawInput           = true;
    bool             captureCursor      = true;
    bool             invertY            = false;
    float            mouseSensitivity   = 1.0f;    // >0
    bool             gamepadEnabled     = true;
    bool             controllerRumble   = true;

    // Audio
    int              audioSampleRate    = 48000;
    int              audioBufferMs      = 48;
    int              audioChannels      = 2;       // 1 or 2
    std::string      audioDeviceId;                // "" => default
    float            masterVolume       = 1.0f;    // 0..1
    float            musicVolume        = 0.7f;    // 0..1
    float            sfxVolume          = 0.9f;    // 0..1
    bool             muteWhenUnfocused  = false;

    // Gameplay / UX
    Difficulty       difficulty         = Difficulty::Normal;
    bool             pauseOnFocusLoss   = true;
    bool             autosaveEnabled    = true;
    int              autosaveMinutes    = 10;      // 1..120
    bool             showFpsOverlay     = false;
    float            uiScale            = 1.0f;    // 0.5 .. 2.0
    bool             tutorialEnabled    = true;

    // Accessibility
    ColorBlindMode   colorBlindMode     = ColorBlindMode::None;
    bool             highContrastUI     = false;
    bool             subtitlesEnabled   = true;
    int              subtitleSizePt     = 18;      // UI points

    // Simulation / determinism
    bool             deterministicRNG   = false;   // use seed strictly
    int              fixedTimeStepHz    = 60;      // 0 => variable
    int              maxCatchUpFrames   = 5;

    // Telemetry & diagnostics
    TelemetryMode    telemetry          = TelemetryMode::Minimal;
    LogLevel         logLevel           = LogLevel::Info;
    bool             enableCrashDumps   = true;
    DebugFlags       debugFlags         = DebugFlags::None;
    SafeModeFlags    safeModeFlags      = SafeModeFlags::None;

    // Paths
    std::string      configDir;         // %LOCALAPPDATA%\\ColonyGame\\Config
    std::string      logsDir;           // %LOCALAPPDATA%\\ColonyGame\\Logs
    std::string      cacheDir;          // %LOCALAPPDATA%\\ColonyGame\\Cache
    std::string      screenshotsDir;    // %USERPROFILE%\\Pictures\\ColonyGame
    std::string      modsDir;           // %LOCALAPPDATA%\\ColonyGame\\Mods
    std::string      replayDir;         // %LOCALAPPDATA%\\ColonyGame\\Replays
    std::string      crashDumpDir;      // %LOCALAPPDATA%\\ColonyGame\\CrashDumps
    std::string      tempDir;           // %LOCALAPPDATA%\\ColonyGame\\Temp

    // Saves
    bool             saveAutoBackup     = true;
    int              saveCompression    = 3;       // 0..9 (policy hint)
    std::string      saveSlotName;                 // optional slot alias

    // Screenshots
    ScreenshotFormat screenshotFormat   = ScreenshotFormat::PNG;
    int              screenshotJpegQ    = 92;      // 1..100

    // Threading
    int              workerThreads      = -1;      // -1 => auto (hardware_concurrency-1)
    ThreadPriority   threadPriority     = ThreadPriority::Normal;

    // Feature toggles
    bool             enableMods         = true;
    bool             enableHotReload    = true;
    bool             enableCheats       = false;   // dev builds

    // Networking / telemetry endpoints (optional)
    std::string      telemetryEndpoint;            // e.g. "https://telemetry.example/api"
    std::string      httpProxy;                    // e.g. "http://127.0.0.1:8888"

    // Free-form passthrough flags (CLI, etc.)
    std::vector<std::string> extraArgs;
};

static_assert(std::is_standard_layout<GameOptions>::value,
              "GameOptions must remain standard-layout to keep ABI stable");

// ============================================================================
// Minimal helpers (no Windows headers needed)
// ============================================================================

namespace detail {

// Replace all occurrences of 'from' with 'to' (in place).
inline void replace_all(std::string& s, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// Expand a single %VAR% token.
inline std::string expand_one_env(std::string_view token) {
    if (token.size() < 3 || token.front() != '%' || token.back() != '%') return std::string(token);
    auto name = std::string(token.substr(1, token.size() - 2));
    if (const char* v = std::getenv(name.c_str())) return std::string(v);
    return std::string(token);
}

// Expand all %VAR% occurrences in 'path'. Windows-style only.
inline std::string expand_env_vars(std::string path) {
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size();) {
        if (path[i] == '%') {
            auto j = path.find('%', i + 1);
            if (j != std::string::npos) {
                out += expand_one_env(path.substr(i, j - i + 1));
                i = j + 1;
                continue;
            }
        }
        out.push_back(path[i++]);
    }
    return out;
}

// Join (A, B) with backslash if B is relative and A non-empty.
inline std::string join_path(const std::string& base, const std::string& more) {
    namespace fs = std::filesystem;
    if (base.empty()) return more;
    fs::path a = fs::u8path(base);
    fs::path b = fs::u8path(more);
    return fs::u8path((b.is_absolute() ? b : (a / b))).u8string();
}

// Normalize slashes to backslashes and trim trailing slash (unless root).
inline std::string normalize_backslashes(std::string p) {
    replace_all(p, "/", "\\");
    if (p.size() > 3 && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    return p;
}

inline bool looks_like_lang_tag(std::string_view lang) {
    auto A = [](char c){ return !!std::isalpha(static_cast<unsigned char>(c)); };
    if (lang.size() < 2) return false;
    if (!(A(lang[0]) && A(lang[1]))) return false;
    if (lang.size() == 2) return true;
    if (lang.size() == 5 && lang[2] == '-') return A(lang[3]) && A(lang[4]);
    return false;
}

// FNV-1a 64-bit hashing helpers (stable across builds)
inline std::uint64_t fnv1a_init() noexcept { return 14695981039346656037ull; }
inline std::uint64_t fnv1a_update(std::uint64_t h, const void* data, std::size_t len) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
template <typename T>
inline void hash_append(std::uint64_t& h, const T& v) noexcept {
    static_assert(std::is_trivially_copyable<T>::value, "hash_append requires trivially copyable type");
    h = fnv1a_update(h, &v, sizeof(T));
}
inline void hash_append_str(std::uint64_t& h, std::string_view s) noexcept {
    h = fnv1a_update(h, s.data(), s.size());
    char zero = 0; // separator to avoid collisions across boundaries
    h = fnv1a_update(h, &zero, 1);
}
template <typename Enum>
inline void hash_append_enum(std::uint64_t& h, Enum e) noexcept {
    using U = std::underlying_type_t<Enum>;
    U u = static_cast<U>(e);
    hash_append(h, u);
}
inline void hash_append_float(std::uint64_t& h, float f) noexcept {
    static_assert(sizeof(float) == 4, "unexpected float size");
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(f));
    hash_append(h, u);
}

} // namespace detail

// ============================================================================
// Defaults & path resolution (Windows conventions)
// ============================================================================

inline void ApplyDefaultPaths(GameOptions& o) {
    auto localAppData = detail::expand_env_vars("%LOCALAPPDATA%");
    auto userProfile  = detail::expand_env_vars("%USERPROFILE%");

    auto norm = [](std::string s){ return detail::normalize_backslashes(detail::expand_env_vars(std::move(s))); };

    if (o.saveDir.empty())        o.saveDir        = detail::join_path(localAppData, "ColonyGame\\Saves");
    if (o.configDir.empty())      o.configDir      = detail::join_path(localAppData, "ColonyGame\\Config");
    if (o.logsDir.empty())        o.logsDir        = detail::join_path(localAppData, "ColonyGame\\Logs");
    if (o.cacheDir.empty())       o.cacheDir       = detail::join_path(localAppData, "ColonyGame\\Cache");
    if (o.screenshotsDir.empty()) o.screenshotsDir = detail::join_path(userProfile,  "Pictures\\ColonyGame");
    if (o.modsDir.empty())        o.modsDir        = detail::join_path(localAppData, "ColonyGame\\Mods");
    if (o.replayDir.empty())      o.replayDir      = detail::join_path(localAppData, "ColonyGame\\Replays");
    if (o.crashDumpDir.empty())   o.crashDumpDir   = detail::join_path(localAppData, "ColonyGame\\CrashDumps");
    if (o.tempDir.empty())        o.tempDir        = detail::join_path(localAppData, "ColonyGame\\Temp");
    if (o.assetsDir.empty())      o.assetsDir      = ".\\res";

    o.saveDir        = norm(o.saveDir);
    o.configDir      = norm(o.configDir);
    o.logsDir        = norm(o.logsDir);
    o.cacheDir       = norm(o.cacheDir);
    o.screenshotsDir = norm(o.screenshotsDir);
    o.modsDir        = norm(o.modsDir);
    o.replayDir      = norm(o.replayDir);
    o.crashDumpDir   = norm(o.crashDumpDir);
    o.tempDir        = norm(o.tempDir);
    o.assetsDir      = norm(o.assetsDir);
}

/// Create directories if missing (no error on failure; returns list of created dirs).
inline std::vector<std::string> EnsureDirectories(const GameOptions& o) {
    namespace fs = std::filesystem;
    std::vector<std::string> made;
    auto mk = [&](const std::string& p){
        if (p.empty()) return;
        std::error_code ec{};
        if (fs::create_directories(fs::u8path(p), ec)) made.push_back(p);
    };
    mk(o.saveDir); mk(o.configDir); mk(o.logsDir); mk(o.cacheDir);
    mk(o.screenshotsDir); mk(o.modsDir); mk(o.replayDir); mk(o.crashDumpDir); mk(o.tempDir);
    return made;
}

// ============================================================================
// Back-compat and sanitization
// ============================================================================

/// Map legacy booleans to new enums if launcher didn't set explicit values.
inline void ApplyBackCompat(GameOptions& o) {
    // fullscreen => windowMode unless launcher already overrode
    if (o.fullscreen && o.windowMode == WindowMode::Windowed)
        o.windowMode = WindowMode::FullscreenExclusive;

    // vsync => vsyncMode
    if (o.vsync)  o.vsyncMode = VsyncMode::On;
    else          o.vsyncMode = VsyncMode::Off;

    // safeMode => safeModeFlags (non-destructive)
    if (o.safeMode) o.safeModeFlags |= SafeModeFlags::DisableMods | SafeModeFlags::NoPostFX;
}

/// Clamp values into safe ranges, fix obviously broken combos.
inline void Sanitize(GameOptions& o) {
    if (o.width < 320)  o.width = 320;
    if (o.height < 200) o.height = 200;

    if (o.width > 7680)  o.width = 7680;
    if (o.height > 4320) o.height = 4320;

    if (o.renderScale < 0.25f) o.renderScale = 0.25f;
    if (o.renderScale > 2.50f) o.renderScale = 2.50f;

    if (o.targetFrameRate > 1000) o.targetFrameRate = 1000;

    if (o.mouseSensitivity <= 0.0f) o.mouseSensitivity = 1.0f;

    if (o.audioSampleRate < 22050)  o.audioSampleRate = 22050;
    if (o.audioSampleRate > 192000) o.audioSampleRate = 192000;

    if (o.audioBufferMs < 16) o.audioBufferMs = 16;
    if (o.audioBufferMs > 200) o.audioBufferMs = 200;

    if (o.audioChannels != 1 && o.audioChannels != 2) o.audioChannels = 2;

    auto clamp01 = [](float& v){ if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f; };
    clamp01(o.masterVolume); clamp01(o.musicVolume); clamp01(o.sfxVolume); clamp01(o.sharpness);

    if (o.autosaveMinutes < 1) o.autosaveMinutes = 1;
    if (o.autosaveMinutes > 120) o.autosaveMinutes = 120;

    if (o.uiScale < 0.5f) o.uiScale = 0.5f;
    if (o.uiScale > 2.0f) o.uiScale = 2.0f;

    if (o.subtitleSizePt < 10) o.subtitleSizePt = 10;
    if (o.subtitleSizePt > 48) o.subtitleSizePt = 48;

    if (o.fixedTimeStepHz < 0) o.fixedTimeStepHz = 0;
    if (o.fixedTimeStepHz > 480) o.fixedTimeStepHz = 480;

    if (o.maxCatchUpFrames < 0) o.maxCatchUpFrames = 0;
    if (o.maxCatchUpFrames > 30) o.maxCatchUpFrames = 30;

    if (o.saveCompression < 0) o.saveCompression = 0;
    if (o.saveCompression > 9) o.saveCompression = 9;

    if (o.screenshotJpegQ < 1) o.screenshotJpegQ = 1;
    if (o.screenshotJpegQ > 100) o.screenshotJpegQ = 100;

    if (o.workerThreads < -1) o.workerThreads = -1;
    if (o.workerThreads > 64) o.workerThreads = 64;
}

/// Validate; returns human-readable problems (empty => OK).
inline std::vector<std::string> Validate(const GameOptions& o) {
    std::vector<std::string> errs;

    if (o.width < 320 || o.height < 200)
        errs.emplace_back("Resolution is too small; minimum is 320x200.");
    if (o.width > 7680 || o.height > 4320)
        errs.emplace_back("Resolution exceeds 8K (7680x4320).");

    if (!detail::looks_like_lang_tag(o.lang))
        errs.emplace_back("Language tag should look like \"en\" or \"en-US\".");

    if (o.mouseSensitivity <= 0.0f)
        errs.emplace_back("Mouse sensitivity must be > 0.");

    if (o.audioSampleRate < 22050 || o.audioSampleRate > 192000)
        errs.emplace_back("Audio sample rate must be in [22050, 192000].");

    if (o.audioChannels != 1 && o.audioChannels != 2)
        errs.emplace_back("Audio channels must be 1 or 2.");

    if (o.autosaveEnabled && (o.autosaveMinutes < 1 || o.autosaveMinutes > 120))
        errs.emplace_back("Autosave interval must be between 1 and 120 minutes.");

#if CG_PUBLIC_STRICT_VALIDATE
    if (o.renderScale < 0.5f || o.renderScale > 2.0f)
        errs.emplace_back("Render scale out of recommended range [0.5, 2.0].");
#endif

    return errs;
}

// ============================================================================
// String conversions (ToString / TryParse) for core enums
// ============================================================================

inline const char* ToString(LogLevel v) {
    switch (v) { case LogLevel::Trace: return "Trace"; case LogLevel::Debug: return "Debug";
        case LogLevel::Info: return "Info"; case LogLevel::Warn: return "Warn";
        case LogLevel::Error: return "Error"; case LogLevel::Fatal: return "Fatal"; default: return "?"; }
}
inline const char* ToString(WindowMode v) {
    switch (v) { case WindowMode::Windowed: return "Windowed"; case WindowMode::Borderless: return "Borderless";
        case WindowMode::FullscreenExclusive: return "Fullscreen"; default: return "?"; }
}
inline const char* ToString(GraphicsBackend v) {
    switch (v) { case GraphicsBackend::Auto: return "Auto"; case GraphicsBackend::D3D11: return "D3D11";
        case GraphicsBackend::D3D12: return "D3D12"; default: return "?"; }
}
inline const char* ToString(VsyncMode v) {
    switch (v) { case VsyncMode::Off: return "Off"; case VsyncMode::On: return "On";
        case VsyncMode::Adaptive: return "Adaptive"; default: return "?"; }
}
inline const char* ToString(RunResult v) {
    switch (v) { case RunResult::Ok: return "Ok"; case RunResult::UserQuit: return "UserQuit";
        case RunResult::FailedToInit: return "FailedToInit"; case RunResult::CrashRecovered: return "CrashRecovered"; default: return "?"; }
}
inline const char* ToString(HdrMode v) {
    switch (v) { case HdrMode::Off: return "Off"; case HdrMode::scRGB: return "scRGB";
        case HdrMode::HDR10: return "HDR10"; default: return "?"; }
}
inline const char* ToString(ColorSpace v) {
    switch (v) { case ColorSpace::sRGB: return "sRGB"; case ColorSpace::DisplayP3: return "DisplayP3";
        case ColorSpace::Rec2020: return "Rec2020"; default: return "?"; }
}
inline const char* ToString(ScreenshotFormat v) {
    switch (v) { case ScreenshotFormat::PNG: return "PNG"; case ScreenshotFormat::JPG: return "JPG";
        case ScreenshotFormat::BMP: return "BMP"; case ScreenshotFormat::DDS: return "DDS"; default: return "?"; }
}
inline const char* ToString(FramePacingMode v) {
    switch (v) { case FramePacingMode::None: return "None"; case FramePacingMode::Sleep: return "Sleep";
        case FramePacingMode::BusyWait: return "BusyWait"; case FramePacingMode::Hybrid: return "Hybrid"; default: return "?"; }
}
inline const char* ToString(ThreadPriority v) {
    switch (v) { case ThreadPriority::Low: return "Low"; case ThreadPriority::Normal: return "Normal";
        case ThreadPriority::High: return "High"; default: return "?"; }
}
inline const char* ToString(ColorBlindMode v) {
    switch (v) { case ColorBlindMode::None: return "None"; case ColorBlindMode::Protanopia: return "Protanopia";
        case ColorBlindMode::Deuteranopia: return "Deuteranopia"; case ColorBlindMode::Tritanopia: return "Tritanopia"; default: return "?"; }
}

// Simple parsing helpers (case-insensitive)
inline bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char ac = static_cast<unsigned char>(a[i]);
        unsigned char bc = static_cast<unsigned char>(b[i]);
        if (std::tolower(ac) != std::tolower(bc)) return false;
    }
    return true;
}
template <typename Enum>
inline bool TryParseEnum(std::string_view, Enum&) { return false; } // fallback

// Specializations
template<> inline bool TryParseEnum<WindowMode>(std::string_view s, WindowMode& out) {
    if (ieq(s,"windowed")) out=WindowMode::Windowed; else if (ieq(s,"borderless")) out=WindowMode::Borderless;
    else if (ieq(s,"fullscreen")||ieq(s,"fullscreenexclusive")) out=WindowMode::FullscreenExclusive; else return false; return true;
}
template<> inline bool TryParseEnum<GraphicsBackend>(std::string_view s, GraphicsBackend& out) {
    if (ieq(s,"auto")) out=GraphicsBackend::Auto; else if (ieq(s,"d3d11")) out=GraphicsBackend::D3D11;
    else if (ieq(s,"d3d12")) out=GraphicsBackend::D3D12; else return false; return true;
}
template<> inline bool TryParseEnum<VsyncMode>(std::string_view s, VsyncMode& out) {
    if (ieq(s,"off")) out=VsyncMode::Off; else if (ieq(s,"on")) out=VsyncMode::On;
    else if (ieq(s,"adaptive")) out=VsyncMode::Adaptive; else return false; return true;
}
template<> inline bool TryParseEnum<HdrMode>(std::string_view s, HdrMode& out) {
    if (ieq(s,"off")) out=HdrMode::Off; else if (ieq(s,"scrgb")) out=HdrMode::scRGB;
    else if (ieq(s,"hdr10")) out=HdrMode::HDR10; else return false; return true;
}
template<> inline bool TryParseEnum<ColorSpace>(std::string_view s, ColorSpace& out) {
    if (ieq(s,"srgb")) out=ColorSpace::sRGB; else if (ieq(s,"displayp3")||ieq(s,"p3")) out=ColorSpace::DisplayP3;
    else if (ieq(s,"rec2020")||ieq(s,"bt2020")) out=ColorSpace::Rec2020; else return false; return true;
}
template<> inline bool TryParseEnum<ScreenshotFormat>(std::string_view s, ScreenshotFormat& out) {
    if (ieq(s,"png")) out=ScreenshotFormat::PNG; else if (ieq(s,"jpg")||ieq(s,"jpeg")) out=ScreenshotFormat::JPG;
    else if (ieq(s,"bmp")) out=ScreenshotFormat::BMP; else if (ieq(s,"dds")) out=ScreenshotFormat::DDS; else return false; return true;
}
template<> inline bool TryParseEnum<ThreadPriority>(std::string_view s, ThreadPriority& out) {
    if (ieq(s,"low")) out=ThreadPriority::Low; else if (ieq(s,"normal")) out=ThreadPriority::Normal;
    else if (ieq(s,"high")) out=ThreadPriority::High; else return false; return true;
}

// ============================================================================
// Utility calculations
// ============================================================================

/// Compute the internal render resolution after applying renderScale.
/// Returns (internalWidth, internalHeight).
inline std::pair<int,int> ComputeInternalResolution(const GameOptions& o) {
    const float s = (o.renderScale <= 0.f ? 1.0f : o.renderScale);
    int iw = static_cast<int>(o.width  * s + 0.5f);
    int ih = static_cast<int>(o.height * s + 0.5f);
    // clamp to at least 1x1
    if (iw < 1) iw = 1; if (ih < 1) ih = 1;
    return { iw, ih };
}

/// Suggest a worker thread count based on hints.
inline int SuggestedWorkerThreads(const GameOptions& o, int hwConcurrency) {
    if (o.workerThreads > 0) return o.workerThreads;
    if (hwConcurrency <= 1)  return 1;
    int base = hwConcurrency - 1;
    if (any(o.safeModeFlags & SafeModeFlags::SingleThreaded)) return 1;
    if (base > 16) base = 16; // practical cap
    return base;
}

// ============================================================================
// Stable hashing of options (useful for cache keys / repro bugs)
// ============================================================================

inline std::uint64_t HashOptions(const GameOptions& o) {
    using namespace detail;
    std::uint64_t h = fnv1a_init();

    // Legacy first 10 fields (keep order!)
    hash_append(h, o.width); hash_append(h, o.height);
    hash_append(h, o.fullscreen); hash_append(h, o.vsync); hash_append(h, o.safeMode);
    hash_append(h, o.seed);
    hash_append_str(h, o.profile); hash_append_str(h, o.lang);
    hash_append_str(h, o.saveDir); hash_append_str(h, o.assetsDir);

    // New fields
    hash_append_enum(h, o.windowMode);
    hash_append(h, o.windowPosX); hash_append(h, o.windowPosY);
    hash_append_float(h, o.renderScale);
    hash_append_enum(h, o.vsyncMode);
    hash_append(h, o.targetFrameRate);
    hash_append(h, o.dpiAware); hash_append(h, o.highDpiMouse);
    hash_append_enum(h, o.framePacing);
    hash_append(h, o.monitorIndex);

    hash_append_enum(h, o.hdrMode);
    hash_append_enum(h, o.colorSpace);
    hash_append(h, o.hdrMaxNits);

    hash_append_enum(h, o.backend);
    hash_append_enum(h, o.aa);
    hash_append_enum(h, o.aniso);
    hash_append_enum(h, o.textureQuality);
    hash_append_enum(h, o.shadowQuality);
    hash_append_enum(h, o.postFx);
    hash_append_enum(h, o.upscaler);
    hash_append_float(h, o.sharpness);
    hash_append(h, o.adapterOrdinal);
    hash_append(h, o.preferDiscreteGpu);

    hash_append(h, o.rawInput); hash_append(h, o.captureCursor); hash_append(h, o.invertY);
    hash_append_float(h, o.mouseSensitivity);
    hash_append(h, o.gamepadEnabled); hash_append(h, o.controllerRumble);

    hash_append(h, o.audioSampleRate); hash_append(h, o.audioBufferMs); hash_append(h, o.audioChannels);
    hash_append_str(h, o.audioDeviceId);
    hash_append_float(h, o.masterVolume); hash_append_float(h, o.musicVolume); hash_append_float(h, o.sfxVolume);
    hash_append(h, o.muteWhenUnfocused);

    hash_append_enum(h, o.difficulty);
    hash_append(h, o.pauseOnFocusLoss); hash_append(h, o.autosaveEnabled); hash_append(h, o.autosaveMinutes);
    hash_append(h, o.showFpsOverlay);
    hash_append_float(h, o.uiScale);
    hash_append(h, o.tutorialEnabled);

    hash_append_enum(h, o.colorBlindMode);
    hash_append(h, o.highContrastUI); hash_append(h, o.subtitlesEnabled);
    hash_append(h, o.subtitleSizePt);

    hash_append(h, o.deterministicRNG);
    hash_append(h, o.fixedTimeStepHz);
    hash_append(h, o.maxCatchUpFrames);

    hash_append_enum(h, o.telemetry);
    hash_append_enum(h, o.logLevel);
    hash_append(h, o.enableCrashDumps);
    hash_append_enum(h, o.debugFlags);
    hash_append_enum(h, o.safeModeFlags);

    hash_append_str(h, o.configDir); hash_append_str(h, o.logsDir); hash_append_str(h, o.cacheDir);
    hash_append_str(h, o.screenshotsDir); hash_append_str(h, o.modsDir); hash_append_str(h, o.replayDir);
    hash_append_str(h, o.crashDumpDir); hash_append_str(h, o.tempDir);

    hash_append(h, o.saveAutoBackup);
    hash_append(h, o.saveCompression);
    hash_append_str(h, o.saveSlotName);

    hash_append_enum(h, o.screenshotFormat);
    hash_append(h, o.screenshotJpegQ);

    hash_append(h, o.workerThreads);
    hash_append_enum(h, o.threadPriority);

    hash_append(h, o.enableMods); hash_append(h, o.enableHotReload); hash_append(h, o.enableCheats);

    hash_append_str(h, o.telemetryEndpoint);
    hash_append_str(h, o.httpProxy);

    for (const auto& s : o.extraArgs) hash_append_str(h, s);

    return h;
}

// ============================================================================
// Mini serialization helpers (string → JSON-ish and INI-ish)
// NOTE: Kept intentionally simple; fields added gradually as needed.
// ============================================================================

inline std::string EscapeJson(std::string_view s) {
    std::string out; out.reserve(s.size()+8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

/// Serialize a subset of options to a compact JSON string (for logging/telemetry).
inline std::string ToJson(const GameOptions& o) {
    std::ostringstream ss;
    ss << "{";
    ss << "\"width\":" << o.width << ",\"height\":" << o.height;
    ss << ",\"windowMode\":\"" << ToString(o.windowMode) << "\"";
    ss << ",\"vsync\":\"" << ToString(o.vsyncMode) << "\"";
    ss << ",\"renderScale\":" << o.renderScale;
    ss << ",\"backend\":\"" << ToString(o.backend) << "\"";
    ss << ",\"aa\":" << static_cast<int>(o.aa);
    ss << ",\"aniso\":" << static_cast<int>(o.aniso);
    ss << ",\"postFx\":" << static_cast<int>(o.postFx);
    ss << ",\"upscaler\":" << static_cast<int>(o.upscaler);
    ss << ",\"hdr\":\"" << ToString(o.hdrMode) << "\"";
    ss << ",\"colorSpace\":\"" << ToString(o.colorSpace) << "\"";
    ss << ",\"profile\":\"" << EscapeJson(o.profile) << "\"";
    ss << ",\"lang\":\"" << EscapeJson(o.lang) << "\"";
    ss << ",\"saveDir\":\"" << EscapeJson(o.saveDir) << "\"";
    ss << ",\"assetsDir\":\"" << EscapeJson(o.assetsDir) << "\"";
    ss << ",\"telemetry\":\"" << (o.telemetry==TelemetryMode::Off?"Off":(o.telemetry==TelemetryMode::Minimal?"Minimal":"Full")) << "\"";
    ss << ",\"hash\":\"" << HashOptions(o) << "\"";
    ss << "}";
    return ss.str();
}

/// Very small INI-like dump (readable diagnostics).
inline std::string ToIni(const GameOptions& o) {
    std::ostringstream ss;
    ss << "[Video]\n";
    ss << "Width=" << o.width << "\n";
    ss << "Height=" << o.height << "\n";
    ss << "WindowMode=" << ToString(o.windowMode) << "\n";
    ss << "Vsync=" << ToString(o.vsyncMode) << "\n";
    ss << "RenderScale=" << o.renderScale << "\n";
    ss << "Backend=" << ToString(o.backend) << "\n";
    ss << "HDR=" << ToString(o.hdrMode) << "\n";
    ss << "ColorSpace=" << ToString(o.colorSpace) << "\n\n";

    ss << "[Audio]\n";
    ss << "SampleRate=" << o.audioSampleRate << "\n";
    ss << "BufferMs=" << o.audioBufferMs << "\n";
    ss << "Channels=" << o.audioChannels << "\n";
    ss << "Master=" << o.masterVolume << "\n\n";

    ss << "[Gameplay]\n";
    ss << "Difficulty=" << static_cast<int>(o.difficulty) << "\n";
    ss << "Autosave=" << (o.autosaveEnabled ? 1 : 0) << "\n";
    ss << "AutosaveMinutes=" << o.autosaveMinutes << "\n\n";

    ss << "[Paths]\n";
    ss << "AssetsDir=" << o.assetsDir << "\n";
    ss << "SaveDir=" << o.saveDir << "\n";
    ss << "ConfigDir=" << o.configDir << "\n";
    ss << "LogsDir=" << o.logsDir << "\n";
    return ss.str();
}

// ============================================================================
// Entry point (implemented by the game)
// ============================================================================

/// Main entry point implemented by the game.
/// Return 0 (or RunResult::Ok) on success; non-zero otherwise.
int RunColonyGame(const GameOptions& opts);

// ============================================================================
// Convenience helpers
// ============================================================================

inline int ToExitCode(RunResult r) { return static_cast<int>(r); }

/// Quick "prepare" pipeline for launchers:
/// 1) Map legacy flags to new fields, 2) Clamp values, 3) Fill default paths.
inline void PrepareForLaunch(GameOptions& o) {
    ApplyBackCompat(o);
    Sanitize(o);
    ApplyDefaultPaths(o);
}

} // namespace cg
