// tests/test_winpath_read_with_retry.cpp
//
// Regression tests for winpath::read_file_to_string_with_retry.
//
// This helper exists because Windows file reads can fail transiently when other processes
// briefly hold handles (Defender scans, Explorer preview handlers, editors doing temp-file
// swaps, etc.). Many user-editable files rely on this function so a small regression can
// cascade into "settings reset" or "bindings reset" surprises.

#include <doctest/doctest.h>

#if defined(_WIN32)

#include "platform/win/PathUtilWin.h"

#include <windows.h>

#include <atomic>
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
    const auto tick = static_cast<unsigned long long>(::GetTickCount64());

    static std::atomic<unsigned long long> counter{0};
    const auto c = counter.fetch_add(1, std::memory_order_relaxed);

    fs::path dir = base / ("colony_game_tests_read_" +
                           std::to_string(pid) + "_" +
                           std::to_string(tid) + "_" +
                           std::to_string(tick) + "_" +
                           std::to_string(c));
    fs::create_directories(dir, ec);
    return dir;
}

bool write_bytes_atomic(const fs::path& p, const void* data, std::size_t size)
{
    std::error_code ec;
    const bool ok = winpath::atomic_write_file(p, data, size, &ec);
    INFO("atomic_write_file ec: ", ec.message(), " (code ", ec.value(), ")");
    return ok && (ec.value() == 0);
}

} // namespace

TEST_CASE("winpath::read_file_to_string_with_retry reads full contents")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "read_retry_roundtrip.txt";

    const std::string payload = "hello world\n";
    REQUIRE(write_bytes_atomic(p, payload.data(), payload.size()));

    std::string out;
    std::error_code ec;
    CHECK(winpath::read_file_to_string_with_retry(p, out, &ec, /*max_bytes=*/1024, /*max_attempts=*/8));
    INFO("read_file_to_string_with_retry ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(ec.value() == 0);
    CHECK(out == payload);

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::read_file_to_string_with_retry returns true for empty files")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "empty.txt";

    // Create an empty file.
    HANDLE h = ::CreateFileW(
        p.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    REQUIRE(h != INVALID_HANDLE_VALUE);
    ::CloseHandle(h);

    std::string out = "should be cleared";
    std::error_code ec;
    CHECK(winpath::read_file_to_string_with_retry(p, out, &ec, /*max_bytes=*/1024, /*max_attempts=*/8));
    INFO("read_file_to_string_with_retry (empty) ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(ec.value() == 0);
    CHECK(out.empty());

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::read_file_to_string_with_retry enforces max_bytes guardrail")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "too_big.txt";

    const std::string payload = "0123456789ABCDEF"; // 16 bytes
    REQUIRE(write_bytes_atomic(p, payload.data(), payload.size()));

    std::string out;
    std::error_code ec;
    const bool ok = winpath::read_file_to_string_with_retry(p, out, &ec, /*max_bytes=*/8, /*max_attempts=*/4);
    CHECK_FALSE(ok);
    CHECK(out.empty());
    CHECK(ec == std::make_error_code(std::errc::file_too_large));

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::read_file_to_string_with_retry reports missing file error")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "does_not_exist.txt";

    std::string out;
    std::error_code ec;
    const bool ok = winpath::read_file_to_string_with_retry(p, out, &ec, /*max_bytes=*/1024, /*max_attempts=*/4);
    CHECK_FALSE(ok);
    CHECK(out.empty());
    INFO("missing file ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK((ec.value() == ERROR_FILE_NOT_FOUND || ec.value() == ERROR_PATH_NOT_FOUND));

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::read_file_to_string_with_retry rejects empty path")
{
    std::string out;
    std::error_code ec;
    const bool ok = winpath::read_file_to_string_with_retry(fs::path{}, out, &ec, /*max_bytes=*/1024, /*max_attempts=*/1);
    CHECK_FALSE(ok);
    CHECK(out.empty());
    CHECK(ec == std::make_error_code(std::errc::invalid_argument));
}

#else

TEST_CASE("winpath read-with-retry tests are Windows-only")
{
    WARN("Skipping winpath read-with-retry tests (not a Windows build).");
}

#endif
