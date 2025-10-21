#include "StagesConfig.hpp"

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <Windows.h> // GetPrivateProfileStringW, GetPrivateProfileIntW, GetModuleFileNameW, WideCharToMultiByte
#include <stdexcept>
#include <filesystem>
#include <string>
#include <cwchar>   // wcstof, _snwprintf_s
#include <cwctype>  // towlower
#include <iterator> // std::size

using namespace std::string_literals;

namespace colony::worldgen {

// ---------- UTF-8 helper (Windows-only, no data loss) ----------

static inline std::string utf8_from_wstring(const std::wstring& w)
{
    if (w.empty()) return {};
    const int in_len = static_cast<int>(w.size());

    // Query required size (bytes, no terminator)
    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0, w.c_str(), in_len, nullptr, 0, nullptr, nullptr);

    if (needed <= 0) return {};

    std::string out(static_cast<size_t>(needed), '\0');

    // Perform the conversion (no default char fallback -> fail-safe for debugging)
    ::WideCharToMultiByte(
        CP_UTF8, 0, w.c_str(), in_len, out.data(), needed, nullptr, nullptr);

    return out;
}

// ---------- small parsing helpers ----------

static inline std::wstring read_wstr(const wchar_t* section,
                                     const wchar_t* key,
                                     const std::wstring& def,
                                     const std::wstring& path)
{
    // Buffer large enough for numeric/text keys; Profile API limits are large,
    // but we only expect small values here.
    wchar_t buf[512]{};
    const DWORD n = ::GetPrivateProfileStringW(
        section, key, def.c_str(), buf, static_cast<DWORD>(std::size(buf)), path.c_str());
    return std::wstring(buf, buf + n);
}

static inline int read_int(const wchar_t* section,
                           const wchar_t* key,
                           int def,
                           const std::wstring& path)
{
    return static_cast<int>(::GetPrivateProfileIntW(section, key, def, path.c_str()));
}

static inline float read_float(const wchar_t* section,
                               const wchar_t* key,
                               float def,
                               const std::wstring& path)
{
    wchar_t defbuf[64];
    _snwprintf_s(defbuf, _TRUNCATE, L"%g", def);
    const std::wstring s = read_wstr(section, key, defbuf, path);
    wchar_t* end = nullptr;
    const float v = std::wcstof(s.c_str(), &end);
    return (end && end != s.c_str()) ? v : def;
}

static inline bool parse_bool_w(const std::wstring& s, bool def)
{
    if (s.empty()) return def;
    std::wstring t = s;
    // canonicalize
    for (auto& ch : t) ch = static_cast<wchar_t>(::towlower(ch));

    if (t == L"1" || t == L"true" || t == L"yes" || t == L"on")  return true;
    if (t == L"0" || t == L"false"|| t == L"no"  || t == L"off") return false;
    return def;
}

static inline bool read_bool(const wchar_t* section,
                             const wchar_t* key,
                             bool def,
                             const std::wstring& path)
{
    return parse_bool_w(read_wstr(section, key, def ? L"true" : L"false", path), def);
}

// ---------- default path helpers ----------

std::wstring StagesConfig::exe_dir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path p(std::wstring(buf, n));
    p = p.parent_path();
    return p.wstring();
}

std::wstring StagesConfig::default_path() {
    std::filesystem::path p(exe_dir());
    p /= L"assets";
    p /= L"config";
    p /= L"stages.ini";
    return p.wstring();
}

// ---------- loader ----------

bool StagesConfig::try_load(const std::wstring& iniPath, StagesRuntimeConfig& out)
{
    // It's okay if the file is missing; we will use defaults embedded in structs.
    // If you want to hard-fail when missing, check file existence here.
    // Note: Profile API may map to the Registry if IniFileMapping is configured.

    // [stage]
    out.stage.params.tileSizeMeters   = read_float(L"stage", L"tile_size_meters",      2.0f, iniPath);
    out.stage.params.mapUnitsPerMeter = read_float(L"stage", L"map_units_per_meter",   1.0f, iniPath);
    out.stage.params.grid.width       = read_int  (L"stage", L"grid_width",             512, iniPath);
    out.stage.params.grid.height      = read_int  (L"stage", L"grid_height",            512, iniPath);

#if CG_HAS_HYDROLOGY
    // [climate]
    out.climate.lapseRateKPerKm    = read_float(L"climate", L"lapse_rate_k_per_km",     6.5f,    iniPath);
    out.climate.latGradientKPerDeg = read_float(L"climate", L"lat_gradient_k_per_deg",  0.5f,    iniPath);
    out.climate.seaLevelTempK      = read_float(L"climate", L"sea_level_temp_k",     288.15f,    iniPath);

    // [hydrology]
    out.hydrology.incisionExpM       = read_float(L"hydrology", L"incision_exp_m",        0.5f, iniPath);
    out.hydrology.incisionExpN       = read_float(L"hydrology", L"incision_exp_n",        1.0f, iniPath);
    out.hydrology.smoothIterations   = read_int  (L"hydrology", L"smooth_iters",             3, iniPath);
#endif

    // [noise]
    out.noise.fbmOctaves           = read_int  (L"noise", L"fbm_octaves",                 5,    iniPath);
    out.noise.fbmGain              = read_float(L"noise", L"fbm_gain",                  0.5f,   iniPath);
    out.noise.fbmLacunarity        = read_float(L"noise", L"fbm_lacunarity",            2.0f,   iniPath);
    out.noise.domainWarpStrength   = read_float(L"noise", L"domain_warp_strength",      0.75f,  iniPath);

    // [debug]
    out.debug.drawTileGrid         = read_bool (L"debug", L"draw_tile_grid",          false,    iniPath);
    out.debug.exportDebugMaps      = read_bool (L"debug", L"export_debug_maps",       false,    iniPath);
    out.debug.seed                 = read_int  (L"debug", L"seed",                        42,    iniPath);

    return true;
}

StagesRuntimeConfig StagesConfig::load(const std::wstring& iniPath)
{
    StagesRuntimeConfig cfg{};
    if (!try_load(iniPath, cfg)) {
        // Avoid lossy wchar_t -> char narrowing by converting to UTF-8 explicitly.
        throw std::runtime_error(std::string("Failed to load INI: ") + utf8_from_wstring(iniPath));
    }
    return cfg;
}

} // namespace colony::worldgen
