#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

struct Framebuffer {
    int width = 0, height = 0, pitch = 0;
    std::vector<uint32_t> pixels; // 0xAARRGGBB; DIB expects BGRA in memory (little-endian OK)
    Framebuffer() = default;
    Framebuffer(int w, int h) { resize(w, h); }

    void resize(int w, int h) {
        width = std::max(1, w);
        height = std::max(1, h);
        pitch = width;
        pixels.assign(size_t(width) * size_t(height), 0xFF000000u);
    }

    inline bool in_bounds(int x, int y) const {
        return (unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height;
    }

    inline void clear(uint32_t argb) {
        std::fill(pixels.begin(), pixels.end(), argb);
    }

    inline void put(int x, int y, uint32_t argb) {
        if (in_bounds(x, y)) pixels[size_t(y) * pitch + x] = argb;
    }
};

// Helpers
static inline uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    // Stored as 0xAARRGGBB; on little-endian this is BGRA in memory (what StretchDIBits wants).
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// Tiny 2D immediate-mode renderer
struct Renderer2D {
    Framebuffer* fb = nullptr;
    explicit Renderer2D(Framebuffer* target = nullptr) : fb(target) {}
    void bind(Framebuffer* target) { fb = target; }

    void fill_rect(int x, int y, int w, int h, uint32_t c) {
        if (!fb) return;
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min(fb->width, x + w), y1 = std::min(fb->height, y + h);
        for (int yy = y0; yy < y1; ++yy) {
            uint32_t* row = fb->pixels.data() + size_t(yy) * fb->pitch;
            for (int xx = x0; xx < x1; ++xx) row[xx] = c;
        }
    }

    // Bresenham line
    void line(int x0, int y0, int x1, int y1, uint32_t c) {
        if (!fb) return;
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            fb->put(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // Simple filled triangle (barycentric floats; good enough for UI/tiles/markers)
    void tri_fill(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
        if (!fb) return;
        int minX = (int)std::floor(std::min({ x0, x1, x2 }));
        int maxX = (int)std::ceil (std::max({ x0, x1, x2 }));
        int minY = (int)std::floor(std::min({ y0, y1, y2 }));
        int maxY = (int)std::ceil (std::max({ y0, y1, y2 }));
        minX = std::max(minX, 0); minY = std::max(minY, 0);
        maxX = std::min(maxX, fb->width - 1); maxY = std::min(maxY, fb->height - 1);

        float denom = (y1 - y2)*(x0 - x2) + (x2 - x1)*(y0 - y2);
        if (std::abs(denom) < 1e-8f) return;

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float a = ((y1 - y2)*(x - x2) + (x2 - x1)*(y - y2)) / denom;
                float b = ((y2 - y0)*(x - x2) + (x0 - x2)*(y - y2)) / denom;
                float g = 1.0f - a - b;
                if (a >= 0 && b >= 0 && g >= 0) fb->put(x, y, c);
            }
        }
    }
};

// Simple procedural color (no external textures)
static inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline uint32_t tile_color(int tx, int ty) {
    uint32_t h = hash32(uint32_t(tx * 73856093) ^ uint32_t(ty * 19349663));
    uint8_t r = uint8_t(128 + (h & 63));
    uint8_t g = uint8_t(80 + ((h >> 8) & 127));
    uint8_t b = uint8_t(80 + ((h >> 16) & 127));
    return RGBA(r, g, b, 255);
}
