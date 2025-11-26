#include "u16_raster.hpp"
#include <fstream>

namespace wg {

static inline void le_write_u16(std::ofstream& os, uint16_t v) {
    unsigned char b[2] = { static_cast<unsigned char>(v & 0xFF),
                           static_cast<unsigned char>((v >> 8) & 0xFF) };
    os.write(reinterpret_cast<const char*>(b), 2);
}

static inline uint16_t le_read_u16(std::ifstream& is) {
    unsigned char b[2]{};
    is.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (uint16_t(b[1]) << 8));
}

bool write_u16_raw(const std::string& path, const U16Raster& r) {
    if (r.pixels.size() != static_cast<size_t>(r.width) * r.height) return false;
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    for (uint16_t v : r.pixels) le_write_u16(os, v);
    return os.good();
}

bool read_u16_raw(const std::string& path, uint32_t width, uint32_t height, U16Raster& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    out.width = width; out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < out.pixels.size(); ++i) {
        if (!is.good()) return false;
        out.pixels[i] = le_read_u16(is);
    }
    return true;
}

} // namespace wg
