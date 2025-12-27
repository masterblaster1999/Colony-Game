// tests/test_io_atomic_file.cpp
//
// Regression coverage for cg::io::write_atomic/read_all (Windows-only).
// These helpers provide durable, atomic writes (temp + flush + ReplaceFile/MoveFile)
// and fast full-file reads.

#include <doctest/doctest.h>

#if defined(_WIN32)

#include "io/AtomicFile.h"

#include <windows.h>

#include <filesystem>
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

    const auto pid = static_cast<unsigned long>(::GetCurrentProcessId());
    const auto tid = static_cast<unsigned long>(::GetCurrentThreadId());

    fs::path dir = base / ("colony_game_tests_io_" + std::to_string(pid) + "_" + std::to_string(tid));
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace

TEST_CASE("cg::io::write_atomic round-trips bytes")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "atomic_io_roundtrip.txt";

    INFO("path: ", p.string());

    std::string err;
    CHECK(cg::io::write_atomic(p, "hello\n", &err, /*make_backup=*/true));
    CHECK(err.empty());

    std::string read;
    err.clear();
    CHECK(cg::io::read_all(p, read, &err));
    CHECK(err.empty());
    CHECK(read == "hello\n");

    // Overwrite should succeed and (with make_backup=true) preserve the prior version as .bak
    err.clear();
    CHECK(cg::io::write_atomic(p, "world\n", &err, /*make_backup=*/true));
    CHECK(err.empty());

    read.clear();
    err.clear();
    CHECK(cg::io::read_all(p, read, &err));
    CHECK(err.empty());
    CHECK(read == "world\n");

    const fs::path bak = cg::io::default_backup_path(p);
    INFO("backup: ", bak.string());

    std::string bak_read;
    err.clear();
    CHECK(cg::io::read_all(bak, bak_read, &err));
    CHECK(err.empty());
    CHECK(bak_read == "hello\n");

    // Cleanup.
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(bak, ec);
    fs::remove_all(dir, ec);
}

TEST_CASE("cg::io::write_atomic make_backup=false does not create .bak")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "atomic_io_no_bak.txt";
    const fs::path bak = cg::io::default_backup_path(p);

    std::string err;
    CHECK(cg::io::write_atomic(p, "first", &err, /*make_backup=*/false));
    CHECK(err.empty());
    CHECK_FALSE(fs::exists(bak));

    err.clear();
    CHECK(cg::io::write_atomic(p, "second", &err, /*make_backup=*/false));
    CHECK(err.empty());
    CHECK_FALSE(fs::exists(bak));

    // Cleanup.
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove_all(dir, ec);
}

#else

TEST_CASE("cg::io atomic file tests are Windows-only")
{
    WARN("Skipping cg::io atomic file tests (not a Windows build).");
}

#endif
