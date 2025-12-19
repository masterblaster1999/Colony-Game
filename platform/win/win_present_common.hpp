#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm> // std::clamp
#include <cmath>     // std::floor

struct PresentConfig
{
    int src_w = 0;      // backbuffer width
    int src_h = 0;      // backbuffer height
    bool keep_aspect = true;
    bool integer_scale = false;
};

struct DestRect
{
    RECT rect{};
    float scale = 1.0f;
};

inline DestRect compute_dest_rect(int src_w, int src_h, int dst_w, int dst_h,
                                  bool keep_aspect, bool integer_scale) noexcept
{
    DestRect out{};

    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        out.rect = RECT{0, 0, 0, 0};
        out.scale = 1.0f;
        return out;
    }

    float sx = static_cast<float>(dst_w) / static_cast<float>(src_w);
    float sy = static_cast<float>(dst_h) / static_cast<float>(src_h);

    float s = keep_aspect ? std::min(sx, sy) : sx; // if not keeping aspect, fill X (adjust if you want stretch)
    if (integer_scale)
        s = std::max(1.0f, std::floor(s));

    int w = static_cast<int>(std::round(src_w * s));
    int h = static_cast<int>(std::round(src_h * s));

    // Center
    int left = (dst_w - w) / 2;
    int top  = (dst_h - h) / 2;

    out.rect.left   = left;
    out.rect.top    = top;
    out.rect.right  = left + w;
    out.rect.bottom = top + h;
    out.scale = s;
    return out;
}
