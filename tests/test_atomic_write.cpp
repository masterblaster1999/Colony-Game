// tests/test_atomic_write.cpp
//
// Focused tests for our Windows file-IO helpers (retry/backoff reads + atomic writes).
// These are small but important for stability when Defender/Explorer/editor processes
// briefly lock files.

#include <doctest/doctest.h>

#if defined(_WIN32)

#include "platform/win/PathUtilWin.h"

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

    fs::path dir = base / ("colony_game_tests_" + std::to_string(pid) + "_" + std::to_string(tid));
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace

TEST_CASE("winpath::atomic_write_file round-trips bytes")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "atomic_write_roundtrip.txt";

    INFO("path: ", p.string());

    std::error_code ec;

    const bool okWrite = winpath::atomic_write_file(p, "hello\n", &ec);
    INFO("atomic_write_file ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(okWrite);

    std::string read;
    ec.clear();
    const bool okRead = winpath::read_file_to_string_with_retry(p, read, &ec, /*max_bytes=*/1024, /*max_attempts=*/16);
    INFO("read_file_to_string_with_retry ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(okRead);
    CHECK(read == "hello\n");

    // Overwrite should also succeed.
    ec.clear();
    const bool okWrite2 = winpath::atomic_write_file(p, "world\n", &ec);
    INFO("atomic_write_file overwrite ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(okWrite2);

    read.clear();
    ec.clear();
    const bool okRead2 = winpath::read_file_to_string_with_retry(p, read, &ec, /*max_bytes=*/1024, /*max_attempts=*/16);
    INFO("read_file_to_string_with_retry overwrite ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(okRead2);
    CHECK(read == "world\n");

    // Cleanup.
    (void)winpath::remove_with_retry(p, &ec, /*max_attempts=*/32);
    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::read_file_to_string_with_retry enforces max_bytes")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "read_max_bytes_guard.txt";

    std::error_code ec;

    // 10 bytes payload.
    CHECK(winpath::atomic_write_file(p, "0123456789", &ec));
    CHECK(ec.value() == 0);

    std::string out;
    ec.clear();
    CHECK_FALSE(winpath::read_file_to_string_with_retry(p, out, &ec, /*max_bytes=*/5, /*max_attempts=*/4));
    CHECK(ec == std::errc::file_too_large);

    // Cleanup.
    (void)winpath::remove_with_retry(p, &ec, /*max_attempts=*/32);
    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::remove_with_retry treats missing path as success")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "missing_file.txt";

    std::error_code ec;
    CHECK(winpath::remove_with_retry(p, &ec, /*max_attempts=*/8));
    CHECK(ec.value() == 0);

    std::error_code dec;
    fs::remove_all(dir, dec);
}

#else

TEST_CASE("winpath IO tests are Windows-only")
{
    WARN("Skipping winpath IO tests (not a Windows build).");
}

#endif
