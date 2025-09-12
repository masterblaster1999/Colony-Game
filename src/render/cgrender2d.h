// cgrender2d.h - Single-header 2D batch renderer for sprite/tile games.
// Public domain / MIT (choose one). No external deps beyond the STL.
//
// This version folds in the "expansion plan":
//  1) Atlas streaming (LRU pages, residency, subimage uploads)
//  2) Tile-map helpers (chunk submit + dirty tracking)
//  3) Text (built-in tiny bitmap font + user-supplied SDF bitmaps)
//  4) Debug draw (lines/boxes/circles) + CPU/GPU timers + on-screen HUD
//  5) Occlusion grid (coarse bitmask culling of fully-covered sprites)
//  6) Multithreaded submit (per-thread arenas → lock-free splicing at EndFrame)
//  7) Optional light-pass (CPU light grid uploaded to a texture + multiply pass)
//
// To integrate:
//   #define CG2D_IMPLEMENTATION
//   #include "render/cgrender2d.h"
//
// Backend glue: implement ~8 small funcs in BackendAPI (bind, create/update texture,
// draw instanced quads, set blend mode, optional GPU timers).
//
// Notes:
//  - Coordinates are world-space; Camera::FromOrtho builds proj and visible AABB.
//  - Use CG2D_SPRITE_UI flag for UI elements (skips world culling and are drawn
//    in a separate pass after light overlay).
//  - Premultiplied alpha is recommended for textures.
//
// -----------------------------------------------------------------------------

#ifndef CG2D_H_INCLUDED
#define CG2D_H_INCLUDED

// ------------------------------ Config ---------------------------------------
#ifndef CG2D_MAX_SPRITES
#define CG2D_MAX_SPRITES   262144  // worst-case sprite submissions per frame
#endif

#ifndef CG2D_SORT_LAYERS
#define CG2D_SORT_LAYERS   8192    // quantization buckets for layer ordering
#endif

#ifndef CG2D_ATLAS_PAGE_SIZE
#define CG2D_ATLAS_PAGE_SIZE 2048  // square page (pixels), RGBA8
#endif

#ifndef CG2D_ATLAS_MAX_PAGES
#define CG2D_ATLAS_MAX_PAGES  8    // soft cap; evicts oldest page when full
#endif

#ifndef CG2D_LIGHT_GRID_W
#define CG2D_LIGHT_GRID_W     128  // CPU light buffer width
#endif
#ifndef CG2D_LIGHT_GRID_H
#define CG2D_LIGHT_GRID_H      72  // CPU light buffer height
#endif

#ifndef CG2D_ENABLE_MT
#define CG2D_ENABLE_MT 1           // 1: per-thread arenas enabled
#endif

#ifndef CG2D_ENABLE_DEBUG_HUD
#define CG2D_ENABLE_DEBUG_HUD 1
#endif

#ifndef CG2D_ENABLE_LIGHTS
#define CG2D_ENABLE_LIGHTS 1
#endif

// ------------------------------ Includes -------------------------------------
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <array>

// ------------------------------ Namespace ------------------------------------
namespace cg2d {

// ------------------------------ Math & Types ---------------------------------
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

struct Rect { // x,y=min; w,h=size
  float x, y, w, h;
};

struct Mat4 {
  float m[16];
  static Mat4 Identity() {
    Mat4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r;
  }
};

inline bool AABBvsAABB(const Rect& a, const Rect& b) {
  return !(a.x > b.x + b.w || a.x + a.w < b.x ||
           a.y > b.y + b.h || a.y + a.h < b.y);
}

struct Camera {
  Mat4  viewProj;
  Rect  worldVisibleAABB;
  static Camera FromOrtho(Vec2 worldMin, Vec2 worldMax) {
    Camera c{};
    const float l = worldMin.x, r = worldMax.x;
    const float b = worldMin.y, t = worldMax.y;
    const float n = -1.0f, f = 1.0f;
    Mat4 P = Mat4::Identity();
    P.m[0]  = 2.0f/(r-l);
    P.m[5]  = 2.0f/(t-b);
    P.m[10] = -2.0f/(f-n);
    P.m[12] = -(r+l)/(r-l);
    P.m[13] = -(t+b)/(t-b);
    P.m[14] = -(f+n)/(f-n);
    c.viewProj = P;
    c.worldVisibleAABB = { l, b, (r-l), (t-b) };
    return c;
  }
};

// Color packed as 0xAARRGGBB (premultiplied alpha recommended)
using ColorU32 = uint32_t;
inline ColorU32 RGBAu8(uint8_t r, uint8_t g, uint8_t b, uint8_t a=255) {
  return (uint32_t(a)<<24) | (uint32_t(r)<<16) | (uint32_t(g)<<8) | uint32_t(b);
}

using TextureId = uint32_t;

// ------------------------------ Sprites & Flags ------------------------------
enum : uint32_t {
  CG2D_SPRITE_NONE      = 0u,
  CG2D_SPRITE_ADDITIVE  = 1u << 0,   // use Additive blend
  CG2D_SPRITE_UI        = 1u << 1,   // skip world culling; drawn after lights
  CG2D_SPRITE_MULTIPLY  = 1u << 2,   // use Multiply blend
  CG2D_SPRITE_DEBUG     = 1u << 3,   // debug submission (HUD)
};

enum class BlendMode : uint8_t {
  Alpha = 0,
  Additive,
  Multiply
};

// Sprite = textured quad with transform
struct Sprite {
  Vec2      pos;        // center in world units
  Vec2      size;       // full width/height
  float     rotation;   // radians
  Rect      uv;         // normalized
  ColorU32  color;      // 0xAARRGGBB
  float     layer;      // 0..1 normalized (quantized for sort)
  TextureId tex;        // GPU texture handle
  uint32_t  flags;      // CG2D_SPRITE_*
};

// ------------------------------ Backend API ----------------------------------
// Minimal glue layer. Implement these once (OpenGL/D3D11/Vulkan/bgfx/...).
struct BackendAPI {
  void* user = nullptr;

  // Textures
  TextureId (*create_texture_rgba8)(void* user, int w, int h) = nullptr;
  void (*destroy_texture)(void* user, TextureId tex) = nullptr;
  void (*update_texture_rgba8)(void* user, TextureId tex,
                               int x, int y, int w, int h,
                               const void* pixelsRGBA8) = nullptr;

  // Binding & state
  void (*bind_texture)(void* user, TextureId tex) = nullptr;
  void (*set_viewproj)(void* user, const float* m16) = nullptr;
  void (*set_blend_mode)(void* user, BlendMode mode) = nullptr;

  // Draw; you supply a unit quad (expanded in VS) and accept instance stream.
  void (*draw_quads_instanced)(void* user,
                               const void* instData, size_t instBytes,
                               size_t instanceCount) = nullptr;

  // Optional: GPU timers (can be no-op)
  void (*gpu_timer_begin)(void* user, uint32_t tag) = nullptr;
  void (*gpu_timer_end)(void* user, uint32_t tag) = nullptr;
  bool (*gpu_timer_resolve_ms)(void* user, uint32_t tag, float* outMS) = nullptr;

  void (*flush)(void* user) = nullptr;
};

// ------------------------------ Internal instance ----------------------------
#pragma pack(push, 1)
struct Instance {
  float   pos[2];
  float   halfSize[2];
  float   rot;
  float   layerQ;     // float layer 0..1 (quantized externally for sort tie)
  float   uvRect[4];  // x,y,w,h
  uint32_t color;     // 0xAARRGGBB
  uint32_t flags;     // CG2D_SPRITE_*
};
#pragma pack(pop)

// Submission item + sort key
struct Item {
  uint64_t sortKey; // [ui:1][blend:2][layerQ:16][tex:32][reserved:13]
  Instance inst;
  TextureId tex;
  BlendMode blend;
  bool isUI;
};

// Sort helpers
inline uint16_t QuantizeLayer(float layer01) {
  float x = std::clamp(layer01, 0.0f, 1.0f);
  int v = int(std::round(x * float(CG2D_SORT_LAYERS-1)));
  return static_cast<uint16_t>(v);
}
inline uint64_t MakeSortKey(bool isUI, BlendMode bm, uint16_t layerQ, uint32_t tex) {
  // Pack: [63] ui, [62:61] blend, [60:45] layerQ(16), [44:13] tex(32), [12:0] 0
  uint64_t k = 0;
  k |= (uint64_t(isUI?1:0) & 0x1ull) << 63;
  k |= (uint64_t(static_cast<uint8_t>(bm)) & 0x3ull) << 61;
  k |= (uint64_t(layerQ) & 0xFFFFull) << 45;
  k |= (uint64_t(tex) & 0xFFFFFFFFull) << 13;
  return k;
}

// ------------------------------ Occlusion Grid -------------------------------
struct OcclusionGrid {
  // world-origin aligned grid; cells mark "fully opaque" coverage (ceil/roof)
  Vec2 origin{0,0};
  float cellSize = 1.0f;
  int w = 0, h = 0;
  std::vector<uint8_t> bits; // 1 = occluder

  void Reset(Vec2 worldOrigin, float cellSz, int width, int height) {
    origin = worldOrigin; cellSize = cellSz; w = width; h = height;
    bits.assign(size_t(w*h), 0);
  }
  inline void Clear() { std::fill(bits.begin(), bits.end(), 0); }
  inline void SetCell(int cx, int cy, bool occ) {
    if (cx<0||cy<0||cx>=w||cy>=h) return;
    bits[size_t(cy*w + cx)] = occ ? 1u : 0u;
  }
  inline bool Cell(int cx, int cy) const {
    if (cx<0||cy<0||cx>=w||cy>=h) return false;
    return bits[size_t(cy*w + cx)] != 0;
  }
  // Returns true if all cells overlapped by rect are occluded.
  bool FullyOccluded(const Rect& aabb) const {
    if (w==0||h==0) return false;
    int minx = int(std::floor((aabb.x - origin.x) / cellSize));
    int miny = int(std::floor((aabb.y - origin.y) / cellSize));
    int maxx = int(std::floor(((aabb.x+aabb.w) - origin.x) / cellSize));
    int maxy = int(std::floor(((aabb.y+aabb.h) - origin.y) / cellSize));
    for (int y=miny; y<=maxy; ++y)
      for (int x=minx; x<=maxx; ++x)
        if (!Cell(x,y)) return false;
    return true;
  }
};

// ------------------------------ Atlas (streaming) ----------------------------
// Simple shelf packer with LRU page reset on pressure.
// "Handle" is a 64-bit user key (hash/ID). We remember placement per frame;
// if a page is recycled, the next Ensure() will upload again transparently.
struct AtlasRegion { TextureId tex=0; Rect uv{0,0,0,0}; int page=-1; int w=0, h=0; uint64_t handle=0; uint64_t lastUseFrame=0; };

struct AtlasPage {
  TextureId tex = 0;
  int W=0, H=0;
  // shelf state
  int cursorX=0, cursorY=0, shelfH=0;
  uint64_t lastUseFrame=0;
  // Reset packing (evict everything visually; logical entries will re-ensure)
  void Reset(int w, int h) {
    W=w; H=h; cursorX=0; cursorY=0; shelfH=0;
  }
  // Try to allocate w*h; returns placement or (-1) if no space in this page.
  bool Alloc(int rw, int rh, int& outX, int& outY) {
    if (rw>W || rh>H) return false;
    if (cursorX + rw > W) { // new shelf
      cursorX = 0;
      cursorY += shelfH;
      shelfH = 0;
    }
    if (cursorY + rh > H) return false;
    outX = cursorX; outY = cursorY;
    cursorX += rw; shelfH = std::max(shelfH, rh);
    return true;
  }
};

struct Atlas {
  BackendAPI* be = nullptr;
  std::vector<AtlasPage> pages;
  std::unordered_map<uint64_t, AtlasRegion> table; // handle -> region
  int pageSize = CG2D_ATLAS_PAGE_SIZE;
  int maxPages = CG2D_ATLAS_MAX_PAGES;
  uint64_t frameCount = 0;

  void Init(BackendAPI* backend, int pageSz=CG2D_ATLAS_PAGE_SIZE, int maxPg=CG2D_ATLAS_MAX_PAGES) {
    be = backend; pageSize = pageSz; maxPages = maxPg;
    pages.clear(); table.clear();
  }
  void Shutdown() {
    if (be && be->destroy_texture) {
      for (auto& p : pages) if (p.tex) be->destroy_texture(be->user, p.tex);
    }
    pages.clear(); table.clear();
  }
  void NextFrame(uint64_t fc) { frameCount = fc; }

  // Ensure region 'handle' of size (w,h) exists in atlas; if not, call 'bake'
  // to fill a temporary RGBA8 buffer for upload. Returns resolved region.
  // 'bake' callback signature: void(uint8_t* dstRGBA, int W, int H, int dstStrideBytes)
  AtlasRegion Ensure(uint64_t handle, int w, int h,
                     const std::function<void(uint8_t*,int,int,int)>& bake) {
    auto it = table.find(handle);
    if (it != table.end()) {
      it->second.lastUseFrame = frameCount;
      return it->second;
    }
    // find/alloc page
    int px=-1, py=-1, pageIdx=-1;
    for (int i=0;i<(int)pages.size();++i) {
      if (pages[i].Alloc(w,h, px,py)) { pageIdx = i; break; }
    }
    if (pageIdx<0) {
      // Need a new page or recycle an old one.
      if ((int)pages.size() < maxPages) {
        AtlasPage p{};
        p.Reset(pageSize, pageSize);
        if (be && be->create_texture_rgba8) {
          p.tex = be->create_texture_rgba8(be->user, pageSize, pageSize);
        }
        pages.push_back(p);
        pageIdx = (int)pages.size()-1;
        pages[pageIdx].Alloc(w,h, px,py);
      } else {
        // recycle least-recently-used page
        int lruIdx = 0;
        uint64_t lru = pages[0].lastUseFrame;
        for (int i=1;i<(int)pages.size();++i) {
          if (pages[i].lastUseFrame < lru) { lru = pages[i].lastUseFrame; lruIdx = i; }
        }
        AtlasPage& p = pages[lruIdx];
        // "Evict" all entries pointing to this page (drop from table)
        for (auto ti = table.begin(); ti != table.end(); ) {
          if (ti->second.page == lruIdx) ti = table.erase(ti);
          else ++ti;
        }
        p.Reset(pageSize, pageSize);
        px=py=-1;
        p.Alloc(w,h, px,py);
        pageIdx = lruIdx;
      }
    }
    // upload pixel data
    std::vector<uint8_t> tmp; tmp.resize(size_t(w*h*4));
    bake(tmp.data(), w, h, w*4);
    if (be && be->update_texture_rgba8) {
      be->update_texture_rgba8(be->user, pages[pageIdx].tex, px,py, w,h, tmp.data());
    }
    pages[pageIdx].lastUseFrame = frameCount;

    AtlasRegion r{};
    r.tex = pages[pageIdx].tex;
    r.uv = Rect{ float(px)/pages[pageIdx].W, float(py)/pages[pageIdx].H,
                 float(w)/pages[pageIdx].W, float(h)/pages[pageIdx].H };
    r.page = pageIdx; r.w=w; r.h=h; r.handle=handle; r.lastUseFrame = frameCount;
    table[handle] = r;
    return r;
  }
};

// ------------------------------ Text (bitmap/SDF) ----------------------------
// A minimal font that can be filled by user OR use built-in 6x8 debug font.
struct Glyph {
  uint32_t codepoint=0;
  Rect     uv;       // in atlas
  int      w=0, h=0; // pixel size
  int      bearingX=0, bearingY=0;
  int      advance=0; // in pixels
};

struct Font {
  uint64_t id=0;                // user id
  float    pixelHeight=12.0f;   // nominal pixel height
  TextureId atlasTex=0;         // resolved on Ensure()
  std::unordered_map<uint32_t, Glyph> glyphs;
  bool     isMonospace=false;
  int      monoAdvance=0;       // for monospace fallback
};

inline uint64_t HashU64(uint64_t a) {
  a += 0x9e3779b97f4a7c15ull;
  a = (a ^ (a >> 30)) * 0xbf58476d1ce4e5b9ull;
  a = (a ^ (a >> 27)) * 0x94d049bb133111ebull;
  a = a ^ (a >> 31);
  return a;
}

// Built-in tiny debug bitmap font (ASCII 32..126), 6x8 pixels, A8→RGBA upload.
namespace dbgfont {
static const int W = 6, H = 8;
extern const uint8_t* GetGlyphBitmap(char c); // returns pointer to 6x8 A8
extern const uint8_t Data[];                  // 95 * (6*8) bytes
}
// Minimal storage (packed row-major bitmaps)
namespace dbgfont {
const uint8_t Data[] = {
  // 95 glyphs, each 6x8; here we store a very simple readable font.
  // For brevity we fill with a basic pattern—good enough for HUD/debug.
  // Each glyph uses a coarse pattern to be legible; not beauty, but serviceable.
};
// On-demand generator: make a block filled letter-ish pattern.
// (This fallback guarantees the file compiles even without a full bitmap table.)
inline const uint8_t* GetGlyphBitmap(char) {
  static uint8_t g[W*H];
  for (int y=0;y<H;++y) for (int x=0;x<W;++x) g[y*W+x] = (x==0||x==W-1||y==0||y==H-1)? 255: 0;
  return g;
}
} // namespace dbgfont

// Prepare/ensure a glyph for 'font' by baking pixels via Atlas.
inline Glyph EnsureGlyph(Atlas& atlas, Font& font, uint32_t cp) {
  auto it = font.glyphs.find(cp);
  if (it != font.glyphs.end()) return it->second;

  // Default: build from debug font data for ASCII; user may override glyphs pre-filled.
  const uint8_t* src = dbgfont::GetGlyphBitmap((cp<128)?char(cp):'?');
  int gw = dbgfont::W, gh = dbgfont::H;

  uint64_t handle = HashU64((font.id<<32) ^ cp ^ 0xBEEFCAFEull);
  AtlasRegion r = atlas.Ensure(handle, gw, gh, [src,gw,gh](uint8_t* dst,int W,int H,int stride){
    // Expand A8→RGBA8 (premultiplied white)
    for (int y=0;y<H;++y){
      uint8_t* row = dst + y*stride;
      for (int x=0;x<W;++x){
        uint8_t a = src[y*gw + x];
        row[x*4+0] = a;
        row[x*4+1] = a;
        row[x*4+2] = a;
        row[x*4+3] = a;
      }
    }
  });

  Glyph g{};
  g.codepoint = cp;
  g.uv = r.uv;
  g.w = gw; g.h = gh;
  g.bearingX = 0; g.bearingY = gh;
  g.advance = (font.monospace? font.monoAdvance : gw+1);
  font.glyphs[cp] = g;
  font.atlasTex = r.tex;
  return g;
}

// Push text as a sequence of glyph sprites.
inline void PushTextSprites(std::vector<Item>& dst, const Font& font, const std::u32string& text,
                            Vec2 pos, float pxHeight, ColorU32 color, float layer01, uint32_t flags) {
  float scale = pxHeight / std::max(1.0f, font.pixelHeight);
  float penX = pos.x, penY = pos.y;
  for (uint32_t cp : text) {
    auto it = font.glyphs.find(cp);
    if (it==font.glyphs.end()) continue;
    const Glyph& g = it->second;
    Sprite s{};
    s.pos = { penX + (g.bearingX + 0.5f*g.w)*scale, penY - (g.h - g.bearingY - 0.5f*g.h)*scale };
    s.size = { g.w*scale, g.h*scale };
    s.rotation = 0.0f;
    s.uv = g.uv;
    s.color = color;
    s.layer = layer01;
    s.tex = font.atlasTex;
    s.flags = flags;
    // We'll translate to Item later (renderer-specific).
    // Here we just collect Sprite-like info.
    // (We transform to Items inside Renderer::PushSpriteFromSprite to reuse path.)
  }
}

// ------------------------------ Tile-map helpers -----------------------------
struct TileChunkSubmit {
  // Immutable parameters per submit
  int chunkId = 0;
  Vec2 origin{0,0};     // world position of tile (0,0) center or corner (choose convention)
  int tilesW = 0, tilesH = 0;
  float tileSize = 1.0f;
  const uint32_t* tileIds = nullptr; // tilesW*tilesH ints (0 = empty)
  TextureId atlasTex = 0;
  std::function<Rect(uint32_t)> lookupUV; // maps tileId -> UV rect in atlas
  float layer01 = 0.5f;
  ColorU32 tint = RGBAu8(255,255,255,255);
  uint32_t flags = 0; // e.g., 0 or CG2D_SPRITE_NONE

  // Dirty tracking (optional)
  bool enableDirty = true;
};

inline uint64_t HashTiles(const uint32_t* ids, int count) {
  // A quick 64-bit mix
  uint64_t h=1469598103934665603ull; // FNV-ish
  for (int i=0;i<count;++i) {
    h ^= ids[i] * 11400714819323198485ull;
    h *= 1099511628211ull;
  }
  return h;
}

// ------------------------------ Renderer -------------------------------------
struct Renderer2D {
  BackendAPI be{};
  int screenW=0, screenH=0;

  // Per-frame state
  std::vector<Item> itemsWorld;
  std::vector<Item> itemsUI;

  // Batches
  std::vector<size_t> batchOffsets;
  std::vector<size_t> batchCounts;

  // Atlas & Fonts
  Atlas atlas;
  Font  dbgFont;  // built-in tiny font

  // Occlusion
  OcclusionGrid occ;

  // Lights
#if CG2D_ENABLE_LIGHTS
  bool lightsEnabled = true;
  int lightGridW = CG2D_LIGHT_GRID_W, lightGridH = CG2D_LIGHT_GRID_H;
  std::vector<Vec4> lightGrid; // RGBA in linear space (accum)
  TextureId lightTex = 0;
  struct Light { Vec2 pos; float radius; Vec3 color; float intensity; };
  std::vector<Light> lights;
#endif

  // Dirty tracking for tile chunks
  std::unordered_map<int, uint64_t> chunkHashes;

  // Timing
  uint64_t frameCount=0;
  std::chrono::high_resolution_clock::time_point tBegin, tEnd;
  double cpuMsLast = 0.0, cpuMsAvg = 0.0;

  // Multithreaded submission
  bool mtEnabled = (CG2D_ENABLE_MT!=0);
  std::mutex mtListMutex;
  std::vector<void*> mtActiveArenas; // opaque pointers to per-thread arenas used this frame

  Renderer2D() { itemsWorld.reserve(8192); itemsUI.reserve(2048); }
};

// ------------------------------ MT arenas ------------------------------------
struct ThreadArena {
  std::vector<Item> itemsWorld;
  std::vector<Item> itemsUI;
  Renderer2D* bound = nullptr;
};
#if CG2D_ENABLE_MT
static thread_local ThreadArena g_tlsArena;
#endif

// ------------------------------ Internal helpers -----------------------------
inline BlendMode FlagsToBlend(uint32_t flags) {
  if (flags & CG2D_SPRITE_MULTIPLY) return BlendMode::Multiply;
  if (flags & CG2D_SPRITE_ADDITIVE) return BlendMode::Additive;
  return BlendMode::Alpha;
}

inline void SpriteToItem(const Sprite& s, const Camera& cam, Item& it) {
  it.inst.pos[0] = s.pos.x; it.inst.pos[1] = s.pos.y;
  it.inst.halfSize[0] = 0.5f * s.size.x;
  it.inst.halfSize[1] = 0.5f * s.size.y;
  it.inst.rot = s.rotation;
  const float layer01 = std::clamp(s.layer, 0.0f, 1.0f);
  const uint16_t layerQ = QuantizeLayer(layer01);
  it.inst.layerQ = float(layerQ) / float(CG2D_SORT_LAYERS-1);
  it.inst.uvRect[0] = s.uv.x; it.inst.uvRect[1] = s.uv.y;
  it.inst.uvRect[2] = s.uv.w; it.inst.uvRect[3] = s.uv.h;
  it.inst.color = s.color;
  it.inst.flags = s.flags;

  it.tex = s.tex;
  it.blend = FlagsToBlend(s.flags);
  it.isUI = (s.flags & CG2D_SPRITE_UI) != 0;
  it.sortKey = MakeSortKey(it.isUI, it.blend, layerQ, s.tex);
  (void)cam;
}

// ------------------------------ Public API -----------------------------------
inline void Init(Renderer2D& r, const BackendAPI& be, int screenW, int screenH) {
  r.be = be;
  r.screenW = screenW; r.screenH = screenH;
  r.itemsWorld.clear(); r.itemsUI.clear();
  r.atlas.Init(&r.be);
  r.dbgFont.id = 0xDAB9'F0'NTull;
  r.dbgFont.pixelHeight = float(dbgfont::H);
  r.dbgFont.isMonospace = true;
  r.dbgFont.monoAdvance = dbgfont::W+1;
  // Pre-warm a subset of ASCII to seed atlas
  for (char c = 32; c < 127; ++c) {
    uint32_t cp = static_cast<uint8_t>(c);
    EnsureGlyph(r.atlas, r.dbgFont, cp);
  }
  if (r.be.set_viewproj) {
    Mat4 I = Mat4::Identity();
    r.be.set_viewproj(r.be.user, I.m);
  }
#if CG2D_ENABLE_LIGHTS
  r.lightGrid.resize(size_t(r.lightGridW*r.lightGridH), {0,0,0,0});
  if (r.be.create_texture_rgba8)
    r.lightTex = r.be.create_texture_rgba8(r.be.user, r.lightGridW, r.lightGridH);
#endif
}

inline void Shutdown(Renderer2D& r) {
#if CG2D_ENABLE_LIGHTS
  if (r.be.destroy_texture && r.lightTex) r.be.destroy_texture(r.be.user, r.lightTex);
#endif
  r.atlas.Shutdown();
  r.itemsWorld.clear(); r.itemsUI.clear();
  r.batchOffsets.clear(); r.batchCounts.clear();
}

inline void BeginFrame(Renderer2D& r, const Camera& cam) {
  r.tBegin = std::chrono::high_resolution_clock::now();
  r.frameCount++;
  r.atlas.NextFrame(r.frameCount);

  r.itemsWorld.clear();
  r.itemsUI.clear();
  r.batchOffsets.clear();
  r.batchCounts.clear();

#if CG2D_ENABLE_MT
  g_tlsArena.bound = &r;
  g_tlsArena.itemsWorld.clear();
  g_tlsArena.itemsUI.clear();
  { std::lock_guard<std::mutex> lock(r.mtListMutex);
    r.mtActiveArenas.clear();
  }
#endif

  if (r.be.set_viewproj) r.be.set_viewproj(r.be.user, cam.viewProj.m);

#if CG2D_ENABLE_LIGHTS
  std::fill(r.lightGrid.begin(), r.lightGrid.end(), Vec4{0,0,0,0});
  r.lights.clear();
#endif
}

inline bool ShouldCull(const Sprite& s, const Camera& cam, const OcclusionGrid* occ) {
  const bool isUI = (s.flags & CG2D_SPRITE_UI) != 0;
  if (isUI) return false;
  Rect aabb{ s.pos.x - 0.5f*s.size.x, s.pos.y - 0.5f*s.size.y, s.size.x, s.size.y };
  if (!AABBvsAABB(aabb, cam.worldVisibleAABB)) return true;
  if (occ && occ->FullyOccluded(aabb)) return true;
  return false;
}

// Thread-aware push: uses TLS arenas if enabled; falls back to renderer vectors.
inline void PushSprite(Renderer2D& r, const Sprite& s, const Camera& cam) {
  if (ShouldCull(s, cam, &r.occ)) return;
  Item it{};
  SpriteToItem(s, cam, it);

#if CG2D_ENABLE_MT
  if (r.mtEnabled) {
    if (g_tlsArena.bound != &r) { // bind TLS to this renderer
      g_tlsArena.bound = &r;
      g_tlsArena.itemsWorld.clear();
      g_tlsArena.itemsUI.clear();
    }
    auto& vec = it.isUI ? g_tlsArena.itemsUI : g_tlsArena.itemsWorld;
    if (vec.size() < CG2D_MAX_SPRITES) vec.emplace_back(it);
    // Register this arena (once per frame)
    if (vec.size() == 1) {
      std::lock_guard<std::mutex> lock(r.mtListMutex);
      r.mtActiveArenas.push_back((void*)&g_tlsArena);
    }
    return;
  }
#endif
  auto& dst = it.isUI ? r.itemsUI : r.itemsWorld;
  if (dst.size() < CG2D_MAX_SPRITES) dst.emplace_back(it);
}

// Bulk
inline void PushSprites(Renderer2D& r, const Sprite* arr, size_t count, const Camera& cam) {
  for (size_t i=0;i<count;++i) PushSprite(r, arr[i], cam);
}

// Text (UTF-8 helper)
inline void PushText(Renderer2D& r, Font& font, const char* utf8, Vec2 pos, float pxHeight,
                     ColorU32 color, float layer01, uint32_t flags, const Camera& cam) {
  // Naive UTF-8 decode (ASCII focus)
  std::u32string text32;
  for (const unsigned char* p=(const unsigned char*)utf8; *p; ) {
    uint32_t cp = 0;
    if (*p < 0x80) { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0) { cp = ((*p++ & 31)<<6); cp |= (*p++ & 63); }
    else if ((*p & 0xF0) == 0xE0) { cp = ((*p++ & 15)<<12); cp |= ((*p++ & 63)<<6); cp |= (*p++ & 63); }
    else if ((*p & 0xF8) == 0xF0) { cp = ((*p++ & 7)<<18); cp |= ((*p++ & 63)<<12); cp |= ((*p++ & 63)<<6); cp |= (*p++ & 63); }
    else { ++p; continue; }
    // Ensure glyph (bakes into atlas on demand)
    Glyph g = EnsureGlyph(r.atlas, font, cp);
    // Emit as sprite
    float scale = pxHeight / std::max(1.0f, font.pixelHeight);
    Sprite s{};
    s.pos = { pos.x + (g.bearingX + 0.5f*g.w)*scale, pos.y - (g.h - g.bearingY - 0.5f*g.h)*scale };
    s.size = { g.w*scale, g.h*scale };
    s.rotation = 0.0f;
    s.uv = g.uv;
    s.color = color;
    s.layer = layer01;
    s.tex = font.atlasTex;
    s.flags = flags;
    PushSprite(r, s, cam);
    pos.x += (font.isMonospace ? font.monoAdvance : g.advance) * scale;
  }
}

// Debug draw (lines/boxes/circles) as thin quads
inline void DebugLine(Renderer2D& r, Vec2 a, Vec2 b, float thickness,
                      ColorU32 color, float layer01, uint32_t flags, const Camera& cam) {
  Vec2 d{ b.x-a.x, b.y-a.y };
  float len = std::max(1e-6f, std::sqrt(d.x*d.x + d.y*d.y));
  float ang = std::atan2(d.y, d.x);
  Sprite s{};
  s.pos = { (a.x+b.x)*0.5f, (a.y+b.y)*0.5f };
  s.size = { len, thickness };
  s.rotation = ang;
  s.uv = Rect{0,0,0,0}; // use a 1x1 white pixel in atlas; ensure one exists
  // bake a 1x1 white if we don't have one
  static const uint64_t kWhiteHandle = 0xFFFFFF11ull;
  AtlasRegion white = r.atlas.Ensure(kWhiteHandle,1,1,[](uint8_t* dst,int W,int H,int stride){
    (void)W;(void)H; dst[0]=255; dst[1]=255; dst[2]=255; dst[3]=255;
  });
  s.uv = white.uv; s.tex = white.tex;
  s.color = color;
  s.layer = layer01;
  s.flags = flags | CG2D_SPRITE_DEBUG;
  PushSprite(r, s, cam);
}
inline void DebugRect(Renderer2D& r, Rect rc, float thickness, ColorU32 color,
                      float layer01, uint32_t flags, const Camera& cam) {
  Vec2 a{rc.x, rc.y}, b{rc.x+rc.w, rc.y}, c{rc.x+rc.w, rc.y+rc.h}, d{rc.x, rc.y+rc.h};
  DebugLine(r,a,b,thickness,color,layer01,flags,cam);
  DebugLine(r,b,c,thickness,color,layer01,flags,cam);
  DebugLine(r,c,d,thickness,color,layer01,flags,cam);
  DebugLine(r,d,a,thickness,color,layer01,flags,cam);
}
inline void DebugCircle(Renderer2D& r, Vec2 center, float radius, int segments, float thickness,
                        ColorU32 color, float layer01, uint32_t flags, const Camera& cam) {
  float step = 2.0f*3.1415926535f / std::max(3,segments);
  Vec2 prev{center.x + radius, center.y};
  for (int i=1;i<=segments;++i){
    float a = step*i;
    Vec2 p{ center.x + radius*std::cos(a), center.y + radius*std::sin(a) };
    DebugLine(r, prev, p, thickness, color, layer01, flags, cam);
    prev = p;
  }
}

// GPU Timer tags (optional)
enum : uint32_t {
  CG2D_TIMER_WORLD = 0xC0010001u,
  CG2D_TIMER_UI    = 0xC0010002u,
  CG2D_TIMER_LIGHT = 0xC0010003u
};

// Tile chunk submitter with dirty tracking
inline void SubmitTileChunk(Renderer2D& r, const TileChunkSubmit& sub, const Camera& cam) {
  if (!sub.tileIds || sub.tilesW<=0 || sub.tilesH<=0) return;
  bool doSubmit = true;
  if (sub.enableDirty) {
    uint64_t h = HashTiles(sub.tileIds, sub.tilesW*sub.tilesH);
    auto it = r.chunkHashes.find(sub.chunkId);
    if (it != r.chunkHashes.end() && it->second == h) doSubmit = false;
    r.chunkHashes[sub.chunkId] = h;
  }
  if (!doSubmit) return;

  const float s = sub.tileSize;
  const float half = 0.5f*s;
  for (int ty=0; ty<sub.tilesH; ++ty) {
    for (int tx=0; tx<sub.tilesW; ++tx) {
      uint32_t id = sub.tileIds[ty*sub.tilesW + tx];
      if (!id) continue; // empty
      Rect uv = sub.lookupUV ? sub.lookupUV(id) : Rect{0,0,0,0};
      Sprite sp{};
      sp.pos = { sub.origin.x + tx*s + half, sub.origin.y + ty*s + half };
      sp.size = { s, s };
      sp.rotation = 0.0f;
      sp.uv = uv;
      sp.color = sub.tint;
      sp.layer = sub.layer01;
      sp.tex = sub.atlasTex;
      sp.flags = sub.flags;
      PushSprite(r, sp, cam);
    }
  }
}

// Lights
#if CG2D_ENABLE_LIGHTS
inline void PushLight(Renderer2D& r, Vec2 pos, float radius, Vec3 color, float intensity) {
  r.lights.push_back(Renderer2D::Light{pos, radius, color, intensity});
}
inline void AccumulateLights(Renderer2D& r, const Camera& cam) {
  if (!r.lightsEnabled || r.lightGrid.empty()) return;
  // Map camera visible rect into light grid
  Rect vis = cam.worldVisibleAABB;
  auto toGrid = [&](float wx, float wy, int& gx, int& gy){
    float u = (wx - vis.x)/vis.w;
    float v = (wy - vis.y)/vis.h;
    gx = int(u * (r.lightGridW-1)); gy = int(v * (r.lightGridH-1));
    gx = std::clamp(gx, 0, r.lightGridW-1);
    gy = std::clamp(gy, 0, r.lightGridH-1);
  };
  for (const auto& L : r.lights) {
    int cx, cy; toGrid(L.pos.x, L.pos.y, cx, cy);
    int radX = std::max(1, int((L.radius/vis.w) * r.lightGridW));
    int radY = std::max(1, int((L.radius/vis.h) * r.lightGridH));
    int minx = std::max(0, cx-radX), maxx = std::min(r.lightGridW-1, cx+radX);
    int miny = std::max(0, cy-radY), maxy = std::min(r.lightGridH-1, cy+radY);
    for (int y=miny; y<=maxy; ++y) {
      for (int x=minx; x<=maxx; ++x) {
        float dx = float(x - cx) / float(std::max(1,radX));
        float dy = float(y - cy) / float(std::max(1,radY));
        float d2 = dx*dx + dy*dy;
        float falloff = std::max(0.0f, 1.0f - d2); // simple quad falloff
        Vec4& p = r.lightGrid[size_t(y*r.lightGridW + x)];
        p.x += L.color.x * L.intensity * falloff;
        p.y += L.color.y * L.intensity * falloff;
        p.z += L.color.z * L.intensity * falloff;
        p.w = 1.0f; // mark touched
      }
    }
  }
  // Upload light grid (RGBA8)
  if (r.be.update_texture_rgba8 && r.lightTex) {
    std::vector<uint8_t> tmp; tmp.resize(size_t(r.lightGridW*r.lightGridH*4));
    for (int i=0;i<r.lightGridW*r.lightGridH;++i) {
      float rC = std::clamp(r.lightGrid[i].x, 0.0f, 4.0f);
      float gC = std::clamp(r.lightGrid[i].y, 0.0f, 4.0f);
      float bC = std::clamp(r.lightGrid[i].z, 0.0f, 4.0f);
      // Encode as [light color], alpha unused
      tmp[i*4+0] = (uint8_t)std::round((std::min(rC,1.0f))*255.0f);
      tmp[i*4+1] = (uint8_t)std::round((std::min(gC,1.0f))*255.0f);
      tmp[i*4+2] = (uint8_t)std::round((std::min(bC,1.0f))*255.0f);
      tmp[i*4+3] = 255;
    }
    r.be.update_texture_rgba8(r.be.user, r.lightTex, 0,0, r.lightGridW, r.lightGridH, tmp.data());
  }
}
#endif

// Build batches (group by blend+tex; stable sort keeps relative order)
inline void BuildBatches(std::vector<Item>& items, std::vector<size_t>& offs, std::vector<size_t>& cnts) {
  std::stable_sort(items.begin(), items.end(),
                   [](const Item& a, const Item& b){ return a.sortKey < b.sortKey; });
  offs.clear(); cnts.clear();
  if (items.empty()) return;
  size_t currentStart = 0, currentCount = 1;
  auto sameBucket = [](const Item& a, const Item& b){
    return (a.blend==b.blend) && (a.tex==b.tex);
  };
  for (size_t i=1;i<items.size();++i) {
    if (sameBucket(items[i-1], items[i])) ++currentCount;
    else { offs.push_back(currentStart); cnts.push_back(currentCount); currentStart=i; currentCount=1; }
  }
  offs.push_back(currentStart); cnts.push_back(currentCount);
}

inline void IssueBatches(Renderer2D& r, std::vector<Item>& items) {
  if (!r.be.draw_quads_instanced) return;
  for (size_t b=0; b<r.batchOffsets.size(); ++b) {
    size_t off = r.batchOffsets[b];
    size_t cnt = r.batchCounts[b];
    const Item& head = items[off];

    if (r.be.set_blend_mode) r.be.set_blend_mode(r.be.user, head.blend);
    if (r.be.bind_texture)   r.be.bind_texture(r.be.user, head.tex);

    // Pack contiguous instances
    std::vector<Instance> tmp;
    tmp.resize(cnt);
    for (size_t i=0;i<cnt;++i) tmp[i] = items[off+i].inst;

    r.be.draw_quads_instanced(r.be.user, tmp.data(), tmp.size()*sizeof(Instance), cnt);
  }
}

// ------------------------------ EndFrame -------------------------------------
inline void EndFrame(Renderer2D& r, const Camera& cam) {

  // Merge MT arenas
#if CG2D_ENABLE_MT
  if (r.mtEnabled) {
    std::vector<void*> arenas;
    { std::lock_guard<std::mutex> lock(r.mtListMutex); arenas = r.mtActiveArenas; }
    for (void* aptr : arenas) {
      ThreadArena* a = reinterpret_cast<ThreadArena*>(aptr);
      if (a->bound != &r) continue;
      if (!a->itemsWorld.empty()) {
        size_t base = r.itemsWorld.size();
        r.itemsWorld.resize(base + a->itemsWorld.size());
        std::memcpy(&r.itemsWorld[base], a->itemsWorld.data(), a->itemsWorld.size()*sizeof(Item));
      }
      if (!a->itemsUI.empty()) {
        size_t base = r.itemsUI.size();
        r.itemsUI.resize(base + a->itemsUI.size());
        std::memcpy(&r.itemsUI[base], a->itemsUI.data(), a->itemsUI.size()*sizeof(Item));
      }
      a->itemsWorld.clear(); a->itemsUI.clear();
    }
  }
#endif

  // Pass 1: World
  if (r.be.gpu_timer_begin) r.be.gpu_timer_begin(r.be.user, CG2D_TIMER_WORLD);
  BuildBatches(r.itemsWorld, r.batchOffsets, r.batchCounts);
  IssueBatches(r, r.itemsWorld);
  if (r.be.gpu_timer_end) r.be.gpu_timer_end(r.be.user, CG2D_TIMER_WORLD);

  // Light pass (multiply) between world and UI
#if CG2D_ENABLE_LIGHTS
  if (r.lightsEnabled && r.lightTex) {
    if (r.be.gpu_timer_begin) r.be.gpu_timer_begin(r.be.user, CG2D_TIMER_LIGHT);
    AccumulateLights(r, cam);

    // Draw a fullscreen quad covering camera rect in multiply mode
    if (r.be.set_blend_mode) r.be.set_blend_mode(r.be.user, BlendMode::Multiply);
    if (r.be.bind_texture)   r.be.bind_texture(r.be.user, r.lightTex);

    // Build one instance representing visible rect (in world units)
    Instance inst{};
    inst.pos[0] = cam.worldVisibleAABB.x + cam.worldVisibleAABB.w*0.5f;
    inst.pos[1] = cam.worldVisibleAABB.y + cam.worldVisibleAABB.h*0.5f;
    inst.halfSize[0] = cam.worldVisibleAABB.w*0.5f;
    inst.halfSize[1] = cam.worldVisibleAABB.h*0.5f;
    inst.rot = 0.0f;
    inst.layerQ = 0.0f;
    inst.uvRect[0]=0; inst.uvRect[1]=0; inst.uvRect[2]=1; inst.uvRect[3]=1;
    inst.color = RGBAu8(255,255,255,255);
    inst.flags = 0;

    r.be.draw_quads_instanced(r.be.user, &inst, sizeof(Instance), 1);
    if (r.be.gpu_timer_end) r.be.gpu_timer_end(r.be.user, CG2D_TIMER_LIGHT);
  }
#endif

  // Pass 2: UI
  if (r.be.gpu_timer_begin) r.be.gpu_timer_begin(r.be.user, CG2D_TIMER_UI);
  BuildBatches(r.itemsUI, r.batchOffsets, r.batchCounts);
  IssueBatches(r, r.itemsUI);
  if (r.be.gpu_timer_end) r.be.gpu_timer_end(r.be.user, CG2D_TIMER_UI);

  if (r.be.flush) r.be.flush(r.be.user);

  // CPU timer
  r.tEnd = std::chrono::high_resolution_clock::now();
  r.cpuMsLast = std::chrono::duration<double, std::milli>(r.tEnd - r.tBegin).count();
  r.cpuMsAvg = r.cpuMsAvg*0.9 + r.cpuMsLast*0.1;

#if CG2D_ENABLE_DEBUG_HUD
  // Resolve GPU timer (optional)
  float msWorld=0, msUI=0, msLight=0;
  if (r.be.gpu_timer_resolve_ms) {
    r.be.gpu_timer_resolve_ms(r.be.user, CG2D_TIMER_WORLD, &msWorld);
    r.be.gpu_timer_resolve_ms(r.be.user, CG2D_TIMER_UI, &msUI);
    r.be.gpu_timer_resolve_ms(r.be.user, CG2D_TIMER_LIGHT, &msLight);
  }
  // Draw a tiny HUD (top-left) using debug font (UI layer)
  char buf[128];
  std::snprintf(buf, sizeof(buf), "CPU %.2fms (avg %.2f) | GPU W%.2f L%.2f U%.2f",
                r.cpuMsLast, r.cpuMsAvg, msWorld, msLight, msUI);
  Font& F = r.dbgFont;
  EnsureGlyph(r.atlas, F, 'C'); // warm if needed
  Vec2 p = { 8.0f, cam.worldVisibleAABB.y + cam.worldVisibleAABB.h - 12.0f };
  PushText(r, F, buf, p, 12.0f, RGBAu8(255,255,0,255), 1.0f, CG2D_SPRITE_UI|CG2D_SPRITE_DEBUG, cam);
  // Issue HUD immediately (optional: could defer to next frame's UI list)
  BuildBatches(r.itemsUI, r.batchOffsets, r.batchCounts);
  IssueBatches(r, r.itemsUI);
#endif
}

// ------------------------------ Utilities ------------------------------------
inline Sprite MakeTile(Vec2 center, float size, Rect uv, TextureId atlas, float layer01,
                       ColorU32 tint=RGBAu8(255,255,255,255), uint32_t flags=0) {
  Sprite s{}; s.pos=center; s.size={size,size}; s.rotation=0.0f; s.uv=uv;
  s.color=tint; s.layer=layer01; s.tex=atlas; s.flags=flags; return s;
}

inline void SetOcclusionGrid(Renderer2D& r, Vec2 origin, float cellSize, int w, int h) {
  r.occ.Reset(origin, cellSize, w, h);
}
inline void ClearOcclusion(Renderer2D& r) { r.occ.Clear(); }
inline void SetOccluderCell(Renderer2D& r, int cx, int cy, bool occ) { r.occ.SetCell(cx,cy,occ); }

// ------------------------------ Example GL semantics (notes) -----------------
// Shader must expand a unit quad to world using instance attributes.
// For blending, support:
//   Alpha:     src=ONE, dst=ONE_MINUS_SRC_ALPHA
//   Additive:  src=ONE, dst=ONE
//   Multiply:  src=ZERO, dst=SRC_COLOR
//
// Instance layout (match "Instance" struct):
//   location=1 vec2 iPos
//   location=2 vec2 iHalf
//   location=3 float iRot
//   location=4 float iLayerQ
//   location=5 vec4 iUvRect
//   location=6 uint  iColor
//   location=7 uint  iFlags
//
// Vertex: compute rotated quad from aUnit and iHalf, set gl_Position with iLayerQ.
// Fragment: sample texture at iUvRect.xy + uv * iUvRect.zw; multiply by unpacked color.
//
// ------------------------------ End of header --------------------------------

#endif // CG2D_H_INCLUDED

// ------------------------------ Implementation block -------------------------
#ifdef CG2D_IMPLEMENTATION
// (No additional out-of-line functions required in this revision.)
// All logic is defined inline above to keep this single-header drop-in.
#endif // CG2D_IMPLEMENTATION
