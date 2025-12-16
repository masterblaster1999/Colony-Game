#include "winpath/winpath.hpp"

#include <windows.h>
#include <string>
#include <vector>

namespace {

std::wstring to_wstring(const std::filesystem::path& p) {
    // std::filesystem::path::c_str() is already wide on Windows
    return p.native();
}

bool write_all(HANDLE h, const std::uint8_t* data, std::uint64_t size) {
    std::uint64_t offset = 0;
    while (offset < size) {
        DWORD chunk = (size - offset > 0x7fffffffULL) ? 0x7fffffffUL : static_cast<DWORD>(size - offset);
        DWORD written = 0;
        if (!WriteFile(h, data + offset, chunk, &written, nullptr))
            return false;
        offset += written;
        if (written == 0) return false;
    }
    return true;
}

} // anon

namespace winpath {

bool atomic_write_file(const std::filesystem::path& dst,
                       const void* data,
                       std::uint64_t size) noexcept
{
    try {
        const auto dir = dst.parent_path();
        std::filesystem::create_directories(dir);

        // Temp file in same directory => same volume => replace is safe/atomic-ish.
        auto tmp = dst;
        tmp += L".tmp";
        tmp += std::to_wstring(GetCurrentProcessId());
        tmp += L".";
        tmp += std::to_wstring(GetTickCount());

        const std::wstring tmpW = to_wstring(tmp);
        const std::wstring dstW = to_wstring(dst);

        HANDLE h = CreateFileW(
            tmpW.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,          // adjust sharing for your needs
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h == INVALID_HANDLE_VALUE)
            return false;

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        const bool okWrite = write_all(h, bytes, size);
        const bool okFlush = okWrite && FlushFileBuffers(h) != 0;
        CloseHandle(h);

        if (!okFlush) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }

        // Prefer ReplaceFileW: designed for this pattern.
        if (ReplaceFileW(dstW.c_str(), tmpW.c_str(),
                         nullptr, // no backup
                         0, nullptr, nullptr))
        {
            return true;
        }

        // Fallback: MoveFileExW with replace-existing + write-through
        if (MoveFileExW(tmpW.c_str(), dstW.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return true;
        }

        // Cleanup temp if replace/move failed
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }
    catch (...) {
        return false;
    }
}

} // namespace winpath
