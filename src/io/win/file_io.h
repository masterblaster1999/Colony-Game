#pragma once

// Keep Windows headers as small and well-behaved as possible.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace io {

// ----- Small RAII for HANDLE -----
// UniqueHandle owns a Windows HANDLE and closes it on destruction.
// - Non-copyable, movable.
// - Default-constructed as INVALID_HANDLE_VALUE (i.e., "no handle").
class UniqueHandle {
public:
    UniqueHandle() noexcept
        : h_(INVALID_HANDLE_VALUE) {}

    explicit UniqueHandle(HANDLE h) noexcept
        : h_(h) {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : h_(other.h_) {
        other.h_ = INVALID_HANDLE_VALUE;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            h_ = other.h_;
            other.h_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    // Raw handle access (do not CloseHandle() it yourself; use release() or reset()).
    HANDLE get() const noexcept { return h_; }

    // Returns true if the handle is valid (not null, not INVALID_HANDLE_VALUE).
    bool is_valid() const noexcept {
        return h_ != INVALID_HANDLE_VALUE && h_ != NULL;
    }

    explicit operator bool() const noexcept {
        return is_valid();
    }

    // Reset to a new handle, closing the old one if it was valid.
    void reset(HANDLE nh = INVALID_HANDLE_VALUE) noexcept {
        if (is_valid()) {
            ::CloseHandle(h_);
        }
        h_ = nh;
    }

    // Release ownership without closing; returns the raw HANDLE and
    // leaves this UniqueHandle in an invalid state.
    HANDLE release() noexcept {
        HANDLE tmp = h_;
        h_ = INVALID_HANDLE_VALUE;
        return tmp;
    }

    // Convenience: prepare this for APIs that write into a HANDLE* out param.
    // Example:
    //   UniqueHandle h;
    //   ::CreateFileW(..., h.put());
    HANDLE* put() noexcept {
        reset();
        return &h_;
    }

private:
    HANDLE h_;
};

// ----- Open parameters -----
// Used to tune how OpenForRead configures CreateFileW.
struct OpenParams {
    bool  sequential_hint = false;  // FILE_FLAG_SEQUENTIAL_SCAN for big reads
    bool  overlapped       = false; // FILE_FLAG_OVERLAPPED for async
    DWORD share            = FILE_SHARE_READ;
};

// Opens a file for read with the requested hints.
// On success, 'out' owns a valid HANDLE. On failure, returns false and optionally
// writes a human-readable message into 'err'.
bool OpenForRead(const std::wstring& path,
                 const OpenParams&   p,
                 UniqueHandle&       out,
                 std::wstring*       err = nullptr);

// Utility: 64-bit file size via GetFileSizeEx.
bool GetFileSize64(HANDLE h, std::uint64_t& out);

// Read-all, using large sequential buffered reads (1..8 MB chunks typical).
// Uses FILE_FLAG_SEQUENTIAL_SCAN if requested in OpenParams.
bool ReadAllSequential(const std::wstring&    path,
                       std::vector<std::uint8_t>& out,
                       std::wstring*          err       = nullptr,
                       std::size_t            chunkSize = 1u << 20); // 1 MiB default

// Async streaming: overlapped N-deep ring; calls onChunk for each completed block.
// onChunk should return true to continue, false to abort early.
using ChunkCallback =
    std::function<bool(const std::uint8_t* data,
                       std::size_t        bytes,
                       std::uint64_t      fileOffset)>;

bool StreamReadOverlapped(const std::wstring& path,
                          std::size_t        chunkSize,
                          int                queueDepth,
                          const ChunkCallback& onChunk,
                          std::wstring*      err = nullptr);

} // namespace io
