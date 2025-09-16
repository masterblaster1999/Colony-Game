// Game.cpp — SDL version (defensive include & future-proofing)
//
// Notes (merged improvements):
// - Deterministic fixed-timestep loop now *bounds catch-up frames* to avoid hitches.
// - Pause-on-focus-loss so the simulation doesn’t “jump” when you alt-tab.
// - Pathfinding now uses a per-step node budget (time-sliced) to prevent spikes.
// - Colonists now track movement timing individually (no shared accumulator).

#include "Game.h"

// ---- Defensive SDL include ---------------------------------------------------
#ifndef __has_include
  #define __has_include(x) 0
#endif

#if __has_include(<SDL.h>)
  #include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
  #include <SDL2/SDL.h>
#else
  #error "SDL2 headers not found. Install SDL2 and ensure your include paths are set."
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <chrono>     // used by the main loop timing
#include <functional> // for Storyteller bindings
#include <cctype>     // tolower for resource mapping
#include <filesystem> // Windows save path helpers return std::filesystem::path

// Built-in tiny bitmap font (5x7) for HUD text (no SDL_ttf needed)
#include "Font5x7.h"

// --- Dev Tools (single-header) ---
#include "dev/DevTools.hpp"

// --- Pathfinding module wrapper ---
#include "ai/Pathfinding.hpp"

// --- Windows atomic save helpers (mini patch #3) ---
#include "io/AtomicFile.h"
#include "platform/win/WinPaths.h"

// --------------------- Storyteller forward declarations -----------------------
// (No header required; these match the implementation in Storyteller.cpp)
namespace cg {
  struct StorytellerBindings {
    // colony snapshot
    std::function<int()>                getColonistCount;
    std::function<int()>                getWealth;
    std::function<int()>                getHostileCount;
    std::function<int()>                getAverageMood;   // 0..100
    std::function<int()>                getDayIndex;      // day since start
    // actuators
    std::function<void(int)>            spawnRaid;        // strength points
    std::function<void(const char*,int)>grantResource;    // id, amount
    std::function<void(const std::string&)> toast;        // HUD toast/log
  };

  void Storyteller_Init(const StorytellerBindings& b, uint64_t seed);
  void Storyteller_Update(float dtSeconds);
  void Storyteller_Save(std::ostream& out);
  bool Storyteller_Load(std::istream& in);
}
// -----------------------------------------------------------------------------

// --------------------------------- Helpers -----------------------------------
namespace util {
template <typename T> static T clamp(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }
template <typename T> static T sign(T v) { return (v > 0) - (v < 0); }
static uint32_t packColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a=255) {
    return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | uint32_t(a);
}
static void setDrawColor(SDL_Renderer* r, Uint32 packed) {
    Uint8 R = (packed >> 24) & 0xFF;
    Uint8 G = (packed >> 16) & 0xFF;
    Uint8 B = (packed >> 8 ) & 0xFF;
    Uint8 A = (packed      ) & 0xFF;
    SDL_SetRenderDrawColor(r, R, G, B, A);
}
static std::string lower(std::string s){
    for (char& c : s) c = char(std::tolower(unsigned char(c)));
    return s;
}
} // namespace util

namespace constants {
    // Single source of truth for 2π
    constexpr double Tau = 6.283185307179586;
}

// ------------------------------ Basic Math Types ------------------------------
struct Vec2i {
    int x=0, y=0;
    bool operator==(const Vec2i& o) const { return x==o.x && y==o.y; }
    bool operator!=(const Vec2i& o) const { return !(*this == o);    }
    bool operator<(const Vec2i& o) const  { return y<o.y || (y==o.y && x<o.x); }
};
static inline Vec2i operator+(Vec2i a, Vec2i b) { return {a.x+b.x, a.y+b.y}; }
static inline Vec2i operator-(Vec2i a, Vec2i b) { return {a.x-b.x, a.y-b.y}; }

// Hash for Vec2i to use in unordered containers
namespace std {
template<> struct hash<Vec2i> {
    size_t operator()(const Vec2i& v) const noexcept {
        return (uint64_t(uint32_t(v.x)) << 32) ^ uint32_t(v.y);
    }
};
} // namespace std

// Shared 4-neighborhood directions (avoid duplication)
static constexpr std::array<Vec2i,4> kCardinal{{ Vec2i{1,0}, Vec2i{-1,0}, Vec2i{0,1}, Vec2i{0,-1} }};

// Distance helper (used by job selection)
static int manhattan(Vec2i a, Vec2i b) { return std::abs(a.x-b.x) + std::abs(a.y-b.y); }

// --------------------------------- RNG ---------------------------------------
class Rng {
public:
    explicit Rng(uint64_t seed) : rng_(seed) {}
    int  irange(int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(rng_); }
    bool chance(double p)       { std::bernoulli_distribution d(p); return d(rng_); }
    double frand(double a=0.0, double b=1.0) { std::uniform_real_distribution<double> d(a,b); return d(rng_); }
    void reseed(uint64_t seed) { rng_ = std::mt19937_64(seed); } // for load-game reseed
private:
    std::mt19937_64 rng_;
};

// --------------------------------- World -------------------------------------
enum class TileType : uint8_t {
    Regolith = 0, // walkable
    Rock     = 1, // slower path cost
    Ice      = 2, // resource
    Crater   = 3, // not walkable
    Sand     = 4, // walkable but meh
};

static const char* tileName(TileType t) {
    switch (t) {
        case TileType::Regolith: return "Regolith";
        case TileType::Rock:     return "Rock";
        case TileType::Ice:      return "Ice";
        case TileType::Crater:   return "Crater";
        case TileType::Sand:     return "Sand";
    }
    return "?";
}

struct Tile {
    TileType type = TileType::Regolith;
    int      resource = 0;      // for Ice / Rock pockets
    bool     walkable = true;
    uint8_t  cost     = 10;     // path cost base (regolith 10)
};

struct World {
    int W=120, H=80;
    std::vector<Tile> tiles;
    Rng* rng = nullptr;

    int idx(int x, int y) const { return y*W + x; }
    bool inBounds(int x, int y) const { return x>=0 && y>=0 && x<W && y<H; }
    Tile& at(int x, int y) { return tiles[idx(x,y)]; }
    const Tile& at(int x, int y) const { return tiles[idx(x,y)]; }

    void resize(int w, int h) {
        W = w; H = h; tiles.assign(W*H, {});
    }

    void generate(Rng& r) {
        rng = &r;
        // Base: regolith
        for (auto& t : tiles) { t.type=TileType::Regolith; t.resource=0; t.walkable=true; t.cost=10; }

        // Scatter sand strips (cosmetic)
        for (int y=0; y<H; ++y) {
            for (int x=0; x<W; ++x) {
                if (r.chance(0.02)) {
                    int len = r.irange(10, 40);
                    int dx = util::sign(r.irange(-1, 1));
                    int dy = util::sign(r.irange(-1, 1));
                    int cx=x, cy=y;
                    for (int i=0;i<len;++i){
                        if (!inBounds(cx,cy)) break;
                        auto& t = at(cx,cy);
                        t.type = TileType::Sand;
                        t.cost = 12;
                        cx += dx; cy += dy;
                    }
                }
            }
        }

        // Ice patches
        for (int k=0;k<200;++k) {
            int x = r.irange(0, W-1), y = r.irange(0, H-1);
            int radius = r.irange(2, 4);
            for (int dy=-radius; dy<=radius; ++dy) {
                for (int dx=-radius; dx<=radius; ++dx) {
                    int X=x+dx, Y=y+dy;
                    if (!inBounds(X,Y)) continue;
                    if (dx*dx + dy*dy <= radius*radius + r.irange(-2,2)) {
                        auto& t = at(X,Y);
                        t.type = TileType::Ice;
                        t.resource = r.irange(5,25); // chunks
                        t.walkable = true;
                        t.cost = 14;
                    }
                }
            }
        }

        // Rock clusters
        for (int k=0;k<250;++k) {
            int x = r.irange(0, W-1), y = r.irange(0, H-1);
            int radius = r.irange(2, 5);
            for (int dy=-radius; dy<=radius; ++dy) {
                for (int dx=-radius; dx<=radius; ++dx) {
                    int X=x+dx, Y=y+dy;
                    if (!inBounds(X,Y)) continue;
                    if (dx*dx + dy*dy <= radius*radius + r.irange(-2,2)) {
                        auto& t = at(X,Y);
                        t.type = TileType::Rock;
                        t.resource = r.irange(3,12);
                        t.walkable = true;
                        t.cost = 16;
                    }
                }
            }
        }

        // Craters (not walkable)
        for (int k=0;k<60;++k) {
            int x = r.irange(4, W-5), y = r.irange(4, H-5);
            int radius = r.irange(2, 4);
            for (int dy=-radius; dy<=radius; ++dy) {
                for (int dx=-radius; dx<=radius; ++dx) {
                    int X=x+dx, Y=y+dy;
                    if (!inBounds(X,Y)) continue;
                    if (dx*dx + dy*dy <= radius*radius + r.irange(-1,1)) {
                        auto& t = at(X,Y);
                        t.type = TileType::Crater;
                        t.walkable = false;
                        t.cost = 255;
                        t.resource = 0;
                    }
                }
            }
        }

        // Ensure a clear landing zone (HQ starting area)
        int cx=W/2, cy=H/2;
        for (int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
            int X=cx+dx, Y=cy+dy; if (!inBounds(X,Y)) continue;
            auto& t=at(X,Y); t.type=TileType::Regolith; t.walkable=true; t.cost=10; t.resource=0;
        }
    }
};

// --------------------------------- Pathfinding (module wrapper) ---------------
// Bounded per-call node budget to avoid frame spikes (time-sliced pathfinding).
static constexpr int kPathNodesPerStep = 2048;

[[nodiscard]] static bool findPathAStar(const World& w, Vec2i start, Vec2i goal, std::deque<Vec2i>& out) {
    if (!w.inBounds(start.x,start.y) || !w.inBounds(goal.x,goal.y)) return false;
    if (!w.at(start.x,start.y).walkable || !w.at(goal.x,goal.y).walkable) return false;

    cg::pf::GridView grid{
        w.W, w.H,
        [&](int x, int y) { return w.inBounds(x,y) && w.at(x,y).walkable; },
        [&](int x, int y) { return int(w.at(x,y).cost); }
    };

    std::vector<cg::pf::Point> path;
    const auto res = cg::pf::aStar(grid,
                                   {start.x, start.y},
                                   {goal.x,  goal.y},
                                   path,
                                   /*maxExpandedNodes*/ kPathNodesPerStep);
    if (res != cg::pf::Result::Found || path.empty()) return false;

    // Preserve previous behavior: skip the start tile (current position)
    out.clear();
    for (size_t i = 1; i < path.size(); ++i) {
        out.push_back(Vec2i{ path[i].x, path[i].y });
    }
    return true;
}

// ------------------------------ Economy / Colony -----------------------------
enum class Resource : uint8_t { Metal=0, Ice=1, Oxygen=2, Water=3 };
static const char* resName(Resource r){
    switch(r){
        case Resource::Metal:  return "Metal";
        case Resource::Ice:    return "Ice";
        case Resource::Oxygen: return "Oxygen";
        case Resource::Water:  return "Water";
    }
    return "?";
}

struct Stockpile {
    int metal  = 15; // start with a bit to place first buildings
    int ice    = 10;
    int oxygen = 50; // units of breathable O2
    int water  = 40;
};

enum class BuildingKind : uint8_t { Solar=0, Habitat=1, OxyGen=2 };

static const char* buildingName(BuildingKind k) {
    switch(k){
        case BuildingKind::Solar:  return "Solar Panel";
        case BuildingKind::Habitat:return "Habitat";
        case BuildingKind::OxyGen: return "Oxygen Generator";
    }
    return "?";
}

struct BuildingDef {
    BuildingKind kind;
    Vec2i size; // grid tiles
    int metalCost=0, iceCost=0;
    int powerProd=0, powerCons=0;
    int oxyProd=0,  oxyCons=0;
    int waterProd=0, waterCons=0;
    int housing=0; // capacity
    bool needsDaylight=false; // solar
};

static BuildingDef defSolar()  { return {BuildingKind::Solar,  {2,2},  6,0,   8,0, 0,0, 0,0, 0, true}; }
static BuildingDef defHab()    { return {BuildingKind::Habitat,{3,2}, 12,4,   0,2, 0,2, 0,2, 4, false}; }
static BuildingDef defOxyGen() { return {BuildingKind::OxyGen, {2,2}, 10,6,   2,0, 4,0, 0,0, 0, false}; }

struct Building {
    int id=0;
    BuildingDef def;
    Vec2i pos; // top-left tile
    bool powered=true; // after economy pass
};

struct Colony {
    Stockpile store;
    int powerBalance=0; // current tick net
    int oxygenBalance=0;
    int waterBalance=0;
    int housing=0;
    int population=0;
};

// -------------------------------- Colonists ----------------------------------
enum class JobType : uint8_t { None=0, MineRock=1, MineIce=2, Deliver=3, Build=4 };

struct Job {
    JobType type = JobType::None;
    Vec2i target{};
    int   ticks=0;
    int   amount=0; // how much gathered/delivered
    int   buildingId=0; // for build jobs
};

struct Colonist {
    int   id=0;
    Vec2i tile{0,0};
    Vec2i home{-1,-1};
    std::deque<Vec2i> path;
    Job   job;
    int   carryMetal=0;
    int   carryIce=0;

    // Life support (kept for future features)
    double oxygen=100.0;
    double water=100.0;
    double energy=100.0;

    // Per-colonist movement accumulator (prevents lockstep motion).
    double moveAcc = 0.0;

    enum class State : uint8_t { Idle, Moving, Working, Returning } state = State::Idle;
};

// ------------------------------ Hostiles (raiders) ----------------------------
struct Hostile {
    int   id=0;
    int   strength=10;
    Vec2i tile{0,0};
    std::deque<Vec2i> path;
};

// ------------------------------- Game Internals ------------------------------
namespace colors {
static const Uint32 hud_bg    = util::packColor(20,20,26,220);
static const Uint32 hud_fg    = util::packColor(230,230,240,255);
static const Uint32 hud_accent= util::packColor(255,128,64,255);

static const Uint32 regolith  = util::packColor(139,85,70);
static const Uint32 sand      = util::packColor(168,120,85);
static const Uint32 ice       = util::packColor(120,170,200);
static const Uint32 rock      = util::packColor(100,100,110);
static const Uint32 crater    = util::packColor(40,40,45);

static const Uint32 gridLine  = util::packColor(0,0,0,50);
static const Uint32 select    = util::packColor(255,220,50,200);
static const Uint32 path      = util::packColor(30,220,255,200);

static const Uint32 solar     = util::packColor(60,120,200);
static const Uint32 habitat   = util::packColor(200,160,80);
static const Uint32 oxyGen    = util::packColor(90,200,140);

static const Uint32 colonist  = util::packColor(240,90,70);
static const Uint32 hq        = util::packColor(200,80,120);

static const Uint32 hostile   = util::packColor(200,60,60);

static const Uint32 banner_bg = util::packColor(30,30,35,240);
static const Uint32 banner_fg = util::packColor(255,255,255,255);
}

// Camera (pixel-space)
struct Camera {
    double x=0, y=0;
    double zoom=1.0;
    int    viewportW=1280, viewportH=720;

    SDL_Rect tileRect(int tx, int ty, int tileSize) const {
        int px = int((tx*tileSize - x) * zoom);
        int py = int((ty*tileSize - y) * zoom);
        int s  = int(tileSize * zoom);
        return SDL_Rect{px,py,s,s};
    }

    Vec2i screenToTile(int sx, int sy, int tileSize) const {
        int wx = int(x + sx/zoom);
        int wy = int(y + sy/zoom);
        return Vec2i{ wx / tileSize, wy / tileSize };
    }
};

// --------------------------------- Game Impl ---------------------------------
class GameImpl {
public:
    GameImpl(SDL_Window* window, SDL_Renderer* renderer, const GameOptions& opts)
        : window_(window), renderer_(renderer), opts_(opts), rng_(opts.seed) {
        camera_.viewportW = opts.width;
        camera_.viewportH = opts.height;
    }

    int run() {
        init();
        // Fixed timestep simulation; vsync ON uses display rate, OFF uses time accumulator
        using clock = std::chrono::steady_clock;
        auto tPrev = clock::now();
        double simAcc = 0.0;
        const double dt = 1.0/60.0; // 60hz
        constexpr double MAX_FRAME_SEC = 0.25; // avoid giant jumps after stalls
        static constexpr int kMaxCatchUpFrames = 5; // bound catch-up to avoid hitches

        bool running = true;
        while (running) {
            // Events
            running = pumpEvents();

            // If we’re unfocused and we pause on focus loss, keep rendering but
            // drop accumulated time to avoid a giant catch-up when refocusing.
            if (pauseOnFocusLoss_ && !hasFocus_) {
                render();
                SDL_Delay(50);
                tPrev = clock::now();
                lastFrameSec_ = 1.0/60.0;
                continue;
            }

            // Timing
            const auto tNow = clock::now();
            const double frameSec = std::chrono::duration<double>(tNow - tPrev).count(); // seconds
            tPrev = tNow;
            lastFrameSec_ = frameSec;               // for devtools dt & banner fade
            fpsCounter(frameSec);

            if (paused_) {
                // Still render to show overlay & FPS
                render();
                continue;
            }

            // Clamp frame to avoid spirals of death on slow frames
            simAcc += std::min(frameSec, MAX_FRAME_SEC) * simSpeed_;
            // Clamp accumulator to avoid spiral-of-death if hitches occur
            simAcc = std::min(simAcc, 0.5);

            int steps = 0;
            while (simAcc >= dt && steps < kMaxCatchUpFrames) {
                update(dt);
                simAcc -= dt;
                ++steps;
            }
            render();
        }
        return 0;
    }

private:
    // ---------------------- Init / World / Entities --------------------------
    void init() {
        // SDL version sanity (runtime vs. headers)
        sdlVersionSanity_();

        tileSize_ = 24; // pixels per tile at zoom=1
        world_.resize(120, 80);
        world_.generate(rng_);

        // Spawn an HQ marker at center as delivery point (not a building)
        hq_ = { world_.W/2, world_.H/2 };

        // Initial buildings: 1 solar + 1 habitat + 1 oxy gen near HQ if affordable
        tryPlaceBuilding(BuildingKind::Solar,  hq_ + Vec2i{3,-2});
        tryPlaceBuilding(BuildingKind::Habitat,hq_ + Vec2i{3, 0});
        tryPlaceBuilding(BuildingKind::OxyGen, hq_ + Vec2i{0, 3});

        // Center camera on HQ
        camera_.x = (hq_.x * tileSize_) - camera_.viewportW/2;
        camera_.y = (hq_.y * tileSize_) - camera_.viewportH/2;

        // One colonist to start
        spawnColonist();

        // ---- DevTools bridge wiring ----
        setupDevToolsBridge_();

        // ---- Storyteller bindings ----
        cg::StorytellerBindings sb{};
        sb.getColonistCount = [this]{ return (int)colonists_.size(); };
        sb.getWealth        = [this]{ return colonyWealth_(); };
        sb.getHostileCount  = [this]{ return (int)hostiles_.size(); };
        sb.getAverageMood   = [this]{ return (int)std::clamp<int>(int(averageMood_*100.0),0,100); };
        sb.getDayIndex      = [this]{ return dayIndex_; };

        sb.spawnRaid        = [this](int pts){ SpawnRaidWithPoints_(pts); };
        sb.grantResource    = [this](const char* id,int amt){ GiveResource_(id, amt); };
        sb.toast            = [this](const std::string& s){ PushToast_(s); };

        cg::Storyteller_Init(sb, opts_.seed);
    }

    // ------------------------------ Event Pump --------------------------------
    bool pumpEvents() {
        auto isCtrl = [](const SDL_Event& e)->bool {
            const Uint16 m = e.key.keysym.mod;
            return (m & KMOD_CTRL) != 0;
        };

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    camera_.viewportW = e.window.data1;
                    camera_.viewportH = e.window.data2;
                } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    hasFocus_ = false;
                } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    hasFocus_ = true;
                }
            }
            if (e.type == SDL_KEYDOWN) {
                auto key = e.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    // If building mode active, cancel; else quit back to launcher exit
                    if (buildMode_) { buildMode_=false; selectedBuild_=std::nullopt; }
                    else return false;
                }
                else if (key == SDLK_F1) {      // <-- toggle DevTools overlay
                    dev::toggle();
                }
                else if (key == SDLK_p) { paused_ = !paused_; }
                else if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
                    simSpeed_ = util::clamp(simSpeed_ * 1.25, 0.25, 8.0);
                }
                else if (key == SDLK_MINUS || key == SDLK_UNDERSCORE || key == SDLK_KP_MINUS) {
                    simSpeed_ = util::clamp(simSpeed_ / 1.25, 0.25, 8.0);
                }
                else if (key == SDLK_1) { selectedBuild_ = BuildingKind::Solar;  buildMode_=true; }
                else if (key == SDLK_2) { selectedBuild_ = BuildingKind::Habitat;buildMode_=true; }
                else if (key == SDLK_3) { selectedBuild_ = BuildingKind::OxyGen; buildMode_=true; }
                else if (key == SDLK_b) {
                    // bulldoze: turn tile to regolith (debug)
                    Vec2i t = currentMouseTile();
                    if (world_.inBounds(t.x,t.y)) {
                        auto& tt = world_.at(t.x,t.y);
                        tt.type=TileType::Regolith; tt.walkable=true; tt.resource=0; tt.cost=10;
                    }
                }
                else if (key == SDLK_f) {
                    // Toggle flood debug overlay from mouse tile
                    floodDebug_ = !floodDebug_;
                    floodFrom_ = currentMouseTile();
                }
                else if (key == SDLK_g) {
                    spawnColonist();
                }
                else if (key == SDLK_r) {
                    if (!buildings_.empty()) buildings_.pop_back();
                }
                // --- Save / Load now use CTRL+S / CTRL+L to avoid conflict with WASD
                else if (key == SDLK_s && isCtrl(e)) {
                    saveGame();
                }
                else if (key == SDLK_l && isCtrl(e)) {
                    loadGame();
                }
                else if (key == SDLK_w || key == SDLK_UP)    { keyPan_.y = -1;  }
                else if (key == SDLK_s || key == SDLK_DOWN)  { keyPan_.y = +1;  }
                else if (key == SDLK_a || key == SDLK_LEFT)  { keyPan_.x = -1;  }
                else if (key == SDLK_d || key == SDLK_RIGHT) { keyPan_.x = +1;  }
                // Debug: force a small raid
                else if (key == SDLK_h) {
                    SpawnRaidWithPoints_(rng_.irange(20, 60));
                }
            }
            if (e.type == SDL_KEYUP) {
                auto key = e.key.keysym.sym;
                if (key == SDLK_w || key == SDLK_UP)    { if (keyPan_.y==-1) keyPan_.y = 0; }
                else if (key == SDLK_s || key == SDLK_DOWN) { if (keyPan_.y==+1) keyPan_.y = 0; }
                else if (key == SDLK_a || key == SDLK_LEFT) { if (keyPan_.x==-1) keyPan_.x = 0; }
                else if (key == SDLK_d || key == SDLK_RIGHT){ if (keyPan_.x==+1) keyPan_.x = 0; }
            }
            if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y > 0) camera_.zoom = util::clamp(camera_.zoom * 1.1, 0.5, 2.5);
                if (e.wheel.y < 0) camera_.zoom = util::clamp(camera_.zoom / 1.1, 0.5, 2.5);
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    onLeftClick();
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    buildMode_ = false; selectedBuild_.reset();
                }
            }
        }
        return true;
    }

    // ------------------------------ Update Tick -------------------------------
    void update(double dt) {
        // Camera pan
        const double panSpeed = 300.0; // px/sec
        camera_.x += keyPan_.x * panSpeed * dt;
        camera_.y += keyPan_.y * panSpeed * dt;

        // Clamp camera to world bounds (account for zoom)
        const double worldWpx = double(world_.W * tileSize_);
        const double worldHpx = double(world_.H * tileSize_);
        const double visWpx   = double(camera_.viewportW) / std::max(0.001, camera_.zoom);
        const double visHpx   = double(camera_.viewportH) / std::max(0.001, camera_.zoom);
        camera_.x = std::clamp(camera_.x, 0.0, std::max(0.0, worldWpx - visWpx));
        camera_.y = std::clamp(camera_.y, 0.0, std::max(0.0, worldHpx - visHpx));

        // Day-night
        dayTime_ += dt * 0.02; // ~50 sec per day by default
        if (dayTime_ >= 1.0) { dayTime_ -= 1.0; ++dayIndex_; }

        // Economy pass
        economyTick(dt);

        // AI / Jobs
        aiTick(dt);

        // Hostiles behavior
        hostileTick_(dt);

        // Toasts lifetimes
        toastTick_(dt);

        // Banner lifetime fade
        if (bannerTime_ > 0.0) {
            bannerTime_ -= dt;
            if (bannerTime_ <= 0.0) banner_.clear();
        }

        // Flood debug overlay compute (optional)
        if (floodDebug_) computeFloodFrom(floodFrom_);

        // Storyteller after sim advances
        cg::Storyteller_Update(float(dt));
    }

    // ------------------------------ Economy -----------------------------------
    void economyTick(double /*dt*/) {
        // Recompute capacities/balances every tick
        colony_.powerBalance = 0;
        colony_.oxygenBalance= 0;
        colony_.waterBalance = 0;
        colony_.housing = 0;
        for (auto& b : buildings_) {
            bool daylight = (dayTime_ > 0.1 && dayTime_ < 0.9);
            b.powered = true; // optimistic; we tally below
            if (b.def.needsDaylight && !daylight) {
                // No solar at night
                colony_.powerBalance += 0;
            } else {
                colony_.powerBalance += b.def.powerProd;
            }
            colony_.powerBalance -= b.def.powerCons;

            colony_.oxygenBalance += b.def.oxyProd;
            colony_.oxygenBalance -= b.def.oxyCons;

            colony_.waterBalance += b.def.waterProd;
            colony_.waterBalance -= b.def.waterCons;

            colony_.housing += b.def.housing;
        }

        // Update stores from balances every tick (simple model)
        colony_.store.oxygen = std::max(0, colony_.store.oxygen + colony_.oxygenBalance);
        colony_.store.water  = std::max(0, colony_.store.water  + colony_.waterBalance);

        // Colonists consume oxygen and water
        int people = int(colonists_.size());
        if (people > 0) {
            int oxyUse = people * 1; // 1 per tick (abstract)
            int waterUse = people * 1;
            colony_.store.oxygen = std::max(0, colony_.store.oxygen - oxyUse);
            colony_.store.water  = std::max(0, colony_.store.water  - waterUse);
        }
        colony_.population = people;

        // --- Mood estimation (0..1) ---
        double m = 0.7;
        if (colony_.store.oxygen < 30) m -= 0.20;
        if (colony_.store.water  < 30) m -= 0.20;
        if (colony_.powerBalance < 0)  m -= 0.10;
        if (colony_.population > colony_.housing) {
            m -= 0.05 * double(colony_.population - colony_.housing);
        }
        // Small circadian buff midday
        double daylight = std::cos((dayTime_ - 0.5) * constants::Tau)*0.5 + 0.5; // 0..1
        m += (daylight - 0.5) * 0.04;

        m = std::clamp(m, 0.05, 0.95);
        // Smooth
        averageMood_ = averageMood_ * 0.95 + m * 0.05;
    }

    // ------------------------------ AI / Jobs ---------------------------------
    void aiTick(double dt) {
        for (auto& c : colonists_) {
            switch (c.state) {
                case Colonist::State::Idle:    aiIdle(c); break;
                case Colonist::State::Moving:  aiMove(c, dt); break;
                case Colonist::State::Working: aiWork(c, dt); break;
                case Colonist::State::Returning: aiReturn(c, dt); break;
            }
        }
    }

    void aiIdle(Colonist& c) {
        // Order of preference: Build pending -> Mine Ice if oxygen low -> Mine Rock
        if (pendingBuild_.has_value()) {
            // Path to build site
            Vec2i target = pendingBuild_->pos;
            // Choose any tile adjacent to building footprint that's walkable
            std::vector<Vec2i> options;
            for (int dy=0; dy<pendingBuild_->def.size.y; ++dy)
                for (int dx=0; dx<pendingBuild_->def.size.x; ++dx) {
                    Vec2i p = pendingBuild_->pos + Vec2i{dx,dy};
                    for (auto d : kCardinal) {
                        Vec2i n = p + d;
                        if (world_.inBounds(n.x,n.y) && world_.at(n.x,n.y).walkable) options.push_back(n);
                    }
                }
            if (!options.empty()) {
                Vec2i pick = options[rng_.irange(0, int(options.size())-1)];
                std::deque<Vec2i> path;
                if (findPathAStar(world_, c.tile, pick, path)) {
                    c.path = std::move(path);
                    c.state = Colonist::State::Moving;
                    c.job = Job{JobType::Build, target, 0, 0, pendingBuild_->id};
                    return;
                }
            }
        }

        // If oxygen store low, prioritize ice mining
        if (colony_.store.oxygen < 40) {
            if (tryAssignMining(c, TileType::Ice)) return;
        }

        // Otherwise rock for metal
        if (tryAssignMining(c, TileType::Rock)) return;

        // Nothing to do, hang near HQ
        if (c.tile != hq_) {
            std::deque<Vec2i> path;
            if (findPathAStar(world_, c.tile, hq_, path)) {
                c.path = std::move(path);
                c.state = Colonist::State::Moving;
                c.job = Job{JobType::Deliver, hq_, 0, 0, 0};
                return;
            }
        }
    }

    bool tryAssignMining(Colonist& c, TileType tt) {
        // Find nearest resource tile with >0 resource
        int bestDist = std::numeric_limits<int>::max();
        Vec2i best{-1,-1};
        for (int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x) {
            const Tile& t = world_.at(x,y);
            if (t.type==tt && t.resource>0 && t.walkable) {
                int d = manhattan(c.tile, Vec2i{x,y});
                if (d < bestDist) { bestDist=d; best={x,y}; }
            }
        }
        if (best.x>=0) {
            std::deque<Vec2i> path;
            if (findPathAStar(world_, c.tile, best, path)) {
                c.path = std::move(path);
                c.state = Colonist::State::Moving;
                c.job = Job{ (tt==TileType::Ice)? JobType::MineIce : JobType::MineRock, best, 0, 0, 0};
                return true;
            }
        }
        return false;
    }

    void aiMove(Colonist& c, double dt) {
        c.moveAcc += dt;
        const double stepTime = 0.12; // time per tile
        if (c.moveAcc >= stepTime && !c.path.empty()) {
            c.tile = c.path.front(); c.path.pop_front();
            c.moveAcc -= stepTime;
            if (c.path.empty()) {
                // Arrived
                if (c.job.type == JobType::MineIce || c.job.type == JobType::MineRock ||
                    c.job.type == JobType::Build || c.job.type == JobType::Deliver) {
                    c.state = Colonist::State::Working;
                    c.job.ticks = 18; // short work cycle
                } else {
                    c.state = Colonist::State::Idle;
                }
            }
        }
    }

    void aiWork(Colonist& c, double /*dt*/) {
        if (c.job.ticks>0) { --c.job.ticks; return; }
        // Work complete
        if (c.job.type == JobType::MineIce || c.job.type == JobType::MineRock) {
            Tile& t = world_.at(c.job.target.x, c.job.target.y);
            int mined = std::min(3, t.resource);
            if (mined <= 0) { c.state = Colonist::State::Idle; return; }
            t.resource -= mined;
            if (c.job.type == JobType::MineIce) c.carryIce += mined;
            else c.carryMetal += mined;

            // Path back to HQ to deliver
            std::deque<Vec2i> path;
            if (findPathAStar(world_, c.tile, hq_, path)) {
                c.path = std::move(path);
                c.state = Colonist::State::Moving;
                c.job = Job{JobType::Deliver, hq_, 0, mined, 0};
            } else {
                c.state = Colonist::State::Idle;
            }
        } else if (c.job.type == JobType::Deliver) {
            // Deliver carried items to colony store
            colony_.store.metal += c.carryMetal; c.carryMetal=0;
            colony_.store.ice   += c.carryIce;   c.carryIce=0;
            c.state = Colonist::State::Idle;
        } else if (c.job.type == JobType::Build) {
            // Apply building if pending matches id and costs are available
            if (pendingBuild_.has_value() && pendingBuild_->id == c.job.buildingId) {
                if (colony_.store.metal >= pendingBuild_->def.metalCost &&
                    colony_.store.ice   >= pendingBuild_->def.iceCost) {
                    colony_.store.metal -= pendingBuild_->def.metalCost;
                    colony_.store.ice   -= pendingBuild_->def.iceCost;
                    buildings_.push_back(*pendingBuild_);
                    pendingBuild_.reset();
                }
            }
            c.state = Colonist::State::Idle;
        } else {
            c.state = Colonist::State::Idle;
        }
    }

    void aiReturn(Colonist& c, double /*dt*/) {
        // Not used in this simple pass (Deliver handled in Work end)
        c.state = Colonist::State::Idle;
    }

    // ------------------------------ Hostiles ----------------------------------
    bool inHQArea_(const Vec2i& p) const {
        return (p.x==hq_.x || p.x==hq_.x+1) && (p.y==hq_.y || p.y==hq_.y+1);
    }

    void hostileTick_(double dt) {
        hostileMoveAcc_ += dt;
        const double stepTime = 0.14;
        if (hostileMoveAcc_ < stepTime) return;
        hostileMoveAcc_ -= stepTime;

        for (size_t i=0; i<hostiles_.size();) {
            Hostile& h = hostiles_[i];

            if (h.path.empty()) {
                // Re-path to HQ
                std::deque<Vec2i> p;
                if (!findPathAStar(world_, h.tile, hq_, p)) {
                    // Stuck: remove
                    i = (hostiles_.erase(hostiles_.begin()+i), i);
                    continue;
                }
                h.path = std::move(p);
            }

            if (!h.path.empty()) {
                h.tile = h.path.front();
                h.path.pop_front();
            }

            if (inHQArea_(h.tile)) {
                int lootM = std::min(colony_.store.metal, 2 + h.strength / 5);
                int lootI = std::min(colony_.store.ice,   h.strength / 10);
                colony_.store.metal -= lootM;
                colony_.store.ice   -= lootI;
                PushToast_(std::string("Raiders hit HQ: -") + std::to_string(lootM) + " Metal, -" + std::to_string(lootI) + " Ice");
                i = (hostiles_.erase(hostiles_.begin()+i), i);
                continue;
            }

            ++i;
        }
    }

    void SpawnRaidWithPoints_(int points) {
        int n = std::max(1, points / 20);
        int spawned = 0;
        for (int i=0;i<n;++i) {
            // pick an edge
            bool vertical = rng_.chance(0.5);
            int x = vertical ? (rng_.chance(0.5)?0:world_.W-1) : rng_.irange(0, world_.W-1);
            int y = vertical ? rng_.irange(0, world_.H-1) : (rng_.chance(0.5)?0:world_.H-1);

            // find nearest walkable from that edge point
            Vec2i s{x,y};
            if (!world_.inBounds(s.x,s.y)) continue;
            if (!world_.at(s.x,s.y).walkable) {
                // scan a small neighborhood
                bool found=false;
                for (int dy=-3;dy<=3 && !found;++dy)
                    for (int dx=-3;dx<=3 && !found;++dx) {
                        int X=x+dx, Y=y+dy;
                        if (!world_.inBounds(X,Y)) continue;
                        if (world_.at(X,Y).walkable) { s={X,Y}; found=true; }
                    }
                if (!found) continue;
            }

            Hostile h;
            h.id = nextHostileId_++;
            h.strength = std::max(8, points / std::max(1,n)) + rng_.irange(-3,3);
            h.tile = s;
            std::deque<Vec2i> p;
            if (!findPathAStar(world_, h.tile, hq_, p)) continue;
            h.path = std::move(p);
            hostiles_.push_back(std::move(h));
            ++spawned;
        }
        if (spawned>0) {
            bannerMessage("⚔️ Raid incoming! (" + std::to_string(spawned) + ")");
        }
    }

    void GiveResource_(const char* id, int amt) {
        std::string k = util::lower(id ? std::string(id) : std::string("metal"));
        if (k=="metal" || k=="steel" || k=="components" || k=="silver") {
            colony_.store.metal += std::max(0, amt);
            PushToast_(std::string("Supply: +") + std::to_string(amt) + " Metal");
        } else if (k=="ice") {
            colony_.store.ice += std::max(0, amt);
            PushToast_(std::string("Supply: +") + std::to_string(amt) + " Ice");
        } else if (k=="oxygen" || k=="o2") {
            colony_.store.oxygen += std::max(0, amt);
            PushToast_(std::string("Supply: +") + std::to_string(amt) + " O2");
        } else if (k=="water" || k=="h2o") {
            colony_.store.water += std::max(0, amt);
            PushToast_(std::string("Supply: +") + std::to_string(amt) + " H2O");
        } else {
            colony_.store.metal += std::max(0, amt);
            PushToast_(std::string("Supply: +") + std::to_string(amt) + " (treated as Metal)");
        }
    }

    void PushToast_(const std::string& s) {
        toasts_.push_back(Toast{ s, 4.0 });
        while (toasts_.size() > 6) toasts_.pop_front();
        // Also echo as banner briefly for emphasis
        bannerMessage(s);
    }

    void toastTick_(double dt) {
        for (auto& t : toasts_) t.ttl -= dt;
        while (!toasts_.empty() && toasts_.front().ttl <= 0.0) toasts_.pop_front();
    }

    // ------------------------------ Input Actions -----------------------------
    void onLeftClick() {
        if (buildMode_ && selectedBuild_.has_value()) {
            Vec2i t = currentMouseTile();
            tryPlaceBuilding(*selectedBuild_, t);
            buildMode_=false; selectedBuild_.reset();
            return;
        }
        // else: maybe set a debug target?
    }

    Vec2i currentMouseTile() const {
        int mx,my; SDL_GetMouseState(&mx,&my);
        Vec2i t = camera_.screenToTile(mx,my,tileSize_);
        return t;
    }

    // Try place building footprint; if area valid, reserve as pending for a builder
    [[nodiscard]] bool tryPlaceBuilding(BuildingKind k, Vec2i topLeft) {
        BuildingDef def = (k==BuildingKind::Solar) ? defSolar()
                         : (k==BuildingKind::Habitat) ? defHab()
                         : defOxyGen();
        // Check footprint
        for (int dy=0; dy<def.size.y; ++dy)
            for (int dx=0; dx<def.size.x; ++dx) {
                int x=topLeft.x+dx, y=topLeft.y+dy;
                if (!world_.inBounds(x,y)) return false;
                const Tile& t = world_.at(x,y);
                if (!t.walkable || t.type==TileType::Crater) return false;
            }
        // Check resources to reserve
        if (colony_.store.metal < def.metalCost || colony_.store.ice < def.iceCost) {
            bannerMessage("Not enough resources for " + std::string(buildingName(k)));
            return false;
        }
        pendingBuild_ = Building{ nextBuildingId_++, def, topLeft, true };
        bannerMessage(std::string("Construction queued: ") + buildingName(k) +
                      " (M:" + std::to_string(def.metalCost) + " I:" + std::to_string(def.iceCost) + ")");
        return true;
    }

    void spawnColonist() {
        Colonist c;
        c.id = nextColonistId_++;
        c.tile = hq_;
        colonists_.push_back(c);
        bannerMessage("Colonist arrived");
    }

    // ------------------------------ Save/Load ---------------------------------
    // (Mini patch #3 applied: Windows-native path + atomic write + .bak rollback)
    void saveGame() {
        // Build the text payload exactly as before
        std::ostringstream out;
        out << "MCS_SAVE v1\n";
        out << "seed " << opts_.seed << "\n";
        out << "world " << world_.W << " " << world_.H << "\n";
        out << "hq " << hq_.x << " " << hq_.y << "\n";
        out << "store " << colony_.store.metal << " " << colony_.store.ice << " "
            << colony_.store.oxygen << " " << colony_.store.water << "\n";
        out << "buildings " << buildings_.size() << "\n";
        for (auto& b : buildings_) {
            out << int(b.def.kind) << " " << b.pos.x << " " << b.pos.y << "\n";
        }
        if (pendingBuild_.has_value()) {
            out << "pending 1 " << int(pendingBuild_->def.kind) << " "
                << pendingBuild_->pos.x << " " << pendingBuild_->pos.y << " " << pendingBuild_->id << "\n";
        } else {
            out << "pending 0\n";
        }
        out << "colonists " << colonists_.size() << "\n";
        for (auto& c : colonists_) {
            out << c.id << " " << c.tile.x << " " << c.tile.y << "\n";
        }
        // Storyteller serialization
        cg::Storyteller_Save(out);

        // Resolve %LOCALAPPDATA%\ColonyGame\Saves\<profile>
        const auto dir  = cg::winpaths::ensure_profile_dir(opts_.profile);
        const auto file = dir / (opts_.profile + std::string(".save"));

        // Atomic replace with backup
        std::string err;
        if (!cg::io::write_atomic(file, out.str(), &err, /*make_backup*/true)) {
            bannerMessage(std::string("Save failed: ") + err);
            return;
        }
        bannerMessage("Game saved");
    }

    void loadGame() {
        const auto dir   = cg::winpaths::ensure_profile_dir(opts_.profile);
        const auto file  = dir / (opts_.profile + std::string(".save"));
        auto fileB = file;
        fileB += ".bak"; // "<name>.save.bak"

        std::string bytes, err;
        if (!cg::io::read_all(file, bytes, &err)) {
            // try backup
            if (!cg::io::read_all(fileB, bytes, &err)) {
                bannerMessage("Load failed: no save");
                return;
            }
        }
        std::istringstream in(bytes);

        std::string tag;
        std::string header; in >> header;
        if (header != "MCS_SAVE") { bannerMessage("Load failed: bad header"); return; }
        in >> tag; if (tag!="v1") { /* version ok for now */ }

        in >> tag; if (tag!="seed") { bannerMessage("Load failed: bad seed tag"); return; }
        in >> opts_.seed;
        rng_.reseed(opts_.seed); // ensure deterministic after load

        int W,H; in >> tag; if (tag!="world") { bannerMessage("Load failed: world"); return; }
        in >> W >> H; world_.resize(W,H); world_.generate(rng_); // regen with current seed (okay for demo)

        in >> tag; if (tag!="hq") { bannerMessage("Load failed: hq"); return; }
        in >> hq_.x >> hq_.y;

        in >> tag; if (tag!="store") { bannerMessage("Load failed: store"); return; }
        in >> colony_.store.metal >> colony_.store.ice >> colony_.store.oxygen >> colony_.store.water;

        in >> tag; if (tag!="buildings") { bannerMessage("Load failed: buildings"); return; }
        size_t bc; in >> bc; buildings_.clear();
        for (size_t i=0;i<bc;++i) {
            int kind,x,y; in >> kind >> x >> y;
            BuildingDef def = (kind==int(BuildingKind::Solar))?defSolar(): (kind==int(BuildingKind::Habitat))?defHab():defOxyGen();
            buildings_.push_back(Building{ nextBuildingId_++, def, {x,y}, true});
        }

        in >> tag; if (tag!="pending") { bannerMessage("Load failed: pending"); return; }
        int hasPending; in >> hasPending;
        if (hasPending==1) {
            int kind,x,y,id; in >> kind >> x >> y >> id;
            BuildingDef def = (kind==int(BuildingKind::Solar))?defSolar(): (kind==int(BuildingKind::Habitat))?defHab():defOxyGen();
            pendingBuild_ = Building{id, def, {x,y}, true};
        } else pendingBuild_.reset();

        in >> tag; if (tag!="colonists") { bannerMessage("Load failed: colonists"); return; }
        size_t cc; in >> cc; colonists_.clear();
        for (size_t i=0;i<cc;++i) {
            Colonist c; in >> c.id >> c.tile.x >> c.tile.y;
            colonists_.push_back(c);
            nextColonistId_ = std::max(nextColonistId_, c.id+1);
        }

        // Storyteller deserialize (ignore return; assumes saved with storyteller)
        cg::Storyteller_Load(in);

        bannerMessage("Game loaded");
    }

    // ------------------------------ Flood Debug -------------------------------
    void computeFloodFrom(Vec2i src) {
        floodDist_.assign(world_.W*world_.H, -1);
        if (!world_.inBounds(src.x,src.y)) return;
        std::queue<Vec2i> q;
        int si = world_.idx(src.x,src.y);
        floodDist_[si] = 0; q.push(src);
        while(!q.empty()){
            Vec2i p=q.front(); q.pop();
            for(auto d:kCardinal){
                Vec2i n=p+d;
                if(!world_.inBounds(n.x,n.y)) continue;
                int i=world_.idx(n.x,n.y);
                if(floodDist_[i]!=-1) continue;
                if(!world_.at(n.x,n.y).walkable) continue;
                floodDist_[i]=floodDist_[world_.idx(p.x,p.y)]+1;
                q.push(n);
            }
        }
    }

    // ------------------------------ Rendering ---------------------------------
    void render() {
        // Background color varies with day/night
        double daylight = std::cos((dayTime_ - 0.5) * constants::Tau)*0.5 + 0.5; // 0 at midnight, 1 at noon
        Uint8 R = Uint8(130 + 60*daylight);
        Uint8 G = Uint8(40  + 30*daylight);
        Uint8 B = Uint8(35  + 25*daylight);
        SDL_SetRenderDrawColor(renderer_, R,G,B,255);
        SDL_RenderClear(renderer_);

        drawWorld();
        drawBuildings();
        drawColonists();
        drawHostiles_();
        if (buildMode_ && selectedBuild_.has_value()) drawPlacementPreview(*selectedBuild_);
        drawHQ();
        if (floodDebug_) drawFloodOverlay();
        drawHUD();
        drawToasts_();

        // ---- DevTools overlay draws on top ----
        dev::updateAndRender(renderer_, devBridge_, float(lastFrameSec_));

        SDL_RenderPresent(renderer_);
    }

    void drawWorld() {
        // Tiles
        for (int y=0;y<world_.H;++y) {
            for (int x=0;x<world_.W;++x) {
                const Tile& t = world_.at(x,y);
                Uint32 color = colors::regolith;
                switch (t.type) {
                    case TileType::Regolith: color = colors::regolith; break;
                    case TileType::Sand:     color = colors::sand; break;
                    case TileType::Ice:      color = colors::ice; break;
                    case TileType::Rock:     color = colors::rock; break;
                    case TileType::Crater:   color = colors::crater; break;
                }
                util::setDrawColor(renderer_, color);
                SDL_Rect rc = camera_.tileRect(x,y,tileSize_);
                SDL_RenderFillRect(renderer_, &rc);

                // Optional: minimal grid
                util::setDrawColor(renderer_, colors::gridLine);
                SDL_RenderDrawRect(renderer_, &rc);
            }
        }
    }

    void drawHQ() {
        util::setDrawColor(renderer_, colors::hq);
        SDL_Rect rc = camera_.tileRect(hq_.x, hq_.y, tileSize_);
        rc.w *= 2; rc.h *= 2; // 2x2 HQ marker
        SDL_RenderFillRect(renderer_, &rc);
    }

    void drawBuildings() {
        for (auto& b : buildings_) {
            Uint32 col = (b.def.kind==BuildingKind::Solar)  ? colors::solar :
                         (b.def.kind==BuildingKind::Habitat)? colors::habitat :
                                                              colors::oxyGen;
            util::setDrawColor(renderer_, col);
            SDL_Rect rc = camera_.tileRect(b.pos.x, b.pos.y, tileSize_);
            rc.w = int(b.def.size.x * tileSize_ * camera_.zoom);
            rc.h = int(b.def.size.y * tileSize_ * camera_.zoom);
            SDL_RenderFillRect(renderer_, &rc);

            // Edge
            util::setDrawColor(renderer_, util::packColor(0,0,0,180));
            SDL_RenderDrawRect(renderer_, &rc);
        }

        if (pendingBuild_.has_value()) {
            auto& b = *pendingBuild_;
            SDL_Rect rc = camera_.tileRect(b.pos.x, b.pos.y, tileSize_);
            rc.w = int(b.def.size.x * tileSize_ * camera_.zoom);
            rc.h = int(b.def.size.y * tileSize_ * camera_.zoom);
            util::setDrawColor(renderer_, util::packColor(255,255,255,50));
            SDL_RenderFillRect(renderer_, &rc);
            util::setDrawColor(renderer_, colors::select);
            SDL_RenderDrawRect(renderer_, &rc);
        }
    }

    void drawColonists() {
        for (auto& c : colonists_) {
            util::setDrawColor(renderer_, colors::colonist);
            SDL_Rect rc = camera_.tileRect(c.tile.x, c.tile.y, tileSize_);
            SDL_RenderFillRect(renderer_, &rc);

            // Draw path if any
            if (!c.path.empty()) {
                util::setDrawColor(renderer_, colors::path);
                Vec2i prev = c.tile;
                for (auto p : c.path) {
                    SDL_Rect a = camera_.tileRect(prev.x, prev.y, tileSize_);
                    SDL_Rect b = camera_.tileRect(p.x,    p.y,    tileSize_);
                    SDL_RenderDrawLine(renderer_, a.x + a.w/2, a.y + a.h/2, b.x + b.w/2, b.y + b.h/2);
                    prev = p;
                }
            }
        }
    }

    void drawHostiles_() {
        for (auto& h : hostiles_) {
            util::setDrawColor(renderer_, colors::hostile);
            SDL_Rect rc = camera_.tileRect(h.tile.x, h.tile.y, tileSize_);
            SDL_RenderFillRect(renderer_, &rc);

            if (!h.path.empty()) {
                util::setDrawColor(renderer_, util::packColor(255,80,80,180));
                Vec2i prev = h.tile;
                int steps = 0;
                for (auto p : h.path) {
                    if (steps++ > 12) break; // short tail
                    SDL_Rect a = camera_.tileRect(prev.x, prev.y, tileSize_);
                    SDL_Rect b = camera_.tileRect(p.x,    p.y,    tileSize_);
                    SDL_RenderDrawLine(renderer_, a.x + a.w/2, a.y + a.h/2, b.x + b.w/2, b.y + b.h/2);
                    prev = p;
                }
            }
        }
    }

    void drawPlacementPreview(BuildingKind k) {
        Vec2i t = currentMouseTile();
        BuildingDef def = (k==BuildingKind::Solar)?defSolar(): (k==BuildingKind::Habitat)?defHab():defOxyGen();

        bool valid = true;
        for(int dy=0;dy<def.size.y;++dy) for(int dx=0;dx<def.size.x;++dx) {
            int x=t.x+dx, y=t.y+dy;
            if(!world_.inBounds(x,y)) { valid=false; break; }
            const Tile& tt = world_.at(x,y);
            if(!tt.walkable || tt.type==TileType::Crater) { valid=false; break; }
        }

        SDL_Rect rc = camera_.tileRect(t.x, t.y, tileSize_);
        rc.w = int(def.size.x * tileSize_ * camera_.zoom);
        rc.h = int(def.size.y * tileSize_ * camera_.zoom);
        util::setDrawColor(renderer_, valid ? util::packColor(100,255,100,60) : util::packColor(255,80,80,60));
        SDL_RenderFillRect(renderer_, &rc);
        util::setDrawColor(renderer_, valid ? util::packColor(60,220,60,200) : util::packColor(255,60,60,200));
        SDL_RenderDrawRect(renderer_, &rc);

        // Cost tooltip
        int mx,my; SDL_GetMouseState(&mx,&my);
        std::ostringstream oss;
        oss << buildingName(k) << "  M:" << def.metalCost << " I:" << def.iceCost;
        drawTooltip(mx+12, my+12, oss.str());
    }

    void drawFloodOverlay() {
        for(int y=0;y<world_.H;++y) for(int x=0;x<world_.W;++x) {
            int d = floodDist_[world_.idx(x,y)];
            if (d<0) continue;
            Uint8 v = Uint8(util::clamp(255 - d*8, 0, 255));
            util::setDrawColor(renderer_, util::packColor(50, v, 50, 40));
            SDL_Rect rc = camera_.tileRect(x,y,tileSize_);
            SDL_RenderFillRect(renderer_, &rc);
        }
    }

    // ------------------------------ HUD / Text --------------------------------
    void drawHUD() {
        int pad=8;
        SDL_Rect hud{pad, pad, 620, 112};
        util::setDrawColor(renderer_, colors::hud_bg);
        SDL_RenderFillRect(renderer_, &hud);
        util::setDrawColor(renderer_, util::packColor(0,0,0,200));
        SDL_RenderDrawRect(renderer_, &hud);

        // Left column: time, FPS, sim speed
        int x = hud.x + 8;
        int y = hud.y + 8;

        std::ostringstream line1;
        line1 << "Day " << dayIndex_ << "  Time " << std::fixed << std::setprecision(2) << dayTime_ << "   "
              << "FPS " << std::fixed << std::setprecision(0) << fps_
              << "   x" << std::fixed << std::setprecision(2) << simSpeed_
              << (paused_ ? "  [PAUSED]" : "")
              << (!hasFocus_ ? "  [FOCUS LOST]" : "");
        drawText(x,y,line1.str(), colors::hud_fg); y += 14;

        // Resources
        std::ostringstream r1;
        r1 << "Metal " << colony_.store.metal << "   Ice " << colony_.store.ice
           << "   O2 " << colony_.store.oxygen << "   H2O " << colony_.store.water
           << "   Wealth " << colonyWealth_();
        drawText(x,y,r1.str(), colors::hud_fg); y += 14;

        // Economy balances
        std::ostringstream r2;
        r2 << "Power " << colony_.powerBalance << "   O2 " << colony_.oxygenBalance
           << "   H2O " << colony_.waterBalance
           << "   Pop " << colony_.population << "/" << colony_.housing
           << "   Mood " << int(std::round(averageMood_*100.0)) << "%";
        drawText(x,y,r2.str(), colors::hud_fg); y += 14;

        // Hostiles
        std::ostringstream r3;
        r3 << "Hostiles " << hostiles_.size();
        drawText(x,y,r3.str(), colors::hud_fg); y += 14;

        // Selected build
        std::string bsel = (selectedBuild_.has_value() ? buildingName(*selectedBuild_) : "None");
        drawText(x,y,std::string("Build: ") + bsel, colors::hud_fg); y += 14;

        // Tips
        drawText(x,y,"F1 DevTools   1=Solar  2=Hab  3=O2Gen   LMB place  RMB cancel  G colonist  Ctrl+S save  Ctrl+L load  P pause  +/- speed  H raid  WASD/Arrows pan", colors::hud_accent);

        // Banner (messages)
        if (!banner_.empty() && bannerTime_ > 0.0) {
            drawBanner(banner_);
        }
    }

    void drawToasts_() {
        // Top-right stacked toasts
        int x = camera_.viewportW - 8;
        int y = 8;
        for (int i = int(toasts_.size())-1; i>=0; --i) {
            const auto& t = toasts_[i];
            int w = int(t.text.size()) * 8 + 16;
            SDL_Rect rc{ x - w, y, w, 20 };
            Uint8 alpha = Uint8(std::clamp(t.ttl>1.0?255.0:255.0*(t.ttl/1.0), 0.0, 255.0));
            util::setDrawColor(renderer_, util::packColor(35,35,45, alpha));
            SDL_RenderFillRect(renderer_, &rc);
            util::setDrawColor(renderer_, util::packColor(0,0,0, alpha));
            SDL_RenderDrawRect(renderer_, &rc);
            drawText(rc.x + 8, rc.y + 6, t.text, util::packColor(230,230,240, alpha));
            y += rc.h + 4;
        }
    }

    void drawBanner(const std::string& msg) {
        // Bottom centered banner
        int w = camera_.viewportW;
        int h = 24;
        SDL_Rect rc{ (w- (int)msg.size()*8 - 24)/2, camera_.viewportH - h - 10, (int)msg.size()*8 + 24, h };
        util::setDrawColor(renderer_, colors::banner_bg);
        SDL_RenderFillRect(renderer_, &rc);
        util::setDrawColor(renderer_, util::packColor(0,0,0,200));
        SDL_RenderDrawRect(renderer_, &rc);
        drawText(rc.x + 12, rc.y + 6, msg, colors::banner_fg);
        // (fade handled in update via bannerTime_)
    }

    void bannerMessage(const std::string& msg) {
        banner_ = msg;
        bannerTime_ = 3.0;
    }

    void drawText(int x, int y, const std::string& text, Uint32 color) {
        Uint8 R=(color>>24)&0xFF, G=(color>>16)&0xFF, B=(color>>8)&0xFF, A=color&0xFF;

        for (char ch : text) {
            if (ch=='\n') { y += 12; /* crude new line */ continue; }
            const uint8_t* glyph = font5x7::getGlyph(ch);
            if (glyph) {
                for (int gy=0; gy<7; ++gy) {
                    uint8_t row = glyph[gy];
                    for (int gx=0; gx<5; ++gx) {
                        if (row & (1<<(4-gx))) {
                            SDL_SetRenderDrawColor(renderer_, R,G,B,A);
                            // 2x "bold" pixels for readability
                            SDL_Rect px{ x+gx, y+gy, 1, 1 };
                            SDL_RenderFillRect(renderer_, &px);
                            SDL_Rect px2{ x+gx+1, y+gy, 1, 1 };
                            SDL_RenderFillRect(renderer_, &px2);
                        }
                    }
                }
            }
            x += 8;
        }
    }

    void drawTooltip(int x, int y, const std::string& text) {
        int w = int(text.size()) * 8 + 8;
        SDL_Rect rc{ x, y, w, 18 };
        util::setDrawColor(renderer_, colors::hud_bg);
        SDL_RenderFillRect(renderer_, &rc);
        util::setDrawColor(renderer_, util::packColor(0,0,0,200));
        SDL_RenderDrawRect(renderer_, &rc);
        drawText(x+4, y+5, text, colors::hud_fg);
    }

    // ------------------------------ Timing / FPS ------------------------------
    void fpsCounter(double frameSec) {
        frameAcc_ += frameSec;
        frameCount_++;
        if (frameAcc_ >= 1.0) {
            fps_ = double(frameCount_) / frameAcc_;
            frameAcc_ = 0.0;
            frameCount_ = 0;
            // Update window title occasionally
            std::ostringstream t;
            t << "Mars Colony Simulation  "
              << std::fixed << std::setprecision(0) << fps_ << " FPS";
            SDL_SetWindowTitle(window_, t.str().c_str());
        }
    }

private:
    // ---- SDL version mismatch hint (runtime vs headers) ----
    void sdlVersionSanity_() {
        SDL_version compiled; SDL_VERSION(&compiled);
        SDL_version linked; SDL_GetVersion(&linked);
        // Strong compatibility requires same major; warn if minor/patch lower than headers
        if (linked.major != compiled.major ||
            (linked.major == compiled.major && linked.minor < compiled.minor)) {
            std::ostringstream oss;
            oss << "SDL runtime " << int(linked.major) << "." << int(linked.minor) << "." << int(linked.patch)
                << " < headers " << int(compiled.major) << "." << int(compiled.minor) << "." << int(compiled.patch);
            // In-game toast (also banner)
            PushToast_(oss.str());
            // Also echo to stderr for logs
            std::cerr << "[warn] " << oss.str() << "\n";
        }
        // Compile-time check (should always pass for SDL2+)
        static_assert(SDL_MAJOR_VERSION >= 2, "Requires SDL2 or newer.");
    }

    // ---- DevTools helpers ----
    void applyTileArchetype_(Tile& t, TileType nt) {
        t.type = nt;
        switch (nt) {
            case TileType::Regolith: t.walkable=true; t.cost=10; t.resource=0; break;
            case TileType::Sand:     t.walkable=true; t.cost=12; t.resource=0; break;
            case TileType::Ice:      t.walkable=true; t.cost=14; if (t.resource==0) t.resource=10; break;
            case TileType::Rock:     t.walkable=true; t.cost=16; if (t.resource==0) t.resource=8;  break;
            case TileType::Crater:   t.walkable=false; t.cost=255; t.resource=0; break;
        }
    }

    void setupDevToolsBridge_() {
        // World size
        devBridge_.gridSize = [&]() -> dev::Size {
            return dev::Size{ world_.W, world_.H };
        };
        // Read tile id
        devBridge_.getTile = [&](int x, int y) -> int {
            if (!world_.inBounds(x,y)) return 0;
            return int(world_.at(x,y).type);
        };
        // Write/paint tile id
        devBridge_.setTile = [&](int x, int y, int id) {
            if (!world_.inBounds(x,y)) return;
            id = util::clamp(id, 0, 4);
            Tile& t = world_.at(x,y);
            applyTileArchetype_(t, TileType(id));
        };
        // Iterate agents
        devBridge_.forEachAgent = [&](std::function<void(const dev::Agent&)> fn) {
            for (auto& c : colonists_) {
                dev::Agent a;
                a.id = c.id;
                a.x = c.tile.x;
                a.y = c.tile.y;
                a.name = std::string("C") + std::to_string(c.id);
                fn(a);
            }
            for (auto& h : hostiles_) {
                dev::Agent a;
                a.id = -h.id;
                a.x = h.tile.x;
                a.y = h.tile.y;
                a.name = std::string("R") + std::to_string(h.id);
                fn(a);
            }
        };
    }

    // ---- Wealth estimator for Storyteller ----
    int colonyWealth_() const {
        int w = 0;
        // stores
        w += colony_.store.metal * 2;
        w += colony_.store.ice   * 1;
        w += colony_.store.oxygen* 1;
        w += colony_.store.water * 1;
        // buildings (approx: cost*2 as value)
        for (auto& b : buildings_) {
            w += b.def.metalCost * 2 + b.def.iceCost * 1;
        }
        // population value
        w += int(colonists_.size()) * 40;
        return w;
    }

private:
    // SDL / options
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    GameOptions   opts_;

    // World
    World world_;
    Rng   rng_;
    int   tileSize_ = 24;
    Camera camera_;

    // Colony / Entities
    Colony colony_;
    Vec2i  hq_{0,0};
    std::vector<Building> buildings_;
    std::optional<Building> pendingBuild_;
    int nextBuildingId_ = 1;

    std::vector<Colonist> colonists_;
    int nextColonistId_ = 1;

    // Hostiles
    std::vector<Hostile> hostiles_;
    int nextHostileId_ = 1;
    double hostileMoveAcc_ = 0.0;

    // Sim
    double dayTime_ = 0.25; // morning
    int    dayIndex_ = 0;
    double averageMood_ = 0.7;
    bool   paused_ = false;
    double simSpeed_ = 1.0;
    double lastFrameSec_ = 1.0/60.0; // for DevTools dt & banner fade

    // Input
    Vec2i keyPan_{0,0};
    bool  buildMode_ = false;
    std::optional<BuildingKind> selectedBuild_;

    // Focus behavior
    bool  hasFocus_ = true;
    bool  pauseOnFocusLoss_ = true;

    // Debug overlay
    bool   floodDebug_ = false;
    Vec2i  floodFrom_{0,0};
    std::vector<int> floodDist_;

    // Banner message
    std::string banner_;
    double      bannerTime_ = 0.0;

    // Toasts
    struct Toast { std::string text; double ttl; };
    std::deque<Toast> toasts_;

    // FPS
    double frameAcc_ = 0.0;
    int    frameCount_ = 0;
    double fps_ = 0.0;

    // DevTools
    dev::Bridge devBridge_;
};

// -------------------------------- Game wrapper -------------------------------
Game::Game(SDL_Window* window, SDL_Renderer* renderer, const GameOptions& opts)
    : window_(window), renderer_(renderer), opts_(opts) {}

int Game::Run() {
    GameImpl impl(window_, renderer_, opts_);
    return impl.run();
}
