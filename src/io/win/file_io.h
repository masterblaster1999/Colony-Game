#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace io {

// ----- Small RAII for HANDLE -----
class UniqueHandle {
public:
    UniqueHandle() noexcept : h_(INVALID_HANDLE_VALUE) {}
    explicit UniqueHandle(HANDLE h) noexcept : h_(h) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = INVALID_HANDLE_VALUE; }
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) { reset(); h_ = o.h_; o.h_ = INVALID_HANDLE_VALUE; }
        return *this;
    }

    HANDLE get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE && h_ != NULL; }

    void reset(HANDLE nh = INVALID_HANDLE_VALUE) noexcept {
        if (h_ != INVALID_HANDLE_VALUE && h_ != NULL) ::CloseHandle(h_);
        h_ = nh;
    }

private:
    HANDLE h_;
};

// ----- Open parameters -----
struct OpenParams {
    bool  sequential_hint = false;  // FILE_FLAG_SEQUENTIAL_SCAN for big reads
    bool  overlapped       = false; // FILE_FLAG_OVERLAPPED for async
    DWORD share            = FILE_SHARE_READ;
};

// Opens a file for read with the requested hints.
bool OpenForRead(const std::wstring& path, const OpenParams& p, UniqueHandle& out, std::wstring* err = nullptr);

// Utility: 64-bit file size via GetFileSizeEx.
bool GetFileSize64(HANDLE h, std::uint64_t& out);

// Read-all, using large sequential buffered reads (1..8 MB chunks typical).
bool ReadAllSequential(const std::wstring& path,
                       std::vector<std::uint8_t>& out,
                       std::wstring* err = nullptr,
                       std::size_t chunkSize = 1u << 20); // 1 MiB default

// Async streaming: overlapped N-deep ring; calls onChunk for each completed block.
// onChunk should return true to continue, false to abort early.
using ChunkCallback = std::function<bool(const std::uint8_t* data, std::size_t bytes, std::uint64_t fileOffset)>;

bool StreamReadOverlapped(const std::wstring& path,
                          std::size_t        chunkSize,
                          int                queueDepth,
                          const ChunkCallback& onChunk,
                          std::wstring*      err = nullptr);

} // namespace io
