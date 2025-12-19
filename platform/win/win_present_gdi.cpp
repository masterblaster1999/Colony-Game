// platform/win/win_present_gdi.cpp
// Pure GDI present helpers — no dependency on engine types, only POD parameters.
//
// Improvements applied:
//  - Fix INT32_MAX build error by removing the macro dependency (use numeric_limits sentinel).
//  - Add missing standard headers for size_t / numeric_limits / std::isfinite.
//  - Guard against invalid sizes (0/negative backbuffer or client sizes).
//  - Avoid per-call brush allocation by using DC_BRUSH + SetDCBrushColor.
//  - Save/restore HDC state (stretch modes, brush origin, brush color) via SaveDC/RestoreDC.
//  - Make integerScale behave nicely when window is smaller than backbuffer (no cropping).
//  - Make dirty-rect mapping robust (clamp rects to backbuffer; map with MulDiv for stability).

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#  define NOMINMAX 1
#endif
#include <windows.h>

#include <cstddef>  // size_t
#include <cstdint>  // std::int32_t
#include <cmath>    // std::floor, std::isfinite
#include <limits>   // std::numeric_limits

struct PresentConfig { bool integerScale; bool smoothScale; };

namespace
{
constexpr LONG kDirtyRectSentinel = static_cast<LONG>((std::numeric_limits<std::int32_t>::max)());

static inline int clampi(int v, int lo, int hi) noexcept
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

struct ScopedSaveDC final
{
    HDC hdc = nullptr;
    int state = 0;

    explicit ScopedSaveDC(HDC in) noexcept : hdc(in), state(in ? SaveDC(in) : 0) {}
    ScopedSaveDC(const ScopedSaveDC&) = delete;
    ScopedSaveDC& operator=(const ScopedSaveDC&) = delete;

    ~ScopedSaveDC()
    {
        if (state)
            RestoreDC(hdc, state);
    }
};

static RECT compute_dest_rect_(int cw, int ch, int bw, int bh, bool integerScale, float* outScale) noexcept
{
    if (outScale)
        *outScale = 1.0f;

    RECT r{0, 0, 0, 0};
    if (cw <= 0 || ch <= 0 || bw <= 0 || bh <= 0)
        return r;

    const float sx = static_cast<float>(cw) / static_cast<float>(bw);
    const float sy = static_cast<float>(ch) / static_cast<float>(bh);
    float minScale = (sx < sy) ? sx : sy;

    if (!std::isfinite(minScale) || minScale <= 0.0f)
        minScale = 1.0f;

    float scale = minScale;

    if (integerScale)
    {
        // Only integer-upscale. If the window is smaller than the backbuffer,
        // fall back to non-integer downscale so the image still fits (no cropping).
        if (minScale >= 1.0f)
        {
            const int intScale = clampi(static_cast<int>(std::floor(minScale)), 1, 4096);
            scale = static_cast<float>(intScale);
        }
        else
        {
            scale = minScale;
        }
    }

    if (!std::isfinite(scale) || scale <= 0.0f)
        scale = 1.0f;

    int dw = static_cast<int>(std::floor(static_cast<float>(bw) * scale));
    int dh = static_cast<int>(std::floor(static_cast<float>(bh) * scale));
    dw = clampi(dw, 1, cw);
    dh = clampi(dh, 1, ch);

    int dx = (cw - dw) / 2;
    int dy = (ch - dh) / 2;
    dx = clampi(dx, 0, cw - dw);
    dy = clampi(dy, 0, ch - dh);

    if (outScale)
        *outScale = scale;

    r.left = dx;
    r.top = dy;
    r.right = dx + dw;
    r.bottom = dy + dh;
    return r;
}

// Paint letterbox/pillarbox bands around a destination rectangle.
static void paint_bands_(HDC hdc, int cw, int ch, const RECT& dst) noexcept
{
    if (!hdc || cw <= 0 || ch <= 0)
        return;

    const int dstLeft = clampi(dst.left, 0, cw);
    const int dstTop = clampi(dst.top, 0, ch);
    const int dstRight = clampi(dst.right, 0, cw);
    const int dstBottom = clampi(dst.bottom, 0, ch);

    if (dstLeft == 0 && dstTop == 0 && dstRight == cw && dstBottom == ch)
        return; // No bands needed.

    // Avoid CreateSolidBrush/DeleteObject churn: use DC_BRUSH.
    HBRUSH br = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    const COLORREF oldColor = SetDCBrushColor(hdc, RGB(10, 10, 10));

    RECT r;
    if (dstTop > 0)
    {
        SetRect(&r, 0, 0, cw, dstTop);
        FillRect(hdc, &r, br);
    }
    if (dstLeft > 0 && dstBottom > dstTop)
    {
        SetRect(&r, 0, dstTop, dstLeft, dstBottom);
        FillRect(hdc, &r, br);
    }
    if (dstRight < cw && dstBottom > dstTop)
    {
        SetRect(&r, dstRight, dstTop, cw, dstBottom);
        FillRect(hdc, &r, br);
    }
    if (dstBottom < ch)
    {
        SetRect(&r, 0, dstBottom, cw, ch);
        FillRect(hdc, &r, br);
    }

    SetDCBrushColor(hdc, oldColor);
}

static inline bool should_smooth_scale_(const PresentConfig& cfg, float uniformScale) noexcept
{
    // If integer scaling is requested and we are actually >= 1, prefer nearest-neighbor.
    // If we're forced to downscale (<1), allow smoothing (if enabled).
    return cfg.smoothScale && (!cfg.integerScale || uniformScale < 1.0f);
}

static inline void configure_stretch_mode_(HDC hdc, bool smooth) noexcept
{
    if (!hdc)
        return;

    if (smooth)
    {
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr); // Required for HALFTONE mode.
    }
    else
    {
        SetStretchBltMode(hdc, COLORONCOLOR);
    }
}
} // namespace

void gdi_present_full(HWND hwnd, HDC hdc, int backW, int backH,
                      const void* pixels, const BITMAPINFO* bmi,
                      const PresentConfig& cfg)
{
    if (!hwnd || !hdc || !pixels || !bmi)
        return;
    if (backW <= 0 || backH <= 0)
        return;

    RECT cr{};
    if (!GetClientRect(hwnd, &cr))
        return;

    const int cw = cr.right - cr.left;
    const int ch = cr.bottom - cr.top;
    if (cw <= 0 || ch <= 0)
        return;

    ScopedSaveDC scoped(hdc);

    float s = 1.0f;
    const RECT dst = compute_dest_rect_(cw, ch, backW, backH, cfg.integerScale, &s);

    paint_bands_(hdc, cw, ch, dst);

    const bool smooth = should_smooth_scale_(cfg, s);
    configure_stretch_mode_(hdc, smooth);

    const int dx = dst.left;
    const int dy = dst.top;
    const int dw = dst.right - dst.left;
    const int dh = dst.bottom - dst.top;
    if (dw <= 0 || dh <= 0)
        return;

    StretchDIBits(hdc,
                  dx, dy, dw, dh,
                  0, 0, backW, backH,
                  pixels, bmi,
                  DIB_RGB_COLORS, SRCCOPY);
}

void gdi_present_dirty(HWND hwnd, HDC hdc, int backW, int backH,
                       const void* pixels, const BITMAPINFO* bmi,
                       const RECT* rects, size_t numRects,
                       const PresentConfig& cfg)
{
    if (!rects || numRects == 0)
    {
        gdi_present_full(hwnd, hdc, backW, backH, pixels, bmi, cfg);
        return;
    }

    if (!hwnd || !hdc || !pixels || !bmi)
        return;
    if (backW <= 0 || backH <= 0)
        return;

    RECT cr{};
    if (!GetClientRect(hwnd, &cr))
        return;

    const int cw = cr.right - cr.left;
    const int ch = cr.bottom - cr.top;
    if (cw <= 0 || ch <= 0)
        return;

    ScopedSaveDC scoped(hdc);

    float s = 1.0f;
    const RECT dst = compute_dest_rect_(cw, ch, backW, backH, cfg.integerScale, &s);

    paint_bands_(hdc, cw, ch, dst);

    const bool smooth = should_smooth_scale_(cfg, s);
    configure_stretch_mode_(hdc, smooth);

    const int dstLeft = dst.left;
    const int dstTop = dst.top;
    const int dstW = dst.right - dst.left;
    const int dstH = dst.bottom - dst.top;
    if (dstW <= 0 || dstH <= 0)
        return;

    for (size_t i = 0; i < numRects; ++i)
    {
        const RECT in = rects[i];

        // Clamp & normalize the source rect against backbuffer bounds.
        const int srcLeft = clampi(static_cast<int>(in.left), 0, backW);
        const int srcTop = clampi(static_cast<int>(in.top), 0, backH);

        const int rawRight = (in.right == kDirtyRectSentinel) ? backW : static_cast<int>(in.right);
        const int rawBottom = (in.bottom == kDirtyRectSentinel) ? backH : static_cast<int>(in.bottom);

        const int srcRight = clampi(rawRight, srcLeft, backW);
        const int srcBottom = clampi(rawBottom, srcTop, backH);

        const int sw = srcRight - srcLeft;
        const int sh = srcBottom - srcTop;

        // If dirty rectangles are malformed, fall back to full present.
        if (sw <= 0 || sh <= 0)
        {
            gdi_present_full(hwnd, hdc, backW, backH, pixels, bmi, cfg);
            return;
        }

        // Map source rect -> destination rect using integer math (stable pixel coverage).
        int ddx = dstLeft + MulDiv(srcLeft, dstW, backW);
        int ddy = dstTop + MulDiv(srcTop, dstH, backH);

        int ddx2 = dstLeft + MulDiv(srcRight, dstW, backW);
        int ddy2 = dstTop + MulDiv(srcBottom, dstH, backH);

        int ddw = ddx2 - ddx;
        int ddh = ddy2 - ddy;

        // At extreme downscale, a tiny dirty rect can map to 0 destination pixels.
        if (ddw <= 0) ddw = 1;
        if (ddh <= 0) ddh = 1;

        // Keep destination rect inside dst.
        ddx = clampi(ddx, dstLeft, dstLeft + dstW - ddw);
        ddy = clampi(ddy, dstTop, dstTop + dstH - ddh);

        StretchDIBits(hdc,
                      ddx, ddy, ddw, ddh,
                      srcLeft, srcTop, sw, sh,
                      pixels, bmi,
                      DIB_RGB_COLORS, SRCCOPY);
    }
}
