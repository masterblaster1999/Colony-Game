#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <functional>
#include <algorithm>

namespace font5x7 {

// =========================================================
// Core types & helpers
// =========================================================
using u8 = std::uint8_t;

constexpr int GLYPH_W = 5;
constexpr int GLYPH_H = 7;

struct Glyph {
    std::array<u8, GLYPH_H> rows{}; // each element uses the lower 5 bits, left->right in bits [4..0]
};

// Read a bit at (row, col), with col 0 = leftmost (bit 4)
constexpr bool bit(u8 rowBits, int col) {
    return ((rowBits >> (GLYPH_W - 1 - col)) & 0x1) != 0;
}
constexpr bool bit(const Glyph& g, int row, int col) {
    return bit(g.rows[row], col);
}

// Compute visual width (trim empty right columns)
constexpr u8 glyphWidth(const Glyph& g) {
    u8 w = 0;
    for (int c = 0; c < GLYPH_W; ++c) {
        bool any = false;
        for (int r = 0; r < GLYPH_H; ++r) { if (bit(g, r, c)) { any = true; break; } }
        if (any) w = static_cast<u8>(c + 1);
    }
    return w;
}

// Convenience macro to define a glyph from 7 binary rows.
#define F5X7_G7(a,b,c,d,e,f,g) ::font5x7::Glyph{ std::array<u8,GLYPH_H>{ a,b,c,d,e,f,g } }

// =========================================================
// Glyph set (constexpr, adapted from your original file)
// =========================================================

inline constexpr Glyph GLYPH_UNKNOWN = F5X7_G7(
    0b11111,0b10001,0b10101,0b10001,0b10101,0b10001,0b11111
);
inline constexpr Glyph GLYPH_SPACE   = F5X7_G7(
    0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000
);

// Digits 0..9
inline constexpr std::array<Glyph,10> DIGITS{{
    F5X7_G7(0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110), // 0
    F5X7_G7(0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110), // 1
    F5X7_G7(0b01110,0b10001,0b00001,0b00110,0b01000,0b10000,0b11111), // 2
    F5X7_G7(0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110), // 3
    F5X7_G7(0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010), // 4
    F5X7_G7(0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110), // 5
    F5X7_G7(0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110), // 6
    F5X7_G7(0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000), // 7
    F5X7_G7(0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110), // 8
    F5X7_G7(0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100)  // 9
}};

// Uppercase A..Z (lowercase falls back to uppercase shape)
inline constexpr std::array<Glyph,26> UPPER{{
    F5X7_G7(0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001), // A
    F5X7_G7(0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110), // B
    F5X7_G7(0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110), // C
    F5X7_G7(0b11100,0b10010,0b10001,0b10001,0b10001,0b10010,0b11100), // D
    F5X7_G7(0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111), // E
    F5X7_G7(0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000), // F
    F5X7_G7(0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01110), // G
    F5X7_G7(0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001), // H
    F5X7_G7(0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110), // I
    F5X7_G7(0b00001,0b00001,0b00001,0b00001,0b10001,0b10001,0b01110), // J
    F5X7_G7(0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001), // K
    F5X7_G7(0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111), // L
    F5X7_G7(0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001), // M
    F5X7_G7(0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001), // N
    F5X7_G7(0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110), // O
    F5X7_G7(0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000), // P
    F5X7_G7(0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101), // Q
    F5X7_G7(0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001), // R
    F5X7_G7(0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110), // S
    F5X7_G7(0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100), // T
    F5X7_G7(0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110), // U
    F5X7_G7(0b10001,0b10001,0b10001,0b01010,0b01010,0b00100,0b00100), // V
    F5X7_G7(0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001), // W
    F5X7_G7(0b10001,0b01010,0b00100,0b00100,0b01010,0b10001,0b10001), // X
    F5X7_G7(0b10001,0b01010,0b00100,0b00100,0b00100,0b00100,0b00100), // Y
    F5X7_G7(0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111)  // Z
}};

// Punct block "!".."/" from your original
inline constexpr std::array<Glyph,15> PUNCT_33_47{{
    F5X7_G7(0b00000,0b00100,0b00100,0b00100,0b00000,0b00000,0b00100), // !
    F5X7_G7(0b01010,0b01010,0b00000,0b00000,0b00000,0b00000,0b00000), // "
    F5X7_G7(0b01010,0b11111,0b01010,0b01010,0b11111,0b01010,0b00000), // #
    F5X7_G7(0b00100,0b01111,0b10100,0b01110,0b00101,0b11110,0b00100), // $
    F5X7_G7(0b11001,0b11010,0b00100,0b00100,0b01011,0b10011,0b00000), // %
    F5X7_G7(0b01100,0b10010,0b10100,0b01000,0b10101,0b10010,0b01101), // &
    F5X7_G7(0b00100,0b00100,0b00000,0b00000,0b00000,0b00000,0b00000), // '
    F5X7_G7(0b00010,0b00100,0b01000,0b01000,0b01000,0b00100,0b00010), // (
    F5X7_G7(0b01000,0b00100,0b00010,0b00010,0b00010,0b00100,0b01000), // )
    F5X7_G7(0b00000,0b00100,0b11111,0b00100,0b01010,0b00000,0b00000), // *
    F5X7_G7(0b00000,0b00100,0b00100,0b11111,0b00100,0b00100,0b00000), // +
    F5X7_G7(0b00000,0b00000,0b00000,0b00000,0b00100,0b00100,0b01000), // ,
    F5X7_G7(0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000), // -
    F5X7_G7(0b00000,0b00000,0b00000,0b00000,0b00000,0b00110,0b00110), // .
    F5X7_G7(0b00001,0b00010,0b00100,0b01000,0b10000,0b00000,0b00000)  // /
}};

// Additional singletons that were in switch-cases originally
inline constexpr Glyph GLYPH_COLON      = F5X7_G7(0b00000,0b00000,0b00110,0b00000,0b00110,0b00000,0b00000);
inline constexpr Glyph GLYPH_SEMICOLON  = F5X7_G7(0b00000,0b00000,0b00110,0b00000,0b00100,0b00100,0b01000);
inline constexpr Glyph GLYPH_LT         = F5X7_G7(0b00010,0b00100,0b01000,0b10000,0b01000,0b00100,0b00010);
inline constexpr Glyph GLYPH_EQ         = F5X7_G7(0b00000,0b11111,0b00000,0b11111,0b00000,0b00000,0b00000);
inline constexpr Glyph GLYPH_GT         = F5X7_G7(0b01000,0b00100,0b00010,0b00001,0b00010,0b00100,0b01000);
inline constexpr Glyph GLYPH_QMARK      = F5X7_G7(0b01110,0b10001,0b00010,0b00100,0b00100,0b00000,0b00100);
inline constexpr Glyph GLYPH_UNDERSCORE = F5X7_G7(0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b11111);
inline constexpr Glyph GLYPH_LBRACKET   = F5X7_G7(0b01110,0b01000,0b01000,0b01000,0b01000,0b01000,0b01110);
inline constexpr Glyph GLYPH_RBRACKET   = F5X7_G7(0b01110,0b00010,0b00010,0b00010,0b00010,0b00010,0b01110);

// =========================================================
// Public: original compatibility API
// =========================================================

// Return a pointer to 7 rows (uint8_t each). This preserves your original API.
inline const u8* getGlyph(char c);

// =========================================================
// Selection helpers (refs to constexpr glyphs)
// =========================================================
inline const Glyph& glyphRef(char c) {
    if (c == ' ') return GLYPH_SPACE;
    if (c >= '0' && c <= '9') return DIGITS[static_cast<size_t>(c - '0')];
    if (c >= 'A' && c <= 'Z') return UPPER[static_cast<size_t>(c - 'A')];
    if (c >= 'a' && c <= 'z') return UPPER[static_cast<size_t>(c - 'a')]; // uppercase fallback

    switch (c) {
        case '!': return PUNCT_33_47[0];
        case '"': return PUNCT_33_47[1];
        case '#': return PUNCT_33_47[2];
        case '$': return PUNCT_33_47[3];
        case '%': return PUNCT_33_47[4];
        case '&': return PUNCT_33_47[5];
        case '\'':return PUNCT_33_47[6];
        case '(': return PUNCT_33_47[7];
        case ')': return PUNCT_33_47[8];
        case '*': return PUNCT_33_47[9];
        case '+': return PUNCT_33_47[10];
        case ',': return PUNCT_33_47[11];
        case '-': return PUNCT_33_47[12];
        case '.': return PUNCT_33_47[13];
        case '/': return PUNCT_33_47[14];
        case ':': return GLYPH_COLON;
        case ';': return GLYPH_SEMICOLON;
        case '<': return GLYPH_LT;
        case '=': return GLYPH_EQ;
        case '>': return GLYPH_GT;
        case '?': return GLYPH_QMARK;
        case '_': return GLYPH_UNDERSCORE;
        case '[': return GLYPH_LBRACKET;
        case ']': return GLYPH_RBRACKET;
        default:  return GLYPH_UNKNOWN;
    }
}

inline const u8* getGlyph(char c) { return glyphRef(c).rows.data(); }

// =========================================================
/*                 NEW: Text & Rendering API               */
// =========================================================

struct RenderOptions {
    // Geometry
    int scaleX = 1;
    int scaleY = 1;
    bool proportional = true;  // use trimmed visual width
    int letterSpacing = 1;     // extra pixels between glyphs
    int lineSpacing   = 2;     // extra pixels between lines
    int spaceAdvance  = 3;     // width of ' ' in pixels before scaling

    // Alignment (used by drawTextBox)
    enum AlignH { Left, Center, Right } alignH = Left;

    // Tabs and wrapping
    int tabSize = 4;      // number of spaces for '\t'
    int wrapHardLimit = 0; // 0 = no hard split; >0 will break words longer than this many glyph cols

    // Styles
    bool underline = false;
    bool strike    = false;
    bool bold      = false; // draws an extra column to the right
    int  italic    = 0;     // 0..2: shear amount (top rows shift right)

    // Effects
    int shadowDx = 0, shadowDy = 0; // if non-zero, draw a shadow first
    u8  shadowAlpha = 128;          // alpha for shadow pixels
    int outline = 0;                // 0=no outline, 1=1px diamond around "on" pixels
    u8  outlineAlpha = 128;         // alpha for outline pixels
};

// Per-line measured size
struct Size { int w = 0; int h = 0; };

// Visual advance for a glyph (unscaled)
inline int advanceOf(char c, const RenderOptions& opt) {
    if (c == ' ') return opt.spaceAdvance;
    if (c == '\t') return opt.spaceAdvance * opt.tabSize;
    const auto& g = glyphRef(c);
    const int w = opt.proportional ? glyphWidth(g) : GLYPH_W;
    return w + opt.letterSpacing;
}

// Measure multi-line text (no wrapping). Scaling is applied.
inline Size measure(std::string_view text, const RenderOptions& opt) {
    int maxW = 0, lineW = 0, lines = 1;
    for (char c : text) {
        if (c == '\n') {
            maxW = std::max(maxW, std::max(0, lineW - opt.letterSpacing));
            lineW = 0;
            ++lines;
            continue;
        }
        lineW += advanceOf(c, opt);
    }
    maxW = std::max(maxW, std::max(0, lineW - opt.letterSpacing));
    const int lineH = GLYPH_H + opt.lineSpacing;
    Size s;
    s.w = maxW * opt.scaleX;
    s.h = (lines * GLYPH_H + (lines - 1) * opt.lineSpacing) * opt.scaleY;
    return s;
}

// Word-wrap into lines that fit boxWidth (in **screen** pixels).
inline std::vector<std::string> wrap(std::string_view text, int boxWidth, const RenderOptions& opt) {
    std::vector<std::string> out;
    if (boxWidth <= 0) { out.emplace_back(text); return out; }

    const int maxCols = std::max(1, boxWidth / opt.scaleX); // in unscaled pixels
    auto flushLine = [&](std::string& acc){ out.emplace_back(acc); acc.clear(); };

    std::string acc;
    acc.reserve(text.size());
    int colW = 0; // unscaled

    auto emitWord = [&](std::string_view w) {
        // Hard-break overly long words if requested
        if (opt.wrapHardLimit > 0 && static_cast<int>(w.size()) > opt.wrapHardLimit) {
            for (char c : w) {
                const int adv = advanceOf(c, opt);
                if (colW + adv - opt.letterSpacing > maxCols && !acc.empty()) { flushLine(acc); colW = 0; }
                acc.push_back(c); colW += adv;
            }
            return;
        }
        int wWidth = 0; for (char c : w) wWidth += advanceOf(c, opt);
        if (!acc.empty() && colW + opt.spaceAdvance + wWidth - opt.letterSpacing > maxCols) {
            flushLine(acc); colW = 0;
        }
        if (!acc.empty()) { acc.push_back(' '); colW += opt.spaceAdvance; }
        acc.append(w);
        colW += wWidth;
    };

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') { flushLine(acc); colW = 0; ++i; continue; }
        // collect a word
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\n' && text[j] != '\t') ++j;
        if (j > i) { emitWord(text.substr(i, j - i)); }
        // spaces / tabs
        while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) {
            const int adv = (text[j] == ' ') ? opt.spaceAdvance : opt.spaceAdvance * opt.tabSize;
            if (colW + adv > maxCols && !acc.empty()) { flushLine(acc); colW = 0; }
            if (text[j] == ' ') { acc.push_back(' '); }
            else                { acc.push_back('\t'); }
            colW += adv; ++j;
        }
        i = j;
    }
    if (!acc.empty() || out.empty()) flushLine(acc);
    return out;
}

// Low-level glyph blit with styling (callback-based)
template <class PutPixel>
inline void drawGlyph(int x, int y, const Glyph& g, PutPixel&& put, const RenderOptions& opt) {
    const int sx = opt.scaleX, sy = opt.scaleY;

    auto drawDot = [&](int px, int py, u8 a){ for (int yy=0; yy<sy; ++yy) for (int xx=0; xx<sx; ++xx) put(px+xx, py+yy, a); };

    // Shadow pass (if any)
    if (opt.shadowDx || opt.shadowDy) {
        for (int r=0; r<GLYPH_H; ++r) {
            const int shear = (opt.italic > 0) ? ((GLYPH_H-1 - r) * opt.italic) / (GLYPH_H-1) : 0;
            for (int c=0; c<GLYPH_W; ++c) if (bit(g, r, c)) {
                const int px = x + (c + shear) * sx + opt.shadowDx;
                const int py = y + r * sy + opt.shadowDy;
                drawDot(px, py, opt.shadowAlpha);
                if (opt.bold) drawDot(px + sx, py, opt.shadowAlpha);
            }
        }
    }

    // Outline pass (if any)
    if (opt.outline > 0) {
        for (int r=0; r<GLYPH_H; ++r) {
            const int shear = (opt.italic > 0) ? ((GLYPH_H-1 - r) * opt.italic) / (GLYPH_H-1) : 0;
            for (int c=0; c<GLYPH_W; ++c) if (bit(g, r, c)) {
                const int baseX = x + (c + shear) * sx;
                const int baseY = y + r * sy;
                // diamond neighborhood (Chebyshev radius 1 without center)
                drawDot(baseX- sx, baseY,     opt.outlineAlpha);
                drawDot(baseX+ sx, baseY,     opt.outlineAlpha);
                drawDot(baseX,     baseY- sy, opt.outlineAlpha);
                drawDot(baseX,     baseY+ sy, opt.outlineAlpha);
            }
        }
    }

    // Ink pass
    for (int r=0; r<GLYPH_H; ++r) {
        const int shear = (opt.italic > 0) ? ((GLYPH_H-1 - r) * opt.italic) / (GLYPH_H-1) : 0;
        for (int c=0; c<GLYPH_W; ++c) if (bit(g, r, c)) {
            const int px = x + (c + shear) * sx;
            const int py = y + r * sy;
            drawDot(px, py, 255);
            if (opt.bold) drawDot(px + sx, py, 255);
        }
    }
}

// Draw underline / strike lines (under the full glyph box)
template <class PutPixel>
inline void drawDecoration(int x, int y, int widthPx, PutPixel&& put, const RenderOptions& opt) {
    const int sx = opt.scaleX, sy = opt.scaleY;
    const int underlineY = y + GLYPH_H * sy;                 // 1px below baseline box
    const int strikeY    = y + (GLYPH_H/2) * sy;             // midline
    auto drawHLine = [&](int yy){
        for (int xx=0; xx<widthPx; ++xx) put(x + xx, yy, 255);
    };
    if (opt.underline) drawHLine(underlineY);
    if (opt.strike)    drawHLine(strikeY);
}

// Draw a single line (no wrapping), returns drawn width (screen px)
template <class PutPixel>
inline int drawLine(int x, int y, std::string_view line, PutPixel&& put, const RenderOptions& opt) {
    int cursorX = x;
    for (char c : line) {
        if (c == ' ') { cursorX += opt.spaceAdvance * opt.scaleX; continue; }
        if (c == '\t') { cursorX += opt.spaceAdvance * opt.tabSize * opt.scaleX; continue; }

        const auto& g = glyphRef(c);
        const int wCols = opt.proportional ? glyphWidth(g) : GLYPH_W;

        drawGlyph(cursorX, y, g, put, opt);
        cursorX += (wCols + opt.letterSpacing) * opt.scaleX;
    }
    // remove trailing spacing from visual width
    return std::max(0, cursorX - x - opt.letterSpacing * opt.scaleX);
}

// Public: draw multi-line text (no wrapping)
template <class PutPixel>
inline Size drawText(int x, int y, std::string_view text, PutPixel&& put, const RenderOptions& opt = {}) {
    int cursorY = y, maxW = 0, lines = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        const std::string_view line = (nl == std::string_view::npos) ? text.substr(start) : text.substr(start, nl - start);
        const int w = drawLine(x, cursorY, line, put, opt);
        if (opt.underline || opt.strike) drawDecoration(x, cursorY, w, put, opt);
        maxW = std::max(maxW, w);
        ++lines;
        if (nl == std::string_view::npos) break;
        start = nl + 1;
        cursorY += (GLYPH_H + opt.lineSpacing) * opt.scaleY;
    }
    Size s;
    s.w = maxW;
    s.h = lines * GLYPH_H * opt.scaleY + (lines - 1) * opt.lineSpacing * opt.scaleY;
    return s;
}

// Draw text inside a box width with word-wrap and alignment.
template <class PutPixel>
inline Size drawTextBox(int x, int y, int boxWidth, std::string_view text, PutPixel&& put, const RenderOptions& opt = {}) {
    auto lines = wrap(text, boxWidth, opt);
    int cursorY = y;
    int maxW = 0;
    for (const auto& line : lines) {
        const int lineW = measure(line, opt).w;
        int lineX = x;
        if (opt.alignH == RenderOptions::Center) lineX = x + (boxWidth - lineW) / 2;
        else if (opt.alignH == RenderOptions::Right) lineX = x + (boxWidth - lineW);

        const int w = drawLine(lineX, cursorY, line, put, opt);
        if (opt.underline || opt.strike) drawDecoration(lineX, cursorY, w, put, opt);
        maxW = std::max(maxW, w);
        cursorY += (GLYPH_H + opt.lineSpacing) * opt.scaleY;
    }
    Size s; s.w = maxW; s.h = (int)lines.size() * GLYPH_H * opt.scaleY + std::max(0, (int)lines.size()-1) * opt.lineSpacing * opt.scaleY;
    return s;
}

#undef F5X7_G7
} // namespace font5x7
