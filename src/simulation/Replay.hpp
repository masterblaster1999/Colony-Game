#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <cstring>
#include <type_traits>

namespace colony::sim {

// Minimal binary format for deterministic replays.
// File layout (little-endian on Windows):
//   [ReplayHeader | InputEvent[N] | uint32_t CRC32]
// CRC32 is the standard reflected CRC-32 (poly 0xEDB88320), initialized to
// 0xFFFFFFFF and XORed with 0xFFFFFFFF at the end (zlib-style). The CRC
// is computed over all bytes preceding it (header + events).  :contentReference[oaicite:2]{index=2}

inline constexpr char kMagic[8] = { 'C','O','L','R','P','L','Y','1' };
inline constexpr std::uint32_t kVersion = 1;

enum class InputType : std::uint8_t {
    None       = 0,
    MouseMove  = 1,
    MouseButton= 2,
    Key        = 3,
    Command    = 4
};

#pragma pack(push, 1)

struct ReplayHeader {
    std::uint8_t  magic[8];     // "COLRPLY1"
    std::uint32_t version;      // 1
    std::uint64_t worldSeed;    // seed used for world-gen
    std::uint64_t simSeed;      // seed used for RNG inside simulation
};

struct InputEvent {
    std::uint64_t tick;     // fixed-timestep tick index
    InputType     type;     // kind of input
    std::uint8_t  pad;      // reserved (zero)
    std::uint16_t code;     // key/button/command code
    std::int32_t  x;        // mouse x or param
    std::int32_t  y;        // mouse y or param
    float         value;    // axis/scroll/strength
};

#pragma pack(pop)

// --- Sanity checks on the packed ABI (Windows/MSVC) ---
static_assert(std::is_trivially_copyable<ReplayHeader>::value, "ReplayHeader must be POD");
static_assert(std::is_trivially_copyable<InputEvent>::value,   "InputEvent must be POD");

// With pack(1), these should be exact byte sizes on Windows:
static_assert(sizeof(ReplayHeader) == 8 + 4 + 8 + 8, "ReplayHeader packing mismatch");
static_assert(sizeof(InputEvent)   == 8 + 1 + 1 + 2 + 4 + 4 + 4, "InputEvent packing mismatch");
// MSVC pack pragmas are the canonical way to force 1-byte packing for file I/O. :contentReference[oaicite:3]{index=3}

class ReplayWriter {
public:
    explicit ReplayWriter(std::uint64_t worldSeed, std::uint64_t simSeed) noexcept {
        std::memcpy(m_hdr.magic, kMagic, sizeof(kMagic));
        m_hdr.version   = kVersion;
        m_hdr.worldSeed = worldSeed;
        m_hdr.simSeed   = simSeed;
    }

    // Append one input event to the stream.
    void push(const InputEvent& e) { m_events.push_back(e); }

    // Reserve capacity if you know the approximate number of events.
    void reserve(std::size_t n) { m_events.reserve(n); }

    // Allow read-only access to staged events.
    const std::vector<InputEvent>& events() const noexcept { return m_events; }

    // Clear staged events (header is preserved).
    void clear() noexcept { m_events.clear(); }

    // Serialize to disk (binary). Returns false on I/O or CRC failures.
    [[nodiscard]] bool save(const std::string& path) const;

    // ---- CRC utilities ----------------------------------------------------
    // Make CRC callable from outside (fixes earlier access error).
    // This overload matches the out-of-line definition in Replay.cpp.
    static std::uint32_t crc32(const std::uint8_t* p, std::size_t n) noexcept;

    // Convenience overload that forwards to the pointer-based version.
    static std::uint32_t crc32(std::string_view sv) noexcept {
        return crc32(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size());
    }

private:
    ReplayHeader             m_hdr{};
    std::vector<InputEvent>  m_events;
};

class ReplayReader {
public:
    // Load from disk (binary) and verify CRC/magic/version.
    [[nodiscard]] bool load(const std::string& path);

    const ReplayHeader& header() const noexcept { return m_hdr; }
    const std::vector<InputEvent>& events() const noexcept { return m_events; }

    // Reset to empty state.
    void clear() noexcept { m_hdr = ReplayHeader{}; m_events.clear(); }

    // ---- CRC utilities ----------------------------------------------------
    // Expose CRC to callers and tests (mirrors writer API).
    static std::uint32_t crc32(const std::uint8_t* p, std::size_t n) noexcept;

    static std::uint32_t crc32(std::string_view sv) noexcept {
        return crc32(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size());
    }

private:
    ReplayHeader             m_hdr{};
    std::vector<InputEvent>  m_events;
};

} // namespace colony::sim
