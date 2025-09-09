// AtmosphereSim.cpp — single-file atmosphere & fluids sim (C++17, no deps).
// Place in: src/sim/AtmosphereSim.cpp
// Optional overlay: compile with -DATMOSPHERE_ENABLE_OVERLAY and link SDL2.

#include <vector>
#include <cstdint>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cassert>

#ifndef ATMOSPHERE_ENABLE_OVERLAY
// Forward declare SDL types so headers not required by default.
struct SDL_Renderer;
#else
#  include <SDL.h>
#endif

namespace sim {

// ---------- small helpers ----------
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return std::fmax(lo, std::fmin(hi, v)); }

struct IVec2 { int x = 0, y = 0; };
static inline int idx(int x, int y, int w) { return y * w + x; }

// ---------- public enums/flags ----------
enum CellFlags : uint16_t {
    Solid      = 1 << 0, // blocks gas
    Door       = 1 << 1, // door tile; "open" controls permeability
    DoorOpen   = 1 << 2, // if set with Door, it's open
    VentIn     = 1 << 3, // intake
    VentOut    = 1 << 4, // exhaust
    Leak       = 1 << 5, // explicit leak to outside
};

struct ColonistPresence {
    int x = 0, y = 0;
    float activity = 1.0f; // 0.0 idle, 2.0 sprint
};

struct Params {
    // Gas dynamics (tunable, arbitrary units)
    float mixCoef          = 0.20f; // dt-normalized local mixing coeff
    float doorPermeability = 0.25f; // when DoorOpen
    float leakPermeability = 0.5f;  // through Leak to exterior
    float sealedMixBoost   = 0.6f;  // extra uniform mixing inside a sealed room

    // Physiology (arbitrary units/tick)
    float o2ConsumptionBase = 0.0008f; // per colonist per tick (scaled by activity)
    float co2ReturnFactor   = 1.0f;

    // Stability/limits
    float maxFluxPerEdge    = 0.01f; // avoid oscillation
    int   roomRebuildEvery  = 60;    // frames
};

struct Cell {
    // Gas fractions (sum ~ 1.0 in “normalized” cells, but we simulate masses)
    float o2 = 0.21f;
    float co2 = 0.0004f;
    float n2 = 0.7896f;
    float smoke = 0.0f;

    float pressure = 1.0f;   // baseline = 1.0
    float tempC = 20.0f;     // expanded later (heating/cooling)
    float humidity = 0.3f;

    uint16_t flags = 0;
    uint16_t roomId = 0xffff; // 0xffff == unassigned/outside
};

class AtmosphereSim {
public:
    AtmosphereSim() = default;

    void Reset(int w, int h, uint64_t seed = 0) {
        width_ = std::max(1, w);
        height_ = std::max(1, h);
        grid_.assign(width_ * height_, Cell{});
        rng_.seed(seed ? seed : std::random_device{}());
        roomIdCounter_ = 0;
        roomIds_.assign(width_ * height_, 0xffff);
        frames_ = 0;
        // Make default outside = roomId=0xffff, interior will be discovered
        rebuildRooms();
    }

    // Map editing
    void SetSolid(int x, int y, bool wall) {
        if (!inBounds(x, y)) return;
        auto& c = grid_[idx(x, y, width_)];
        if (wall) c.flags |= Solid; else c.flags &= ~Solid;
        dirtyRooms_ = true;
    }
    void SetDoor(int x, int y, bool present, bool open) {
        if (!inBounds(x, y)) return;
        auto& c = grid_[idx(x, y, width_)];
        c.flags = present ? (uint16_t)(c.flags | Door) : (uint16_t)(c.flags & ~Door);
        if (present) {
            if (open) c.flags |= DoorOpen; else c.flags &= ~DoorOpen;
        }
        dirtyRooms_ = true;
    }
    void SetLeak(int x, int y, bool leak) {
        if (!inBounds(x, y)) return;
        auto& c = grid_[idx(x, y, width_)];
        if (leak) c.flags |= Leak; else c.flags &= ~Leak;
    }

    // Colonists for this tick
    void SetColonists(const std::vector<ColonistPresence>& people) { colonists_ = people; }

    // One simulation step; dt in “frames” (1.0 per frame), but you can pass partials
    void Step(float dt = 1.0f) {
        if (dirtyRooms_ || (frames_ % params_.roomRebuildEvery) == 0)
            rebuildRooms();

        // Physiology: consume O2, add CO2
        for (const auto& cst : colonists_) {
            if (!inBounds(cst.x, cst.y)) continue;
            auto& c = grid_[idx(cst.x, cst.y, width_)];
            float want = params_.o2ConsumptionBase * clampf(cst.activity, 0.1f, 3.0f) * dt;
            float got = std::min(c.o2 * c.pressure, want);
            if (got > 0) {
                float frac = got / std::max(1e-6f, c.pressure);
                c.o2 -= frac;
                c.co2 += frac * params_.co2ReturnFactor;
            }
        }

        // Gas mixing & pressure
        // 2-pass: compute fluxes, then apply
        const int W = width_, H = height_;
        flux_.assign(W * H * 4, 0.0f); // per cell, 4 neighbors: 0:+x,1:-x,2:+y,3:-y
        auto permeabilityAt = [&](int x, int y, int nx, int ny) -> float {
            // If either cell is solid with no open door → 0
            const auto& a = grid_[idx(x, y, W)];
            const auto& b = grid_[idx(nx, ny, W)];
            if ((a.flags & Solid) || (b.flags & Solid)) {
                // Door tiles can be Solid+Door; allow when open
                if ( ((a.flags & Door) && (a.flags & DoorOpen)) ||
                     ((b.flags & Door) && (b.flags & DoorOpen)) ) {
                    // ok
                } else {
                    return 0.0f;
                }
            }
            float base = 1.0f;
            if ((a.flags & Door) || (b.flags & Door)) base *= params_.doorPermeability;
            if ((a.flags & Leak) || (b.flags & Leak)) base *= params_.leakPermeability;
            return base;
        };

        // Compute fluxes based on pressure differences
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int id = idx(x, y, W);
                const auto& c = grid_[id];
                auto tryFlux = [&](int nx, int ny, int edgeIdx) {
                    if (!inBounds(nx, ny)) return;
                    const int nid = idx(nx, ny, W);
                    const auto& nb = grid_[nid];
                    float perm = permeabilityAt(x, y, nx, ny);
                    if (perm <= 0.0f) return;
                    float dp = (c.pressure - nb.pressure);
                    float f = clampf(dp * params_.mixCoef * perm, -params_.maxFluxPerEdge, params_.maxFluxPerEdge);
                    flux_[id*4 + edgeIdx] = f; // positive: outflow
                };
                tryFlux(x + 1, y, 0);
                tryFlux(x - 1, y, 1);
                tryFlux(x, y + 1, 2);
                tryFlux(x, y - 1, 3);
            }
        }

        // Apply fluxes to pressure and gases
        next_ = grid_;
        auto applyFlux = [&](int x, int y, int nx, int ny, int edgeIdx) {
            if (!inBounds(nx, ny)) return;
            const int id = idx(x, y, W);
            const int nid = idx(nx, ny, W);
            float f = flux_[id*4 + edgeIdx];
            if (f == 0.0f) return;
            // Move mass proportionally; split across gases by current fractions
            const auto& src = grid_[id];
            auto& dst = next_[id];
            auto& ngb = next_[nid];
            float mass = std::max(0.0f, f);
            if (mass > 0.0f) {
                // leaving id → nid if f>0; if f<0, we'll do the opposite when edge reversed
                float total = std::max(1e-6f, src.o2 + src.co2 + src.n2 + src.smoke);
                float o2m = (src.o2 / total) * mass;
                float c2m = (src.co2 / total) * mass;
                float n2m = (src.n2 / total) * mass;
                float smm = (src.smoke / total) * mass;

                dst.pressure -= mass;
                ngb.pressure += mass;

                dst.o2 = clampf(dst.o2 - o2m, 0.0f, 1e6f);
                ngb.o2 += o2m;
                dst.co2 = clampf(dst.co2 - c2m, 0.0f, 1e6f);
                ngb.co2 += c2m;
                dst.n2 = clampf(dst.n2 - n2m, 0.0f, 1e6f);
                ngb.n2 += n2m;
                dst.smoke = clampf(dst.smoke - smm, 0.0f, 1e6f);
                ngb.smoke += smm;
            }
        };

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                applyFlux(x, y, x + 1, y, 0);
                applyFlux(x, y, x - 1, y, 1);
                applyFlux(x, y, x, y + 1, 2);
                applyFlux(x, y, x, y - 1, 3);
            }
        }

        // Sealed-room mixing boost: average toward room mean quickly
        mixRooms(next_);

        grid_.swap(next_);
        ++frames_;
    }

    // --- Public utilities ---
    int Width() const  { return width_; }
    int Height() const { return height_; }
    const Cell& At(int x, int y) const { return grid_[idx(x,y,width_)]; }
    void SetParams(const Params& p) { params_ = p; }

    // Optional overlay: draw O2 heatmap
    void RenderOverlay(SDL_Renderer* r, int cellSize, int ox = 0, int oy = 0) {
        (void)r; (void)cellSize; (void)ox; (void)oy;
#ifdef ATMOSPHERE_ENABLE_OVERLAY
        // A simple per-rect fill using SDL (keep it super light)
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const auto& c = grid_[idx(x,y,width_)];
                // map O2 ratio to green channel
                float total = std::max(1e-6f, c.o2 + c.co2 + c.n2 + c.smoke);
                float o2ratio = clampf(c.o2/total, 0.0f, 0.35f) / 0.35f;
                uint8_t g = (uint8_t)std::round(255.0f * o2ratio);
                uint8_t r8 = (uint8_t)std::round(255.0f * (1.0f - o2ratio));
                SDL_Rect rc{ ox + x * cellSize, oy + y * cellSize, cellSize, cellSize };
                SDL_SetRenderDrawColor(r, r8, g, 0, 160);
                SDL_RenderFillRect(r, &rc);
            }
        }
#endif
    }

private:
    bool inBounds(int x, int y) const { return (unsigned)x < (unsigned)width_ && (unsigned)y < (unsigned)height_; }

    void rebuildRooms() {
        std::fill(roomIds_.begin(), roomIds_.end(), 0xffff);
        roomIdCounter_ = 0;
        // Treat the outer boundary as "outside" (0xffff)
        // Flood fill interior cells that are not Solid and not explicit Leak to make room ids.
        uint16_t nextId = 0;
        std::vector<IVec2> stack;
        auto permeable = [&](const Cell& c) {
            if (c.flags & Solid) return false;
            // closed door still counts as interior surface, but not permeable to outside
            return true;
        };

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                int id = idx(x, y, width_);
                if (roomIds_[id] != 0xffff) continue;
                const auto& c = grid_[id];
                if (!permeable(c)) continue;

                // If on edge or marked Leak → treat as outside connected; skip room assignment
                if (x==0 || y==0 || x==width_-1 || y==height_-1 || (c.flags & Leak)) {
                    roomIds_[id] = 0xffff; // outside
                    continue;
                }

                uint16_t thisRoom = nextId++;
                stack.clear();
                stack.push_back({x, y});
                roomIds_[id] = thisRoom;

                while (!stack.empty()) {
                    IVec2 p = stack.back(); stack.pop_back();
                    static const int dx[4]={1,-1,0,0}, dy[4]={0,0,1,-1};
                    for (int k=0;k<4;++k) {
                        int nx = p.x + dx[k], ny = p.y + dy[k];
                        if (!inBounds(nx, ny)) continue;
                        int nid = idx(nx, ny, width_);
                        if (roomIds_[nid] != 0xffff) continue;
                        const auto& nc = grid_[nid];
                        if (!permeable(nc)) continue;

                        // If either side leaks to outside, don't include in a sealed room
                        if (nx==0 || ny==0 || nx==width_-1 || ny==height_-1 || (nc.flags & Leak)) {
                            roomIds_[nid] = 0xffff; // outside
                            continue;
                        }

                        // Door closed still interior; mixing boost handles permeability later
                        roomIds_[nid] = thisRoom;
                        stack.push_back({nx, ny});
                    }
                }
            }
        }
        // write back room ids into cells for debugging
        for (int i=0;i<(int)grid_.size();++i) grid_[i].roomId = roomIds_[i];
        dirtyRooms_ = false;
    }

    void mixRooms(std::vector<Cell>& dst) {
        // For each room id, compute simple average and lerp toward it
        struct Acc { double o2=0, co2=0, n2=0, smoke=0, p=0; int count=0; };
        if (roomIdCounter_ == 0 && !roomIds_.empty()) {
            // recompute how many unique ids exist
            uint16_t maxId = 0;
            for (auto id : roomIds_) if (id != 0xffff) maxId = std::max(maxId, id);
            roomIdCounter_ = maxId + 1;
        }
        std::vector<Acc> accs(roomIdCounter_);
        for (int i=0;i<(int)dst.size();++i) {
            uint16_t r = roomIds_[i];
            if (r == 0xffff) continue;
            auto& a = accs[r];
            const auto& c = dst[i];
            a.o2 += c.o2; a.co2 += c.co2; a.n2 += c.n2; a.smoke += c.smoke; a.p += c.pressure; a.count++;
        }
        for (auto& a : accs) if (a.count>0) {
            a.o2/=a.count; a.co2/=a.count; a.n2/=a.count; a.smoke/=a.count; a.p/=a.count;
        }
        const float k = params_.sealedMixBoost;
        for (int i=0;i<(int)dst.size();++i) {
            uint16_t r = roomIds_[i];
            if (r == 0xffff) continue; // outside
            auto& c = dst[i];
            const auto& a = accs[r];
            c.o2    = c.o2    + (float)((a.o2    - c.o2   ) * k);
            c.co2   = c.co2   + (float)((a.co2   - c.co2  ) * k);
            c.n2    = c.n2    + (float)((a.n2    - c.n2   ) * k);
            c.smoke = c.smoke + (float)((a.smoke - c.smoke) * k);
            c.pressure = c.pressure + (float)((a.p - c.pressure) * k);
        }
    }

private:
    int width_ = 0, height_ = 0;
    std::vector<Cell> grid_, next_;
    std::vector<uint16_t> roomIds_;
    std::vector<float> flux_; // (width*height*4)
    uint16_t roomIdCounter_ = 0;
    bool dirtyRooms_ = true;
    Params params_;
    std::vector<ColonistPresence> colonists_;
    std::mt19937 rng_;
    uint64_t frames_ = 0;
};

} // namespace sim

// --------- minimal “C-style” linker-friendly API (optional) ---------
// Use this if you prefer calling without a header in other TUs.
extern "C" {
    void* Atmosphere_Create(int w, int h, uint64_t seed) {
        auto* a = new sim::AtmosphereSim();
        a->Reset(w, h, seed);
        return a;
    }
    void Atmosphere_Destroy(void* ptr) { delete reinterpret_cast<sim::AtmosphereSim*>(ptr); }
    void Atmosphere_Step(void* ptr, float dt) { reinterpret_cast<sim::AtmosphereSim*>(ptr)->Step(dt); }
    int  Atmosphere_Width(void* ptr)  { return reinterpret_cast<sim::AtmosphereSim*>(ptr)->Width(); }
    int  Atmosphere_Height(void* ptr) { return reinterpret_cast<sim::AtmosphereSim*>(ptr)->Height(); }
}
