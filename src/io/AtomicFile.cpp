#include "io/AtomicFile.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace {
    static bool write_temp_and_flush(const std::filesystem::path& temp, const std::string& bytes, std::string* err) {
        HANDLE h = ::CreateFileW(temp.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (err) *err = "CreateFileW failed: " + std::to_string(GetLastError());
            return false;
        }
        const BYTE* ptr = reinterpret_cast<const BYTE*>(bytes.data());
        size_t remaining = bytes.size();
        while (remaining > 0) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 1<<20));
            DWORD written = 0;
            if (!::WriteFile(h, ptr, chunk, &written, nullptr)) {
                if (err) *err = "WriteFile failed: " + std::to_string(GetLastError());
                ::CloseHandle(h);
                return false;
            }
            remaining -= written;
            ptr += written;
        }
        ::FlushFileBuffers(h); // flush file contents
        ::CloseHandle(h);
        return true;
    }
}

namespace cg::io {
    bool write_atomic(const std::filesystem::path& path,
                      const std::string& bytes,
                      std::string* err,
                      bool make_backup)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        auto tmp = path; tmp += L".tmp";
        auto bak = path; bak += L".bak";

        if (!write_temp_and_flush(tmp, bytes, err)) return false;

        // If a previous save exists, prefer ReplaceFileW to keep a backup
        if (make_backup && std::filesystem::exists(path)) {
            if (::ReplaceFileW(path.wstring().c_str(),
                               tmp.wstring().c_str(),
                               bak.wstring().c_str(),
                               REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
                return true;
            }
            // fall through to MoveFileExW if ReplaceFileW fails for any reason
        }

        // Atomic (same volume) replace + write‑through of metadata. 
        // MOVEFILE_WRITE_THROUGH ensures the operation is flushed before return. 
        if (!::MoveFileExW(tmp.wstring().c_str(), path.wstring().c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            if (err) *err = "MoveFileExW failed: " + std::to_string(GetLastError());
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }

    bool read_all(const std::filesystem::path& p, std::string& out, std::string* err) {
        std::ifstream in(p, std::ios::binary);
        if (!in) { if (err) *err = "open failed"; return false; }
        in.seekg(0, std::ios::end);
        auto sz = in.tellg();
        in.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(sz));
        if (sz > 0) in.read(out.data(), sz);
        return true;
    }
}
