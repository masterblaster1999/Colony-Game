// src/simulation/Replay.cpp
#include "Replay.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace colony::sim {

// standard CRC32 (IEEE 802.3) table
static constexpr std::array<uint32_t, 256> kCrcTable = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}();

uint32_t ReplayWriter::crc32(const uint8_t* p, size_t n) noexcept {
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; i++) c = kCrcTable[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

uint32_t ReplayReader::crc32(const uint8_t* p, size_t n) noexcept {
    return ReplayWriter::crc32(p, n);
}

bool ReplayWriter::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write(reinterpret_cast<const char*>(&m_hdr),
            static_cast<std::streamsize>(sizeof(m_hdr)));

    if (!m_events.empty()) {
        f.write(reinterpret_cast<const char*>(m_events.data()),
                static_cast<std::streamsize>(sizeof(InputEvent) * m_events.size()));
    }

    // CRC of header + events
    std::vector<uint8_t> tmp(sizeof(m_hdr) + sizeof(InputEvent) * m_events.size());
    std::memcpy(tmp.data(), &m_hdr, sizeof(m_hdr));
    if (!m_events.empty()) {
        std::memcpy(tmp.data() + sizeof(m_hdr),
                    m_events.data(),
                    sizeof(InputEvent) * m_events.size());
    }
    const uint32_t crc = crc32(tmp.data(), tmp.size());
    f.write(reinterpret_cast<const char*>(&crc),
            static_cast<std::streamsize>(sizeof(crc)));

    return !!f;
}

bool ReplayReader::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.read(reinterpret_cast<char*>(&m_hdr),
           static_cast<std::streamsize>(sizeof(m_hdr)));

    if (std::memcmp(m_hdr.magic, "COLRPLY1", 8) != 0 || m_hdr.version != 1) return false;

    // read file into memory to verify CRC
    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()),
           static_cast<std::streamsize>(size));

    if (size < sizeof(ReplayHeader) + sizeof(uint32_t)) return false;

    uint32_t crcStored;
    std::memcpy(&crcStored, data.data() + size - sizeof(uint32_t), sizeof(uint32_t));

    const uint32_t crcCalc = crc32(data.data(), size - sizeof(uint32_t));
    if (crcStored != crcCalc) return false;

    // events
    const size_t eventBytes = size - sizeof(ReplayHeader) - sizeof(uint32_t);
    const size_t count = eventBytes / sizeof(InputEvent);
    m_events.resize(count);
    if (count) {
        std::memcpy(m_events.data(),
                    data.data() + sizeof(ReplayHeader),
                    count * sizeof(InputEvent));
    }
    return true;
}

} // namespace colony::sim
