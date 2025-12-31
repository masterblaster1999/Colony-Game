// tests/test_winpath_retry_ops.cpp
//
// Regression tests for winpath retry helpers (remove_with_retry / rename_with_retry).
//
// These helpers exist because Windows file operations can fail transiently when other
// processes briefly hold handles (Defender scans, Explorer preview handlers, editors, etc.).

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

    fs::path dir = base / ("colony_game_tests_ops_" + std::to_string(pid) + "_" +
                           std::to_string(tid) + "_" + std::to_string(tick) + "_" + std::to_string(c));
    fs::create_directories(dir, ec);
    return dir;
}

bool set_read_only(const fs::path& p, bool ro)
{
    DWORD attrs = ::GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        attrs = FILE_ATTRIBUTE_NORMAL;

    if (ro)
        attrs |= FILE_ATTRIBUTE_READONLY;
    else
        attrs &= ~FILE_ATTRIBUTE_READONLY;

    return ::SetFileAttributesW(p.c_str(), attrs) != FALSE;
}

} // namespace

TEST_CASE("winpath::rename_with_retry moves a file")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path from = dir / "from.txt";
    const fs::path to = dir / "to.txt";

    std::error_code ec;
    CHECK(winpath::atomic_write_file(from, "hello\n", &ec));
    CHECK(ec.value() == 0);

    ec.clear();
    const bool ok = winpath::rename_with_retry(from, to, &ec, /*max_attempts=*/16);
    INFO("rename_with_retry ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(ok);
    CHECK(ec.value() == 0);

    std::error_code sec;
    CHECK_FALSE(fs::exists(from, sec));
    CHECK(fs::exists(to, sec));

    // Cleanup.
    (void)winpath::remove_with_retry(to, &ec, /*max_attempts=*/32);
    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::rename_with_retry reports error for missing source")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path from = dir / "does_not_exist.txt";
    const fs::path to = dir / "dest.txt";

    std::error_code ec;
    const bool ok = winpath::rename_with_retry(from, to, &ec, /*max_attempts=*/4);
    INFO("rename_with_retry missing source ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK_FALSE(ok);
    CHECK(ec.value() != 0);

    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::copy_file_with_retry copies a file (and supports overwrite toggle)")
{
    const fs::path dir  = make_unique_temp_dir();
    const fs::path from = dir / "from.txt";
    const fs::path to   = dir / "to.txt";

    std::error_code ec;
    CHECK(winpath::atomic_write_file(from, "hello copy\n", &ec));
    CHECK(ec.value() == 0);

    // First copy should succeed.
    ec.clear();
    const bool ok1 = winpath::copy_file_with_retry(from, to, /*overwrite_existing=*/true, &ec, /*max_attempts=*/16);
    INFO("copy_file_with_retry ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(ok1);
    CHECK(ec.value() == 0);

    // Verify content.
    std::string got;
    ec.clear();
    CHECK(winpath::read_file_to_string_with_retry(to, got, &ec, /*max_bytes=*/64 * 1024, /*max_attempts=*/16));
    CHECK(ec.value() == 0);
    CHECK(got == "hello copy\n");

    // If overwrite is disabled, copying onto an existing file should fail.
    ec.clear();
    const bool ok2 = winpath::copy_file_with_retry(from, to, /*overwrite_existing=*/false, &ec, /*max_attempts=*/4);
    CHECK_FALSE(ok2);
    CHECK(ec.value() != 0);

    // Cleanup.
    (void)winpath::remove_with_retry(from, &ec, /*max_attempts=*/32);
    (void)winpath::remove_with_retry(to, &ec, /*max_attempts=*/32);
    std::error_code dec;
    fs::remove_all(dir, dec);
}

TEST_CASE("winpath::remove_with_retry deletes read-only files")
{
    const fs::path dir = make_unique_temp_dir();
    const fs::path p = dir / "readonly.txt";

    std::error_code ec;
    CHECK(winpath::atomic_write_file(p, "ro\n", &ec));
    CHECK(ec.value() == 0);

    // Make it read-only and ensure the attribute is set.
    CHECK(set_read_only(p, true));

    ec.clear();
    const bool ok = winpath::remove_with_retry(p, &ec, /*max_attempts=*/64);
    INFO("remove_with_retry ro ec: ", ec.message(), " (code ", ec.value(), ")");
    CHECK(ok);
    CHECK(ec.value() == 0);

    std::error_code sec;
    CHECK_FALSE(fs::exists(p, sec));

    std::error_code dec;
    fs::remove_all(dir, dec);
}

#else

TEST_CASE("winpath retry-op tests are Windows-only")
{
    WARN("Skipping winpath retry-op tests (not a Windows build).");
}

#endif
