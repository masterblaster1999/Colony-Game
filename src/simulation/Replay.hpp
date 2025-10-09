#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>

namespace colony::sim {

// Minimal binary format for deterministic replays.
// Header is followed by packed InputEvent records and a trailing CRC32.

enum class InputType : uint8_t { None=0, MouseMove=1, MouseButton=2, Key=3, Command=4 };

#pragma pack(push, 1)
struct ReplayHeader {
    uint8_t  magic[8];     // "COLRPLY1"
    uint32_t version;      // 1
    uint64_t worldSeed;    // seed used for world-gen
    uint64_t simSeed;      // seed used for RNG inside simulation
};

struct InputEvent {
    uint64_t tick;     // fixed-timestep tick index
    InputType type;    // kind of input
    uint8_t  pad;      // reserved
    uint16_t code;     // key/button/command code
    int32_t  x;        // mouse x or param
    int32_t  y;        // mouse y or param
    float    value;    // axis/scroll/strength
};
#pragma pack(pop)

class ReplayWriter {
public:
    explicit ReplayWriter(uint64_t worldSeed, uint64_t simSeed)
    {
        std::memcpy(m_hdr.magic, "COLRPLY1", 8);
        m_hdr.version = 1;
        m_hdr.worldSeed = worldSeed;
        m_hdr.simSeed   = simSeed;
    }

    void push(const InputEvent& e) { m_events.push_back(e); }

    bool save(const std::string& path) const;

private:
    static uint32_t crc32(const uint8_t* p, size_t n);

    ReplayHeader           m_hdr{};
    std::vector<InputEvent> m_events;
};

class ReplayReader {
public:
    bool load(const std::string& path);
    const ReplayHeader& header() const { return m_hdr; }
    const std::vector<InputEvent>& events() const { return m_events; }

private:
    static uint32_t crc32(const uint8_t* p, size_t n);

    ReplayHeader           m_hdr{};
    std::vector<InputEvent> m_events;
};

} // namespace colony::sim
