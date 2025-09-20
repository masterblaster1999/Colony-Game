// ============================================================================
// CombatLite.hpp (v2) - massively upgraded, still header-only & deterministic.
// Ranged combat on a grid with A* pathing hooks, robust LoS, cover/flanking,
// projectiles, basic suppression, and small utility-AI scoring.
//
// Fit for Colony-Game Phase 1. No dynamic allocations inside hot loops.
// Public API is compatible with v1 where possible.
// ============================================================================

#pragma once

// --- Platform trim for Windows ---
#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
#endif

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>
#include <string>
#include <optional>
#include <functional>
#include <utility>

// Must expose cg::pf::{GridView,Point,Result,aStar}
#include "Pathfinding.hpp"

#ifndef CG_COMBAT_ASSERT
  #include <cassert>
  #define CG_COMBAT_ASSERT(x) assert(x)
#endif

// ======================
// Configuration knobs
// ======================
#ifndef CG_COMBAT_REEVAL_HZ
#define CG_COMBAT_REEVAL_HZ 4.0f   // think() cadence
#endif

#ifndef CG_COMBAT_MAX_ASTAR_EXPANSIONS
#define CG_COMBAT_MAX_ASTAR_EXPANSIONS 800
#endif

#ifndef CG_COMBAT_RANGE_METRIC
// 0=Manhattan, 1=Chebyshev, 2=Euclidean (rounded)
#define CG_COMBAT_RANGE_METRIC 1
#endif

#ifndef CG_COMBAT_LOS_MODE
// 0=Bresenham (classic), 1=Supercover/DDA (recommended)
#define CG_COMBAT_LOS_MODE 1
#endif

#ifndef CG_COMBAT_USE_PCG32
// 0=keep xorshift64*, 1=PCG32 (recommended)
#define CG_COMBAT_USE_PCG32 1
#endif

#ifndef CG_COMBAT_PREDETERMINE_HITS
// If 1, decide hit/miss (and crit) at fire-time for consistent UX.
#define CG_COMBAT_PREDETERMINE_HITS 1
#endif

#ifndef CG_COMBAT_SUPPRESSION_AIM_PENALTY
#define CG_COMBAT_SUPPRESSION_AIM_PENALTY 20 // -20 aim while suppressed
#endif

#ifndef CG_COMBAT_SUPPRESSION_EXPIRES_SEC
#define CG_COMBAT_SUPPRESSION_EXPIRES_SEC 1.25f
#endif

namespace cg::combat {

// ----------------------
// Deterministic RNGs
// ----------------------
struct RNG_Xor64Star {
    // Vigna (xorshift64*, scrambled) constant 0x2545F4914F6CDD1D
    // Good speed/quality for games; keep for backward compatibility.
    // Ref: Vigna “An experimental exploration of Marsaglia’s xorshift generators, scrambled” (TOMS 2016).
    uint64_t s;
    explicit RNG_Xor64Star(uint64_t seed=1u) : s(seed ? seed : 1u) {}
    inline uint32_t nextU32() {
        uint64_t x = s;
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        s = x;
        return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
    }
    inline float nextFloat01() { return (nextU32() >> 8) * (1.0f / 16777216.0f); } // 24-bit
};

// PCG32: small, fast, statistically solid PRNG for games.
// Ref: O’Neill “PCG: A Family of Simple Fast…” (2014; TOMS paper v1.02).
struct RNG_PCG32 {
    uint64_t state; uint64_t inc;
    explicit RNG_PCG32(uint64_t seed=1u, uint64_t seq=54u) {
        // increment must be odd
        inc = (seq << 1u) | 1u;
        state = 0u; nextU32(); state += seed; nextU32();
    }
    inline uint32_t nextU32() {
        uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ULL + inc;
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    inline float nextFloat01() { return (nextU32() >> 8) * (1.0f / 16777216.0f); }
};

#if CG_COMBAT_USE_PCG32
using RNG = RNG_PCG32;
#else
using RNG = RNG_Xor64Star;
#endif

// ----------------------
// Core data
// ----------------------
enum class Faction : uint8_t { Colonist=0, Wildlife=1, Raider=2 };

// Basic resistances (percentage). Keep simple for Phase 1.
enum class DamageType : uint8_t { Ballistic=0, Fire=1, Shock=2, COUNT=3 };

struct Resistances {
    // +% reduces damage; negative values increase damage.
    int8_t vs[(int)DamageType::COUNT] = {0,0,0};
    inline int apply(DamageType t, int dmg) const {
        int8_t r = vs[(int)t];
        // clamp to avoid weirdness; e.g., +/-90% extremes
        r = std::max<int8_t>(-90, std::min<int8_t>(90, r));
        return dmg - (dmg * r) / 100;
    }
};

struct Stats {
    int maxHP = 100;
    int hp    = 100;
    int armor = 0;        // flat reduction after resistances
    Resistances resist{};
};

// “XCOM-ish” weapon knobs (mapped to your light system):
//  - accuracyBase: base aim% at optimalRange with no cover
//  - optimalRange / falloffPerTile: accuracy loss beyond/inside optimal
//  - critBase: base crit% when hit (flanking adds bonus)
//  - projectileSpeed: tiles/sec for time-of-flight
struct Weapon {
    int   range            = 9;       // tiles (chebyshev)
    int   damageMin        = 8;
    int   damageMax        = 14;
    int   burst            = 1;       // bullets per trigger
    float cooldown         = 0.8f;    // seconds between shots
    float projectileSpeed  = 16.0f;   // tiles/sec
    float spreadRad        = 0.02f;   // small jitter (visual)
    DamageType dtype       = DamageType::Ballistic;

    // New accuracy model
    int   accuracyBase     = 65;      // base aim%
    int   critBase         = 5;       // base crit%
    int   flankCritBonus   = 50;      // +% crit when flanking (XCOM-inspired)
    int   optimalRange     = 7;       // tiles
    int   falloffPerTile   = 3;       // -% aim per tile beyond optimal (or inside for snubs)
    bool  suppressionCapable = false; // if true, “fire” can suppress instead of damage
};

struct Status {
    bool suppressed = false;
    float suppressedTime = 0.f;
};

struct Combatant {
    // Identity & placement
    int      id = -1;
    Faction  team = Faction::Colonist;
    pf::Point pos{0,0};
    int      facing = 0;

    // Stats & gear
    Stats    stats{};
    Weapon   weapon{};
    Status   status{};

    // Brain
    enum class State : uint8_t { Idle, Patrol, Engage, SeekCover, Flank, Suppress, Retreat, Downed } state = State::Idle;
    float    thinkTimer = 0.f;      // re-eval cadence
    float    atkCooldown = 0.f;     // time until next shot
    int      targetId = -1;         // enemy id
    pf::Point home{0,0};
    std::vector<pf::Point> path;
    int      pathIdx = 0;

    // Morale-lite
    int      pain = 0;              // grows on hits; can trigger Retreat
};

struct Projectile {
    pf::Point from{};
    pf::Point to{};                 // destination (tile center assumption)
    float     t = 0.f;              // 0..1 along segment (parametric)
    float     speedTilesPerSec = 16.f;
    int       srcId = -1;
    int       dstId = -1;           // locked target (if any)
    DamageType dtype = DamageType::Ballistic;
    int       dmg = 1;
    bool      resolved = false;

#if CG_COMBAT_PREDETERMINE_HITS
    bool      willHit = true;       // decided at fire time
    bool      willCrit = false;
#endif

    // Minor visual jitter
    float     spreadRad = 0.f;
};

// ----------------------
// Lightweight occupancy/collision hooks (provided by game)
// ----------------------
struct WorldHooks {
    const pf::GridView* grid = nullptr;

    // Is the tile blocking LoS? (e.g., walls, thick trees, tall rocks)
    bool (*opaque)(int x,int y) = nullptr;

    // Is the tile passable? (fallback to grid->walkable)
    bool (*passable)(int x,int y) = nullptr;

    // Optional: cover value [0..100] (0=open field, 100=full cover)
    // Convention (tweakable):
    //  0..24 = none, 25..59 = low, 60..100 = high
    int  (*coverAt)(int x,int y) = nullptr;

    // Optional: is there a unit occupying this tile (for cheap avoidance)?
    bool (*occupied)(int x,int y) = nullptr;
};

// ----------------------
// Events (optional; wire up to UI/log/audio)
// ----------------------
struct Events {
    // Fired once per projectile creation.
    std::function<void(const Projectile&)> onShoot;
    // Fired when damage applied.
    std::function<void(const Combatant& target, int dmg, bool crit, int srcId)> onDamage;
    // Fired once when a unit goes down.
    std::function<void(const Combatant&)> onDowned;
};

// ----------------------
// Utility
// ----------------------
static inline int clampi(int v,int lo,int hi){ return (v<lo)?lo:((v>hi)?hi:v); }

static inline int manhattan(const pf::Point& a, const pf::Point& b) {
    const int dx = (a.x>b.x)?(a.x-b.x):(b.x-a.x);
    const int dy = (a.y>b.y)?(a.y-b.y):(b.y-a.y);
    return dx+dy;
}

static inline int chebyshev(const pf::Point& a, const pf::Point& b) {
    const int dx = (a.x>b.x)?(a.x-b.x):(b.x-a.x);
    const int dy = (a.y>b.y)?(a.y-b.y):(b.y-a.y);
    return (dx>dy)?dx:dy;
}

static inline int euclidRounded(const pf::Point& a, const pf::Point& b) {
    const int dx = a.x-b.x, dy = a.y-b.y;
    return int(std::lround(std::sqrt(double(dx*dx + dy*dy))));
}

static inline int gridDistance(const pf::Point& a, const pf::Point& b) {
#if CG_COMBAT_RANGE_METRIC==0
    return manhattan(a,b);
#elif CG_COMBAT_RANGE_METRIC==1
    return chebyshev(a,b);
#else
    return euclidRounded(a,b);
#endif
}

static inline bool inRange(const pf::Point& a, const pf::Point& b, int r) {
    return gridDistance(a,b) <= r;
}

// ----------------------
// Line-of-sight
// ----------------------
// 0) Bresenham variant (fast, simple) – ok on square grids. Reference: RogueBasin.
// 1) “Supercover”/DDA (voxel traversal in 2D) – robust against diagonal corner cases.
//    Reference: Amanatides & Woo (1987). Suitable for targeting rays across a grid.
static inline bool lineOfSight_Bresenham(const WorldHooks& w, pf::Point a, pf::Point b) {
    if (!w.grid) return false;
    int x0=a.x, y0=a.y, x1=b.x, y1=b.y;
    int dx = std::abs(x1-x0), dy = -std::abs(y1-y0);
    int sx = (x0<x1)?1:-1, sy = (y0<y1)?1:-1;
    int err = dx+dy;
    auto opq = w.opaque;
    while (true) {
        if (opq && opq(x0,y0)) return (x0==a.x && y0==a.y);
        if (x0==x1 && y0==y1) return true;
        int e2 = err<<1;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static inline bool tileOpaque(const WorldHooks& w, int x, int y) {
    if (!w.grid) return true;
    if (!w.grid->inBounds(x,y)) return true;
    return (w.opaque && w.opaque(x,y));
}

// DDA step through all cells intersected by a center-to-center ray.
static inline bool lineOfSight_Supercover(const WorldHooks& w, pf::Point a, pf::Point b) {
    if (!w.grid) return false;
    // Start blocked tile is allowed (standing in a doorway)
    auto blocked = [&](int x,int y){ return tileOpaque(w,x,y) && !(x==a.x && y==a.y); };
    int x0=a.x, y0=a.y, x1=b.x, y1=b.y;
    int dx = std::abs(x1-x0), dy = std::abs(y1-y0);
    int sx = (x0<x1)?1:-1, sy = (y0<y1)?1:-1;

    int x = x0, y = y0;
    if (blocked(x,y)) return false;
    if (dx==0 && dy==0) return true;

    double tMaxX, tMaxY;
    double tDeltaX = (dx==0) ? std::numeric_limits<double>::infinity() : 1.0 / double(dx);
    double tDeltaY = (dy==0) ? std::numeric_limits<double>::infinity() : 1.0 / double(dy);
    tMaxX = tDeltaX * 0.5;
    tMaxY = tDeltaY * 0.5;

    int steps = dx + dy + 4;
    while (steps-- > 0) {
        if (x==x1 && y==y1) return true;
        if (tMaxX < tMaxY) {
            x += sx; tMaxX += tDeltaX;
        } else if (tMaxY < tMaxX) {
            y += sy; tMaxY += tDeltaY;
        } else {
            // diagonal crossing a corner: step both (supercover)
            x += sx; y += sy; tMaxX += tDeltaX; tMaxY += tDeltaY;
        }
        if (blocked(x,y)) return false;
    }
    return false;
}

static inline bool lineOfSight(const WorldHooks& w, pf::Point a, pf::Point b) {
#if CG_COMBAT_LOS_MODE==0
    return lineOfSight_Bresenham(w,a,b);
#else
    return lineOfSight_Supercover(w,a,b);
#endif
}

// Simple cover score from environment and opaque neighbors (legacy-friendly).
static inline int coverScore(const WorldHooks& w, int x, int y) {
    int score = 0;
    if (w.coverAt) score += clampi(w.coverAt(x,y),0,100);
    static const int d[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto& o: d) {
        const int nx=x+o[0], ny=y+o[1];
        if (w.opaque && w.opaque(nx,ny)) score += 6; // up to ~48 extra
    }
    return clampi(score, 0, 100);
}

// Map cover (0..100) -> aim penalty like XCOM (none/half/full).
// By default: <25 none, 25..59 low (-20 aim), 60+ high (-40 aim).
static inline int coverAimPenalty(int cover01to100) {
    if (cover01to100 >= 60) return 40;
    if (cover01to100 >= 25) return 20;
    return 0;
}

// Determine if shooter is flanking target: sample two tiles adjacent to target,
// perpendicular to shot direction; if neither provides cover toward shooter,
// count as flanked (removes cover, boosts crit).
static inline bool isFlanked(const WorldHooks& w, pf::Point shooter, pf::Point target) {
    int dx = (target.x>shooter.x) ? 1 : (target.x<shooter.x ? -1 : 0);
    int dy = (target.y>shooter.y) ? 1 : (target.y<shooter.y ? -1 : 0);
    // orthogonal vector choices
    std::array<pf::Point,2> flankTiles = { pf::Point{target.x - dy, target.y + dx},
                                           pf::Point{target.x + dy, target.y - dx} };
    int bestCover = 0;
    for (auto& p : flankTiles) {
        if (!w.grid || !w.grid->inBounds(p.x,p.y)) continue;
        int c = coverScore(w, p.x, p.y);
        // Check if that cover actually lies between shooter and target by requiring LoS to be blocked
        if (w.opaque && w.opaque(p.x, p.y)) bestCover = std::max(bestCover, c);
    }
    // If there is no significant side cover, treat as flanked
    return bestCover < 25;
}

// ----------------------
// Damage helpers
// ----------------------
static inline int rollDamage(RNG& rng, const Weapon& w) {
    const int span = std::max(0, w.damageMax - w.damageMin);
    return w.damageMin + int(rng.nextFloat01() * float(span+1));
}

static inline int applyArmor(int dmg, int armor) {
    int out = dmg - std::max(0, armor);
    return (out<1)?1:out;
}

// ----------------------
// Aim / chance-to-hit model (XCOM-inspired knobs)
// ----------------------
struct HitContext {
    int distance = 0;
    int coverPenalty = 0;
    bool flanked = false;
    bool suppressed = false;
};

static inline int distancePenalty(const Weapon& w, int dist) {
    // soft falloff beyond optimalRange
    int delta = dist - w.optimalRange;
    return (delta>0) ? (delta * w.falloffPerTile) : 0;
}

static inline int computeAimPercent(const Weapon& w, const Combatant& shooter, const Combatant& target, const HitContext& hc) {
    int aim = w.accuracyBase;
    aim -= distancePenalty(w, hc.distance);
    if (!hc.flanked) aim -= hc.coverPenalty;
    if (hc.suppressed) aim -= CG_COMBAT_SUPPRESSION_AIM_PENALTY;
    aim = clampi(aim, 5, 95); // never 0/100
    return aim;
}

static inline int computeCritPercent(const Weapon& w, bool flanked) {
    int c = w.critBase + (flanked ? w.flankCritBonus : 0);
    return clampi(c, 0, 100);
}

// ----------------------
// Combat world
// ----------------------
struct World {
    WorldHooks hooks{};
    Events     events{};
    RNG        rng{12345};

    std::vector<Combatant>  units;
    std::vector<Projectile> shots;

    // ---- High-level API ----
    int spawnUnit(Faction f, pf::Point at, const Stats& st, const Weapon& wp) {
        const int id = (int)units.size();
        Combatant c; c.id=id; c.team=f; c.pos=at; c.stats=st; c.weapon=wp; c.home=at;
        units.push_back(c); return id;
    }

    // Tick all combat (dt in seconds)
    void update(float dt) {
        if (!hooks.grid) return;

        // Update brains & actions
        for (auto& u : units) {
            if (u.stats.hp <= 0) { u.state = Combatant::State::Downed; continue; }

            // decay suppression
            if (u.status.suppressed) {
                u.status.suppressedTime -= dt;
                if (u.status.suppressedTime <= 0) { u.status.suppressed=false; u.status.suppressedTime=0.f; }
            }

            u.thinkTimer -= dt;
            u.atkCooldown = std::max(0.f, u.atkCooldown - dt);
            if (u.thinkTimer <= 0.f) {
                think(u);
                u.thinkTimer = 1.0f / CG_COMBAT_REEVAL_HZ;
            }
            stepAlongPath(u);
            attemptFire(u);
        }

        // Projectiles fly & resolve
        for (auto& p : shots) {
            if (p.resolved) continue;
            const int steps = std::max(1, gridDistance(p.from, p.to));
            p.t += (p.speedTilesPerSec * dt) / float(steps);
            if (p.t >= 1.f) {
                onProjectileArrive(p);
                p.resolved = true;
            }
        }

        // GC finished shots
        shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Projectile& p){ return p.resolved; }), shots.end());
    }

    // ---- AI / Behavior ----
    void think(Combatant& u) {
        if (u.stats.hp <= 0) { u.state = Combatant::State::Downed; return; }

        // Acquire/validate target
        if (u.targetId < 0 || !isValidTarget(u, u.targetId)) {
            u.targetId = findTarget(u);
        }
        if (u.targetId < 0) { u.state = Combatant::State::Patrol; u.path.clear(); u.pathIdx=0; return; }

        auto& t = units[u.targetId];
        const bool los = lineOfSight(hooks, u.pos, t.pos);
        const int dist = gridDistance(u.pos, t.pos);
        const bool inR = dist <= u.weapon.range;

        // Simple morale: retreat when <25% HP or high pain
        if (u.stats.hp < (u.stats.maxHP/4) || u.pain >= 40) {
            u.state = Combatant::State::Retreat;
            pathTo(u, safeRetreatTile(u));
            return;
        }

        // Prioritize flanking move if we have LoS but poor chance (heavy cover)
        if (los && inR && !isFlanked(hooks, u.pos, t.pos)) {
            auto flank = flankTile(u, t.pos, /*probe*/8);
            if (flank && *flank != u.pos) {
                u.state = Combatant::State::Flank;
                pathTo(u, *flank);
                return;
            }
        }

        if (los && inR) {
            u.state = Combatant::State::Engage;
            // small reposition toward better cover if idle
            auto better = coverPeekTile(u, t.pos, 6);
            if (better && *better != u.pos) pathTo(u, *better);
            return;
        }

        // If no LoS or not in range -> advance via cover lanes toward target
        u.state = Combatant::State::SeekCover;
        pf::Point goal = coverAdvanceTile(u, t.pos, 8);
        pathTo(u, goal);
    }

    // Enemy selection within 24 tiles; prefer closer/low-HP/flanked opportunities
    int findTarget(const Combatant& u) const {
        int best=-1, bestScore=std::numeric_limits<int>::min();
        for (auto& v : units) {
            if (v.id==u.id || v.stats.hp<=0) continue;
            if (v.team == u.team) continue;
            int d = gridDistance(u.pos, v.pos);
            if (d > 24) continue;
            int score = 200 - d*3 + (v.stats.maxHP - v.stats.hp) + (lineOfSight(hooks, u.pos, v.pos) ? 10 : 0);
            if (score > bestScore) { bestScore = score; best = v.id; }
        }
        return best;
    }

    bool isValidTarget(const Combatant& u, int targetId) const {
        if (targetId < 0 || targetId >= (int)units.size()) return false;
        auto& t = units[targetId];
        if (t.stats.hp <= 0) return false;
        if (t.team == u.team) return false;
        return true;
    }

    // ---- Movement ----
    void stepAlongPath(Combatant& u) {
        if (u.pathIdx >= (int)u.path.size()) return;
        // Snap-step per tick (your colony sim can tween visually)
        if (u.pos.x == u.path[u.pathIdx].x && u.pos.y == u.path[u.pathIdx].y) {
            ++u.pathIdx;
        }
        if (u.pathIdx < (int)u.path.size()) {
            // avoid stepping into occupied hard blocks (if hook available)
            auto next = u.path[u.pathIdx];
            if (!(hooks.occupied && hooks.occupied(next.x, next.y))) {
                u.pos = next;
            } else {
                // re-plan lightly if blocked
                pathTo(u, u.path.back());
            }
        }
    }

    void pathTo(Combatant& u, pf::Point goal) {
        u.path.clear(); u.pathIdx = 0;
        if (!hooks.grid) return;
        std::vector<pf::Point> p;
        auto r = pf::aStar(*hooks.grid, u.pos, goal, p, CG_COMBAT_MAX_ASTAR_EXPANSIONS);
        if (r == pf::Result::Found && !p.empty()) {
            u.path = std::move(p);
            u.pathIdx = 0;
        }
    }

    // Choose a tile with better cover but still LoS to target (peek)
    std::optional<pf::Point> coverPeekTile(const Combatant& u, const pf::Point& target, int radius) {
        if (!hooks.grid) return std::nullopt;
        int bestS=std::numeric_limits<int>::min(); pf::Point best{u.pos};
        for (int dy=-radius; dy<=radius; ++dy)
        for (int dx=-radius; dx<=radius; ++dx) {
            int x=u.pos.x+dx, y=u.pos.y+dy;
            if (!hooks.grid->inBounds(x,y) || !(hooks.passable?hooks.passable(x,y):hooks.grid->walkable(x,y))) continue;
            int c = coverScore(hooks, x,y);
            int distT = gridDistance({x,y}, target);
            int inRangeBonus = (distT <= u.weapon.range) ? 10 : 0;
            int score = c*2 + inRangeBonus - distT;
            if (score > bestS && lineOfSight(hooks, {x,y}, target)) { bestS=score; best={x,y}; }
        }
        if (bestS == std::numeric_limits<int>::min()) return std::nullopt;
        return best;
    }

    // Move toward enemy, preferring tiles that increase cover and LoS potential
    pf::Point coverAdvanceTile(const Combatant& u, const pf::Point& target, int probeRadius) {
        pf::Point best = u.pos; int bestScore = std::numeric_limits<int>::min();
        const int dNow = gridDistance(u.pos, target);
        for (int dy=-probeRadius; dy<=probeRadius; ++dy)
        for (int dx=-probeRadius; dx<=probeRadius; ++dx) {
            pf::Point p{u.pos.x+dx, u.pos.y+dy};
            if (!hooks.grid->inBounds(p.x,p.y) || !(hooks.passable?hooks.passable(p.x,p.y):hooks.grid->walkable(p.x,p.y))) continue;
            int c = coverScore(hooks, p.x,p.y);
            int dProp = gridDistance(p, target);
            int score = c*2 + (dNow - dProp)*4 + (lineOfSight(hooks,p,target)?15:0);
            if (score > bestScore) { bestScore=score; best=p; }
        }
        return best;
    }

    // Seek a tile that provides a flanking angle while keeping LoS.
    std::optional<pf::Point> flankTile(const Combatant& u, const pf::Point& target, int probeRadius) {
        pf::Point best = u.pos; int bestScore = std::numeric_limits<int>::min();
        for (int dy=-probeRadius; dy<=probeRadius; ++dy)
        for (int dx=-probeRadius; dx<=probeRadius; ++dx) {
            pf::Point p{u.pos.x+dx, u.pos.y+dy};
            if (!hooks.grid->inBounds(p.x,p.y) || !(hooks.passable?hooks.passable(p.x,p.y):hooks.grid->walkable(p.x,p.y))) continue;
            if (!lineOfSight(hooks, p, target)) continue;
            bool flank = isFlanked(hooks, p, target);
            int c = coverScore(hooks, p.x,p.y);
            int dProp = gridDistance(p, target);
            int score = (flank?30:0) + c*2 - dProp;
            if (score > bestScore) { bestScore=score; best=p; }
        }
        if (bestScore == std::numeric_limits<int>::min()) return std::nullopt;
        return best;
    }

    // Fallback retreat tile: head ~8 tiles away from target or toward home
    pf::Point safeRetreatTile(const Combatant& u) {
        pf::Point goal = u.home;
        if (u.targetId>=0 && u.targetId<(int)units.size()) {
            auto& t = units[u.targetId];
            int dx = clampi(u.pos.x - t.pos.x, -1, 1);
            int dy = clampi(u.pos.y - t.pos.y, -1, 1);
            goal = { clampi(u.pos.x + dx*8, 0, hooks.grid->w-1),
                     clampi(u.pos.y + dy*8, 0, hooks.grid->h-1) };
        }
        return goal;
    }

    // ---- Shooting ----
    void attemptFire(Combatant& u) {
        if (u.atkCooldown > 0.f || u.targetId < 0 || u.stats.hp<=0) return;

        auto& t = units[u.targetId];
        if (!isValidTarget(u, t.id)) return;

        const bool los = lineOfSight(hooks, u.pos, t.pos);
        const int dist = gridDistance(u.pos, t.pos);
        if (!los || dist > u.weapon.range) return;

        u.atkCooldown = u.weapon.cooldown;

        // Suppression (optional branch): if weapon supports and chance to hit is poor
        HitContext hc{};
        hc.distance = dist;
        hc.flanked = isFlanked(hooks, u.pos, t.pos);
        int coverAtT = hooks.coverAt ? hooks.coverAt(t.pos.x, t.pos.y) : coverScore(hooks, t.pos.x, t.pos.y);
        hc.coverPenalty = coverAimPenalty(coverAtT);
        hc.suppressed = t.status.suppressed;

        const int aim = computeAimPercent(u.weapon, u, t, hc);
        const bool poorHit = (aim <= 30);

        if (u.weapon.suppressionCapable && poorHit) {
            // apply suppression to target instead of firing bullets
            t.status.suppressed = true;
            t.status.suppressedTime = CG_COMBAT_SUPPRESSION_EXPIRES_SEC;
            // (optional) spawn a “tracer” shot for visuals if you want – ignored here
            return;
        }

        const int shotsFired = std::max(1, u.weapon.burst);
        for (int i=0;i<shotsFired;++i) {
            Projectile p;
            p.from = u.pos; p.to = t.pos;
            p.speedTilesPerSec = u.weapon.projectileSpeed;
            p.srcId = u.id; p.dstId = t.id;
            p.dtype = u.weapon.dtype;
            p.dmg   = applyArmor(t.stats.resist.apply(p.dtype, rollDamage(rng, u.weapon)), t.stats.armor);
            p.spreadRad = u.weapon.spreadRad;

#if CG_COMBAT_PREDETERMINE_HITS
            const int aimNow = computeAimPercent(u.weapon, u, t, hc);
            const int critNow = computeCritPercent(u.weapon, hc.flanked);
            p.willHit = (rng.nextFloat01()*100.0f) < (float)aimNow;
            p.willCrit = p.willHit && (rng.nextFloat01()*100.0f) < (float)critNow;
#endif

            // Tiny dispersion: ±1 tile jitter with very low probability (visual)
            if (rng.nextFloat01() < u.weapon.spreadRad) {
                int jx = (rng.nextFloat01()<0.5f? -1: 1);
                int jy = (rng.nextFloat01()<0.5f? -1: 1);
                p.to.x = clampi(p.to.x + jx, 0, hooks.grid->w-1);
                p.to.y = clampi(p.to.y + jy, 0, hooks.grid->h-1);
            }

            shots.push_back(p);
            if (events.onShoot) events.onShoot(shots.back());
        }
    }

    void onProjectileArrive(Projectile& p) {
        if (p.dstId < 0 || p.dstId >= (int)units.size()) return;
        auto& t = units[p.dstId];
        if (t.stats.hp <= 0) return;

        // If no LoS at impact time (moved behind wall), count as miss
        if (!lineOfSight(hooks, p.from, t.pos)) return;

        bool hit = true;
        bool crit = false;

#if CG_COMBAT_PREDETERMINE_HITS
        hit = p.willHit; crit = p.willCrit;
#else
        // Late resolve (less consistent UX but more reactive to last-moment changes).
        auto& s = units[p.srcId];
        HitContext hc{};
        hc.distance = gridDistance(s.pos, t.pos);
        hc.flanked = isFlanked(hooks, s.pos, t.pos);
        int coverAtT = hooks.coverAt ? hooks.coverAt(t.pos.x, t.pos.y) : coverScore(hooks, t.pos.x, t.pos.y);
        hc.coverPenalty = coverAimPenalty(coverAtT);
        hc.suppressed = t.status.suppressed;
        const int aim = computeAimPercent(s.weapon, s, t, hc);
        hit = (rng.nextFloat01()*100.0f) < (float)aim;
        if (hit) {
            const int critPct = computeCritPercent(s.weapon, hc.flanked);
            crit = (rng.nextFloat01()*100.0f) < (float)critPct;
        }
#endif

        if (!hit) return;

        int dmg = p.dmg;
        if (crit) dmg = int(std::round(dmg * 1.5f));

        t.stats.hp -= dmg;
        t.pain     += std::min(20, dmg);
        if (events.onDamage) events.onDamage(t, dmg, crit, p.srcId);

        if (t.stats.hp <= 0) {
            t.stats.hp = 0;
            t.state = Combatant::State::Downed;
            if (events.onDowned) events.onDowned(t);
        }
    }
}; // struct World

// ----------------------
// Presets (tuned to new model)
// ----------------------
inline Weapon Rifle()        { Weapon w; w.range=10; w.damageMin=8;  w.damageMax=14; w.cooldown=0.6f; w.spreadRad=0.02f; w.accuracyBase=70; w.optimalRange=8; w.falloffPerTile=3; w.critBase=10; return w; }
inline Weapon SMG()          { Weapon w; w.range=7;  w.damageMin=5;  w.damageMax=9;  w.cooldown=0.18f; w.burst=3; w.spreadRad=0.05f; w.accuracyBase=62; w.optimalRange=4; w.falloffPerTile=6; w.critBase=8; return w; }
inline Weapon PredatorBite() { Weapon w; w.range=1;  w.damageMin=6;  w.damageMax=10; w.cooldown=0.9f; w.accuracyBase=85; w.optimalRange=1; w.falloffPerTile=0; w.critBase=5; return w; }
inline Weapon LMG_Suppress() { Weapon w=Rifle(); w.cooldown=0.5f; w.accuracyBase=60; w.suppressionCapable=true; w.critBase=0; return w; }

} // namespace cg::combat
