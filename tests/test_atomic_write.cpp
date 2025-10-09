// tests/test_atomic_write.cpp
#ifdef _WIN32
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <random>
#include <string>
#include <cstdlib>   // _wdupenv_s
#include <cwchar>    // _snwprintf_s
#include <windows.h>

#if __has_include("platform/win/PathUtilWin.h")
  #include "platform/win/PathUtilWin.h"
  #define COLONY_HAS_PROJECT_PATHUTIL 1
#else
  #define COLONY_HAS_PROJECT_PATHUTIL 0
#endif

namespace fs = std::filesystem;

static fs::path writable_dir() {
#if COLONY_HAS_PROJECT_PATHUTIL
    return winpath::writable_data_dir();
#else
    wchar_t* env = nullptr; size_t len = 0;
    fs::path base = fs::temp_directory_path();
    if (_wdupenv_s(&env, &len, L"LOCALAPPDATA") == 0 && env) {
        base = fs::path(env);
        free(env);
    }
    fs::path out = base / L"ColonyGame";
    fs::create_directories(out);
    return out;
#endif
}

#if COLONY_HAS_PROJECT_PATHUTIL
static bool project_atomic_write(const fs::path& p, const void* data, size_t size_bytes) {
    // Expected signature in your repo.
    return winpath::atomic_write_file(p.wstring(), data, size_bytes);
}
#else
// Local atomic writer used only if the project's helper isn't available.
static bool project_atomic_write(const fs::path& path, const void* data, size_t size_bytes) {
    const fs::path dir = path.parent_path();
    wchar_t tmp_name[MAX_PATH];
    _snwprintf_s(tmp_name, _TRUNCATE, L".%s.tmp.%lu_%llu",
                 path.filename().c_str(), GetCurrentProcessId(),
                 static_cast<unsigned long long>(GetTickCount64()));
    const fs::path tmp = dir / tmp_name;

    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const BYTE* p = static_cast<const BYTE*>(data);
    size_t left = size_bytes;
    while (left) {
        const DWORD chunk = (left > (1u << 20)) ? (1u << 20) : static_cast<DWORD>(left);
        DWORD written = 0;
        if (!WriteFile(h, p, chunk, &written, nullptr)) {
            CloseHandle(h);
            DeleteFileW(tmp.c_str());
            return false;
        }
        left -= written;
        p += written;
    }
    FlushFileBuffers(h);
    CloseHandle(h);

    if (ReplaceFileW(path.c_str(), tmp.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
        return true;
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;

    DeleteFileW(tmp.c_str());
    return false;
}
#endif

static std::vector<std::uint8_t> make_blob(size_t n) {
    std::vector<std::uint8_t> v(n);
    std::mt19937_64 rng(0x9E3779B97F4A7C15ULL); // fixed seed
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(rng() & 0xFF);
    return v;
}

TEST_CASE("atomic_write: small and large roundtrip") {
    const fs::path dir = writable_dir();
    fs::create_directories(dir);
    const fs::path path = dir / L"doctest_atomic_roundtrip.bin";

    const auto small = make_blob(4096);
    REQUIRE(project_atomic_write(path, small.data(), small.size()));
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> r((std::istreambuf_iterator<char>(in)), {});
        CHECK(std::vector<std::uint8_t>(r.begin(), r.end()) == small);
    }

    const auto big = make_blob(10u * 1024u * 1024u);
    REQUIRE(project_atomic_write(path, big.data(), big.size()));
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> r((std::istreambuf_iterator<char>(in)), {});
        CHECK(std::vector<std::uint8_t>(r.begin(), r.end()) == big);
    }

    fs::remove(path);
}

TEST_CASE("atomic_write: overwrite is all-or-nothing") {
    const fs::path dir  = writable_dir();
    const fs::path path = dir / L"doctest_atomic_overwrite.bin";
    fs::create_directories(dir);

    const auto first  = make_blob(123456);
    const auto second = make_blob(654321);

    REQUIRE(project_atomic_write(path, first.data(), first.size()));
    REQUIRE(project_atomic_write(path, second.data(), second.size()));

    std::ifstream in(path, std::ios::binary);
    std::vector<char> r((std::istreambuf_iterator<char>(in)), {});
    CHECK(r.size() == second.size());
    CHECK(std::equal(r.begin(), r.end(), reinterpret_cast<const char*>(second.data())));
    fs::remove(path);
}
#else
// Non-Windows: nothing to test here.
#endif
