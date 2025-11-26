#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wg {

struct U16Raster {
    uint32_t width{};
    uint32_t height{};
    std::vector<uint16_t> pixels; // row-major, little-endian on disk
};

bool write_u16_raw(const std::string& path, const U16Raster& r);
bool read_u16_raw(const std::string& path, uint32_t width, uint32_t height, U16Raster& out);

} // namespace wg
