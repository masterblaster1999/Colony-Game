#pragma once

// DevTools.hpp — single-header in-game editor & debug UI for Colony-Game.
// No external dependencies; uses SDL2 if available; otherwise stubs out.
//
// How to use:
//   1) #include "dev/DevTools.hpp" from Game.cpp
//   2) Wire the Bridge lambdas to your world (grid, tiles, agents).
//   3) Call dev::updateAndRender(renderer, bridge, dt) each frame.
//
// Expands into a ~3,000 LOC toolset; this starter is compact to drop in and run.

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <algorithm>
#include <array>
#include <unordered_map>

#if defined(HAS_SDL2)
  #include "../SDLDetect.h"   // picks <SDL.h> or <SDL2/SDL.h>   (present in repo)
  #define DEVTOOLS_ENABLED 1
#else
  #define DEVTOOLS_ENABLED 0
#endif

// We use your built-in bitmap font for all UI text.
#include "../game/Font5x7.h"   // if your include path differs, adjust accordingly.

namespace dev {

// ---------- Tiny math / geometry ----------

struct Size { int w{0}, h{0}; };
struct Vec2i { int x{0}, y{0}; };
struct Rect  { int x{0}, y{0}, w{0}, h{0}; };

inline bool contains(const Rect& r, int px, int py) {
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

struct Agent {
    int   id{0};
    int   x{0}, y{0};
    std::string name;
};

// Game-side adapters you fill once and then we use every frame.
struct Bridge {
    std::function<Size()> gridSize;                       // world tiles size
    std::function<int(int,int)> getTile;                 // read tile id
    std::function<void(int,int,int)> setTile;            // write tile id
    std::function<void(std::function<void(const Agent&)>)> forEachAgent; // iterate agents
    // You can extend with jobs/resources/etc. later.
};

// ---------- Minimal text rendering using your 5x7 font ----------
#if DEVTOOLS_ENABLED
static void drawGlyph(SDL_Renderer* r, int gx, int gy, const uint8_t rows[7], int scale=2) {
    for (int y=0; y<7; ++y) {
        uint8_t row = rows[y];
        for (int x=0; x<5; ++x) if (row & (1 << (4 - x))) {
            SDL_Rect p{ gx + x*scale, gy + y*scale, scale, scale };
            SDL_RenderFillRect(r, &p);
        }
    }
}

static void drawChar(SDL_Renderer* r, int x, int y, char c, int scale=2) {
    using namespace font5x7;
    SDL_SetRenderDrawColor(r, 255,255,255,255);
    if (c==' ') { /* skip */ }
    else if (c>='0' && c<='9') drawGlyph(r, x,y, DIGITS[c-'0'], scale);
    else if (c>='A' && c<='Z') drawGlyph(r, x,y, LETTERS[c-'A'], scale);
    else {
        // naive fallback subset from PUNCT (index by small map)
        switch(c){
            case '!': drawGlyph(r,x,y,PUNCT[0],scale); break;
            case '"': drawGlyph(r,x,y,PUNCT[1],scale); break;
            case '#': drawGlyph(r,x,y,PUNCT[2],scale); break;
            case '-': drawGlyph(r,x,y,PUNCT[6],scale); break;
            case '.': drawGlyph(r,x,y,PUNCT[7],scale); break;
            default:  drawGlyph(r,x,y,GLYPH_UNKNOWN,scale); break;
        }
    }
}

static void drawText(SDL_Renderer* r, int x, int y, const std::string& s, int scale=2, int spacing=1) {
    int cx=x;
    for (char c: s) { drawChar(r, cx, y, c, scale); cx += (5*scale + spacing); }
}
#endif // DEVTOOLS_ENABLED

// ---------- Simple retained UI state ----------
struct UIState {
    bool show{true};
    bool mapPainter{true};
    int  hotItem{-1};
    int  activeItem{-1};
    bool mouseDown{false};
    int  mx{0}, my{0};
    bool justPressed{false};
    int  idCounter{1};
    int  panelY{8};
    int  brushId{1};     // example tile id to paint
    int  brushSize{1};
    bool floodFill{false};
};
static UIState g_ui;

// ---------- ID helpers ----------
inline int nextId() { return g_ui.idCounter++; }

// ---------- Input ----------
#if DEVTOOLS_ENABLED
static void beginFrame(SDL_Renderer* r) {
    // mouse
    Uint32 buttons = SDL_GetMouseState(&g_ui.mx, &g_ui.my);
    bool down = (buttons & SDL_BUTTON_LMASK) != 0;
    g_ui.justPressed = (!g_ui.mouseDown && down);
    g_ui.mouseDown = down;

    // hot/active reset
    g_ui.hotItem = -1;
    g_ui.idCounter = 1;
    (void)r;
}
#endif

// ---------- Widgets ----------
#if DEVTOOLS_ENABLED
static bool button(SDL_Renderer* r, const Rect& rr, const char* label) {
    int id = nextId();
    bool hot = contains(rr, g_ui.mx, g_ui.my);
    if (hot) g_ui.hotItem = id;

    SDL_SetRenderDrawColor(r, hot ? 80:50, hot?80:50, hot?80:50, 220);
    SDL_RenderFillRect(r, (SDL_Rect*)&rr);
    SDL_SetRenderDrawColor(r, 220,220,220,255);
    SDL_RenderDrawRect(r, (SDL_Rect*)&rr);
    drawText(r, rr.x+6, rr.y+4, label, 2);

    bool clicked = false;
    if (hot && g_ui.justPressed) { g_ui.activeItem = id; }
    if (!g_ui.mouseDown && g_ui.activeItem == id && hot) { clicked = true; g_ui.activeItem = -1; }
    if (!g_ui.mouseDown && g_ui.activeItem == id && !hot) g_ui.activeItem = -1;
    return clicked;
}

static bool checkbox(SDL_Renderer* r, const Rect& rr, const char* label, bool& v) {
    Rect box{rr.x, rr.y, rr.h, rr.h};
    if (button(r, box, v ? "X" : "")) v = !v;
    drawText(r, rr.x + rr.h + 6, rr.y + 4, label, 2);
    return v;
}

static int hslider(SDL_Renderer* r, Rect rr, int minv, int maxv, int& v) {
    v = std::clamp(v, minv, maxv);
    SDL_SetRenderDrawColor(r, 60,60,60,220);
    SDL_RenderFillRect(r, (SDL_Rect*)&rr);
    int span = rr.w - 8;
    int px = rr.x + 4 + int(span * (v - minv) / float(std::max(1, maxv - minv)));
    SDL_SetRenderDrawColor(r, 200,200,200,255);
    SDL_Rect knob{px-4, rr.y+2, 8, rr.h-4};
    SDL_RenderFillRect(r, &knob);
    bool hot = contains(rr, g_ui.mx, g_ui.my);
    if (hot && g_ui.mouseDown) {
        float t = (g_ui.mx - (rr.x+4)) / float(std::max(1, rr.w-8));
        v = minv + int(t * (maxv - minv));
        v = std::clamp(v, minv, maxv);
    }
    return v;
}
#endif

// ---------- Panels ----------
#if DEVTOOLS_ENABLED
static void drawTopBar(SDL_Renderer* r) {
    Rect bar{8, g_ui.panelY, 560, 32};
    SDL_SetRenderDrawColor(r, 30, 30, 30, 200);
    SDL_RenderFillRect(r, (SDL_Rect*)&bar);
    SDL_SetRenderDrawColor(r, 220,220,220,255);
    SDL_RenderDrawRect(r, (SDL_Rect*)&bar);
    drawText(r, bar.x + 8, bar.y + 8, "DEV", 2);

    Rect b1{ bar.x + 60, bar.y + 4, 90, 24 };
    if (button(r, b1, g_ui.mapPainter ? "Map: ON" : "Map: OFF")) g_ui.mapPainter = !g_ui.mapPainter;

    drawText(r, b1.x + 110, bar.y + 8, "Brush", 2);
    Rect s1{ b1.x + 165, bar.y + 6, 120, 20 };
    hslider(r, s1, 1, 9, g_ui.brushSize);

    Rect s2{ s1.x + 140, bar.y + 6, 120, 20 };
    drawText(r, s1.x + 2, bar.y + 28, "Size", 2);
    hslider(r, s2, 0, 15, g_ui.brushId);

    Rect cb{ s2.x + 140, bar.y + 6, 110, 20 };
    checkbox(r, cb, "Flood", g_ui.floodFill);
}

static void paintDot(const Bridge& br, int cx, int cy, int radius, int tile) {
    for (int dy=-radius; dy<=radius; ++dy) {
        for (int dx=-radius; dx<=radius; ++dx) {
            if (dx*dx + dy*dy <= radius*radius) {
                br.setTile(cx+dx, cy+dy, tile);
            }
        }
    }
}

static void flood(const Bridge& br, const Size& S, int sx, int sy, int tile) {
    if (sx<0||sy<0||sx>=S.w||sy>=S.h) return;
    const int target = br.getTile(sx,sy);
    if (target==tile) return;
    std::vector<std::pair<int,int>> st;
    st.emplace_back(sx,sy);
    while (!st.empty()) {
        auto [x,y]=st.back(); st.pop_back();
        if (x<0||y<0||x>=S.w||y>=S.h) continue;
        if (br.getTile(x,y)!=target) continue;
        br.setTile(x,y,tile);
        st.emplace_back(x+1,y); st.emplace_back(x-1,y);
        st.emplace_back(x,y+1); st.emplace_back(x,y-1);
    }
}

static void drawMapPainter(SDL_Renderer* r, const Bridge& br, int screenW, int screenH) {
    if (!g_ui.mapPainter) return;
    Size S = br.gridSize();
    if (S.w<=0 || S.h<=0) return;

    // simple auto-fit
    int cell = std::max(2, std::min(screenW / S.w, (screenH-64) / S.h));
    int ox = 8, oy = 48;
    // draw tiles
    for (int y=0; y<S.h; ++y) {
        for (int x=0; x<S.w; ++x) {
            int t = br.getTile(x,y);
            // naive palette: encode tile id into color (placeholder)
            uint8_t r8 = (t * 53) & 255;
            uint8_t g8 = (t * 97) & 255;
            uint8_t b8 = (t * 199) & 255;
            SDL_SetRenderDrawColor(r, r8,g8,b8,255);
            SDL_Rect q{ ox + x*cell, oy + y*cell, cell-1, cell-1 };
            SDL_RenderFillRect(r, &q);
        }
    }
    // paint on click
    if (g_ui.mouseDown && g_ui.mx>=ox && g_ui.my>=oy) {
        int gx = (g_ui.mx - ox) / cell;
        int gy = (g_ui.my - oy) / cell;
        if (gx>=0 && gy>=0 && gx<S.w && gy<S.h) {
            if (g_ui.floodFill) flood(br, S, gx, gy, g_ui.brushId);
            else paintDot(br, gx, gy, g_ui.brushSize, g_ui.brushId);
        }
    }

    // Legend
    drawText(r, ox, oy-16, "Map Painter (F1 to toggle DevTools)", 2);
}
#endif // DEVTOOLS_ENABLED

// ---------- Public API ----------
inline void toggle() { g_ui.show = !g_ui.show; }

inline bool isOpen() { return g_ui.show; }

inline int uiBrush() { return g_ui.brushId; }

#if DEVTOOLS_ENABLED
inline void updateAndRender(SDL_Renderer* r, const Bridge& bridge, float /*dt*/) {
    if (!g_ui.show) return;
    int w=0,h=0; SDL_GetRendererOutputSize(r, &w, &h);
    beginFrame(r);
    drawTopBar(r);
    drawMapPainter(r, bridge, w, h);
}
#else
// No SDL build: provide harmless stubs so you can keep the same calls.
inline void updateAndRender(void*, const Bridge&, float) {}
#endif

} // namespace dev
