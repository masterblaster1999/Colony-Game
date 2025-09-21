#include "file_io.h"
#include <cassert>
#include <system_error>

namespace io {

static std::wstring format_winerr_(DWORD e) {
    LPWSTR buf = nullptr;
    DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring s = n ? std::wstring(buf, buf + n) : L"(unknown)";
    if (buf) ::LocalFree(buf);
    return s;
}

bool OpenForRead(const std::wstring& path, const OpenParams& p, UniqueHandle& out, std::wstring* err) {
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if (p.sequential_hint) flags |= FILE_FLAG_SEQUENTIAL_SCAN; // perf hint for large sequential access
    if (p.overlapped)      flags |= FILE_FLAG_OVERLAPPED;      // open for async (overlapped) I/O

    HANDLE h = ::CreateFileW(path.c_str(),
                             GENERIC_READ,
                             p.share,
                             nullptr,
                             OPEN_EXISTING,
                             flags,
                             nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = L"CreateFileW failed: " + format_winerr_(::GetLastError());
        return false;
    }
    out.reset(h);
    return true;
}

bool GetFileSize64(HANDLE h, std::uint64_t& out) {
    LARGE_INTEGER li{};
    if (!::GetFileSizeEx(h, &li)) return false;
    out = static_cast<std::uint64_t>(li.QuadPart);
    return true;
}

bool ReadAllSequential(const std::wstring& path,
                       std::vector<std::uint8_t>& out,
                       std::wstring* err,
                       std::size_t chunkSize) {
    UniqueHandle h;
    OpenParams p;
    p.sequential_hint = true;   // FILE_FLAG_SEQUENTIAL_SCAN
    p.overlapped      = false;  // synchronous buffered reads
    if (!OpenForRead(path, p, h, err)) return false;

    std::uint64_t fsize = 0;
    if (GetFileSize64(h.get(), fsize) && fsize > 0) {
        try { out.reserve(static_cast<std::size_t>(fsize)); } catch (...) {}
    }

    std::vector<std::uint8_t> buf(chunkSize);
    DWORD bytesRead = 0;

    for (;;) {
        BOOL ok = ::ReadFile(h.get(), buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr);
        if (!ok) {
            DWORD le = ::GetLastError();
            if (err) *err = L"ReadFile failed: " + format_winerr_(le);
            return false;
        }
        if (bytesRead == 0) break; // EOF
        out.insert(out.end(), buf.data(), buf.data() + bytesRead);
        if (bytesRead < buf.size()) break; // likely EOF reached
    }
    return true;
}

// ----------------------------- Overlapped streaming -----------------------------

struct OverChunk {
    OVERLAPPED              ol{};
    HANDLE                  ev = nullptr;
    std::vector<std::uint8_t> buf;
    std::uint64_t           offset = 0;
    bool                    inflight = false;

    void init(std::size_t sz) {
        ZeroMemory(&ol, sizeof(ol));
        ev = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        buf.resize(sz);
    }
    void reset() {
        if (ev) { ::CloseHandle(ev); ev = nullptr; }
        buf.clear();
        inflight = false;
        ZeroMemory(&ol, sizeof(ol));
        offset = 0;
    }
};

static inline void set_overlapped_offset_(OVERLAPPED& ol, std::uint64_t ofs) {
    ol.Offset     = static_cast<DWORD>(ofs & 0xFFFFFFFFull);
    ol.OffsetHigh = static_cast<DWORD>((ofs >> 32) & 0xFFFFFFFFull);
}

bool StreamReadOverlapped(const std::wstring& path,
                          std::size_t        chunkSize,
                          int                queueDepth,
                          const ChunkCallback& onChunk,
                          std::wstring*      err) {
    if (queueDepth <= 0) queueDepth = 2;
    if (queueDepth > 64) queueDepth = 64; // WaitForMultipleObjects limit
    if (chunkSize == 0)  chunkSize  = 1u << 20; // 1 MiB

    UniqueHandle h;
    OpenParams p;
    p.sequential_hint = true;   // better cache manager behavior for forward scans
    p.overlapped      = true;   // open handle for overlapped I/O
    if (!OpenForRead(path, p, h, err)) return false;

    std::uint64_t fsize = 0;
    bool haveSize = GetFileSize64(h.get(), fsize);
    std::uint64_t nextOffset = 0;

    // Create ring of overlapped chunks
    std::vector<OverChunk> ring(queueDepth);
    for (auto& c : ring) c.init(chunkSize);

    auto submit = [&](OverChunk& c, std::uint64_t ofs) -> bool {
        ResetEvent(c.ev);
        ZeroMemory(&c.ol, sizeof(c.ol));
        c.ol.hEvent = c.ev;
        c.offset    = ofs;
        set_overlapped_offset_(c.ol, ofs);

        DWORD br = 0;
        BOOL ok = ::ReadFile(h.get(), c.buf.data(), static_cast<DWORD>(c.buf.size()),
                             &br, &c.ol);
        if (!ok) {
            DWORD le = ::GetLastError();
            if (le != ERROR_IO_PENDING) {
                if (err) *err = L"ReadFile (overlapped) failed: " + format_winerr_(le);
                return false;
            }
        }
        c.inflight = true;
        return true;
    };

    // Prime pipeline
    int primed = 0;
    for (; primed < queueDepth; ++primed) {
        if (haveSize && nextOffset >= fsize) break;
        if (!submit(ring[primed], nextOffset)) {
            // clean up events
            for (auto& c : ring) c.reset();
            return false;
        }
        nextOffset += chunkSize;
    }
    if (primed == 0) { // empty file
        for (auto& c : ring) c.reset();
        return true;
    }

    // Harvest/submit loop
    int inFlight = primed;
    while (inFlight > 0) {
        // Build arrays of events & indices for the current in-flight set
        std::vector<HANDLE> events;
        std::vector<int>    indices;
        events.reserve(inFlight);
        indices.reserve(inFlight);

        for (int i = 0; i < queueDepth; ++i) {
            if (ring[i].inflight) {
                events.push_back(ring[i].ev);
                indices.push_back(i);
            }
        }
        assert((int)events.size() == inFlight);

        DWORD w = ::WaitForMultipleObjects(static_cast<DWORD>(events.size()),
                                           events.data(), FALSE, INFINITE);
        if (w < WAIT_OBJECT_0 || w >= WAIT_OBJECT_0 + events.size()) {
            if (err) *err = L"WaitForMultipleObjects failed.";
            for (auto& c : ring) c.reset();
            return false;
        }

        int ri = indices[static_cast<int>(w - WAIT_OBJECT_0)];
        OverChunk& c = ring[ri];

        DWORD bytes = 0;
        BOOL ok = ::GetOverlappedResult(h.get(), &c.ol, &bytes, FALSE);
        if (!ok) {
            DWORD le = ::GetLastError();
            if (err) *err = L"GetOverlappedResult failed: " + format_winerr_(le);
            for (auto& c2 : ring) c2.reset();
            return false;
        }

        c.inflight = false;
        // Callback with completed data
        if (bytes > 0) {
            if (!onChunk(c.buf.data(), bytes, c.offset)) {
                for (auto& c2 : ring) c2.reset();
                return true; // aborted by client
            }
        }

        // EOF?
        bool eofBySize = haveSize && (c.offset + bytes >= fsize);
        bool eofByShortRead = (bytes < c.buf.size());
        if (eofBySize || eofByShortRead) {
            // do not resubmit this slot
            --inFlight;
        } else {
            // Resubmit next logical block
            std::uint64_t ofs = nextOffset;
            if (haveSize && ofs >= fsize) {
                --inFlight; // nothing else to submit
            } else {
                if (!submit(c, ofs)) {
                    for (auto& c2 : ring) c2.reset();
                    return false;
                }
                nextOffset += chunkSize;
                // inFlight unchanged (completed -> immediately pending again)
            }
        }
    }

    for (auto& c : ring) c.reset();
    return true;
}

} // namespace io
