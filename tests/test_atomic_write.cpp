#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <system_error>

static void remove_with_retry(const std::filesystem::path& p) {
    std::error_code ec;

    // Try for ~1–2 seconds total with backoff.
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::filesystem::remove(p, ec);
        if (!ec) return; // deleted successfully

        const int v = ec.value(); // on Windows: Win32 error code
        if (v == ERROR_SHARING_VIOLATION ||
            v == ERROR_LOCK_VIOLATION ||
            v == ERROR_ACCESS_DENIED) {
            Sleep((attempt < 6) ? (1u << attempt) : 50u);
            continue;
        }

        // Non-retryable: throw like filesystem::remove would.
        throw std::filesystem::filesystem_error("remove", p, ec);
    }

    // Still failed after retries: throw for visibility.
    throw std::filesystem::filesystem_error("remove", p, ec);
}
#else
static void remove_with_retry(const std::filesystem::path& p) {
    std::filesystem::remove(p);
}
#endif
