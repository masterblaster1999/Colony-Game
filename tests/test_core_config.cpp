// tests/test_core_config.cpp
//
// Regression/robustness tests for src/core/Config.{h,cpp}.
//
// Goals:
//   - Saving creates the directory + writes config.ini
//   - Loading round-trips values
//   - Corrupt values do not throw / crash (no std::stoi exceptions)

#include <doctest/doctest.h>

#include "core/Config.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

fs::path make_unique_temp_dir()
{
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec || base.empty())
        base = fs::path(".");

    const auto stamp = static_cast<long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    fs::path dir = base / ("colony_core_config_tests_" + std::to_string(stamp));
    fs::create_directories(dir, ec);
    if (ec)
        return base;

    return dir;
}

} // namespace

TEST_CASE("core::SaveConfig creates config.ini and core::LoadConfig round-trips values")
{
    const fs::path dir = make_unique_temp_dir() / "roundtrip";

    core::Config cfg;
    cfg.windowWidth = 800;
    cfg.windowHeight = 600;
    cfg.vsync = false;

    CHECK(core::SaveConfig(cfg, dir));

    core::Config loaded;
    loaded.windowWidth = 1;
    loaded.windowHeight = 2;
    loaded.vsync = true;

    CHECK(core::LoadConfig(loaded, dir));
    CHECK(loaded.windowWidth == 800);
    CHECK(loaded.windowHeight == 600);
    CHECK(loaded.vsync == false);

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("core::LoadConfig returns false for missing file (first run)")
{
    const fs::path dir = make_unique_temp_dir() / "missing";
    std::error_code ec;
    fs::create_directories(dir, ec);

    core::Config cfg; // defaults
    CHECK_FALSE(core::LoadConfig(cfg, dir));

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("core::LoadConfig tolerates corrupt values (does not throw)")
{
    const fs::path dir = make_unique_temp_dir() / "corrupt";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const fs::path p = dir / "config.ini";
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        REQUIRE(f.good());
        f << "windowWidth=not_an_int\n";
        f << "windowHeight=600\n";
        f << "vsync=true\n";
    }

    core::Config cfg;
    cfg.windowWidth = 111;  // should remain unchanged (invalid)
    cfg.windowHeight = 222; // should update
    cfg.vsync = false;      // should update

    CHECK(core::LoadConfig(cfg, dir));
    CHECK(cfg.windowWidth == 111);
    CHECK(cfg.windowHeight == 600);
    CHECK(cfg.vsync == true);

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("core::LoadConfig ignores invalid boolean values (does not clobber existing config)")
{
    const fs::path dir = make_unique_temp_dir() / "invalid_bool";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const fs::path p = dir / "config.ini";
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        REQUIRE(f.good());
        f << "windowWidth=800\n";
        f << "vsync=not_a_bool\n";
    }

    core::Config cfg;
    cfg.windowWidth = 1; // should update
    cfg.vsync = true;    // should remain unchanged (invalid bool)

    CHECK(core::LoadConfig(cfg, dir));
    CHECK(cfg.windowWidth == 800);
    CHECK(cfg.vsync == true);

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("core::LoadConfig supports UTF-16LE BOM (Windows-style) config.ini")
{
    const fs::path dir = make_unique_temp_dir() / "utf16le_bom";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const fs::path p = dir / "config.ini";

    // This is what a simple ASCII config looks like when saved as UTF-16LE with BOM:
    //   FF FE  'w' 00 'i' 00 ...
    const std::string ascii =
        "windowWidth=800\n"
        "windowHeight=600\n"
        "vsync=0\n";

    std::string bytes;
    bytes.reserve(2 + ascii.size() * 2);
    bytes.push_back(static_cast<char>(0xFF));
    bytes.push_back(static_cast<char>(0xFE));

    for (unsigned char c : ascii)
    {
        bytes.push_back(static_cast<char>(c));
        bytes.push_back('\0'); // UTF-16LE: low byte then 0x00 for ASCII
    }

    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        REQUIRE(f.good());
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    core::Config cfg;
    cfg.windowWidth = 1;
    cfg.windowHeight = 2;
    cfg.vsync = true;

    CHECK(core::LoadConfig(cfg, dir));
    CHECK(cfg.windowWidth == 800);
    CHECK(cfg.windowHeight == 600);
    CHECK(cfg.vsync == false);

    std::error_code dec;
    fs::remove_all(dir, dec);
}



TEST_CASE("core::LoadConfig supports inline comments after values")
{
    const fs::path dir = make_unique_temp_dir() / "inline_comments";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const fs::path p = dir / "config.ini";
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        REQUIRE(f.good());
        f << "windowWidth=800 # pixels\n";
        f << "windowHeight=600 ; pixels\n";
        f << "vsync=true // enable vsync\n";
        f << "; whole line comment\n";
        f << "# whole line comment\n";
    }

    core::Config cfg;
    cfg.windowWidth = 1;
    cfg.windowHeight = 2;
    cfg.vsync = false;

    CHECK(core::LoadConfig(cfg, dir));
    CHECK(cfg.windowWidth == 800);
    CHECK(cfg.windowHeight == 600);
    CHECK(cfg.vsync == true);

    std::error_code dec;
    fs::remove_all(dir, dec);
}
