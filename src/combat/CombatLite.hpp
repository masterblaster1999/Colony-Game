// src/combat/CombatLite.hpp
#pragma once
// Minimal, header-only combat layer for Colony-Game (Phase 1).
// Ranged-only, simple cover, line-of-sight, projectiles, basic AI states.
// Integrates with your grid + A* pathfinding (cg::pf).

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
#endif

#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>
#include <string>
#include <optional>

#include "Pathfinding.hpp" // must expose cg::pf::{GridView,Point,Result,aStar}

namespace cg::combat {

// ----------------------
// Deterministic RNG
// ----------------------
struct RNG {
    uint64_t s=0x9E3779B97F4A7C15ull;
    explicit RNG(uint64_t seed=1) : s(seed?seed:1) {}
    uint32_t nextU32() {
        // xorshift64* (deterministic, fast)
        uint64_t x = s;
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        s = x;
        return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
    }
    float nextFloat() { return (nextU32() & 0xFFFFFF) / float(0x1000000); }
};

// ----------------------
// Core data
// ----------------------
enum class Faction : uint8_t { Colonist=0, Wildlife=1, Raider=2 };

struct Stats {
    int maxHP = 100;
    int hp    = 100;
    int armor = 0; // flat reduction (Phase 1 lite)
};

enum class DamageType : uint8_t { Ballistic=0, Fire=1, Shock=2 };

struct Weapon {
    int   range      = 9;     // tiles
    int   damageMin  = 8;
    int   damageMax  = 14;
    int   burst      = 1;     // bullets per trigger
    float cooldown   = 0.8f;  // seconds between shots
    float projectileSpeed = 16.0f; // tiles/sec
    float spread    = 0.02f;  // radians (small)
    DamageType dtype = DamageType::Ballistic;
};

struct Combatant {
    // Identity & placement
    int      id = -1;
    Faction  team = Faction::Colonist;
    pf::Point pos{0,0};
    int      facing = 0; // -1..1 x, -1..1 y flattened later

    // Stats & gear
    Stats    stats{};
    Weapon   weapon{};

    // Brain
    enum class State : uint8_t { Idle, Patrol, Engage, SeekCover, Retreat, Downed } state = State::Idle;
    float    thinkTimer = 0.f;       // re-eval cadence
    float    atkCooldown = 0.f;      // time until next shot
    int      targetId = -1;          // enemy id
    pf::Point home{0,0};             // patrol anchor
    std::vector<pf::Point> path;     // active path (grid tiles)
    int      pathIdx = 0;

    // Morale-lite
    int      pain = 0;               // grows on hits; can trigger Retreat
};

struct Projectile {
    pf::Point from{};
    pf::Point to{};
    float     t = 0.f;               // 0..1 along segment
    float     speedTilesPerSec = 16.f;
    int       srcId = -1;
    int       dstId = -1;            // optional "locked" target id (-1 = ballistic)
    DamageType dtype = DamageType::Ballistic;
    int       dmg = 1;
    bool      hit = false;           // resolved this frame
    // Pre-sampled line path for grid stepping (optional)
};

// Lightweight occupancy/collision hooks (filled by game)
struct WorldHooks {
    const pf::GridView* grid = nullptr;
    // Is the tile blocking LoS? (e.g., walls, thick trees, tall rocks)
    bool (*opaque)(int x,int y) = nullptr;
    // Is the tile passable? (fallback to grid->walkable)
    bool (*passable)(int x,int y) = nullptr;
    // Optional: cover value [0..100] (0=open field, 100=full cover)
    int  (*coverAt)(int x,int y) = nullptr;
};

// ----------------------
// Utility
// ----------------------
static inline bool inRange(const pf::Point& a, const pf::Point& b, int r) {
    const int dx = (a.x>b.x)?(a.x-b.x):(b.x-a.x);
    const int dy = (a.y>b.y)?(a.y-b.y):(b.y-a.y);
    return (dx+dy) <= r; // manhattan for speed; fine for first pass
}

static inline int manhattan(const pf::Point& a, const pf::Point& b) {
    const int dx = (a.x>b.x)?(a.x-b.x):(b.x-a.x);
    const int dy = (a.y>b.y)?(a.y-b.y):(b.y-a.y);
    return dx+dy;
}

static inline int clampi(int v,int lo,int hi){ return (v<lo)?lo:((v>hi)?hi:v); }

// Bresenham LoS on opaque()
static inline bool lineOfSight(const WorldHooks& w, pf::Point a, pf::Point b) {
    if (!w.grid) return false;
    int x0=a.x, y0=a.y, x1=b.x, y1=b.y;
    int dx = std::abs(x1-x0), dy = -std::abs(y1-y0);
    int sx = (x0<x1)?1:-1, sy = (y0<y1)?1:-1;
    int err = dx+dy;
    auto opq = w.opaque;
    while (true) {
        if (opq && opq(x0,y0)) return (x0==a.x && y0==a.y); // starting tile can be occupied
        if (x0==x1 && y0==y1) return true;
        int e2 = err<<1;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Simple cover score: sample 8 neighbors and existing coverAt()
static inline int coverScore(const WorldHooks& w, int x, int y) {
    int score = 0;
    if (w.coverAt) score += clampi(w.coverAt(x,y),0,100);
    // edge adjacency implies some cover—count opaque neighbors
    static const int d[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto& o: d) {
        const int nx=x+o[0], ny=y+o[1];
        if (w.opaque && w.opaque(nx,ny)) score += 6; // 8*6=48 max from neighbors
    }
    return clampi(score, 0, 100);
}

// ----------------------
// Damage application
// ----------------------
static inline int rollDamage(RNG& rng, const Weapon& w) {
    const int span = std::max(0, w.damageMax - w.damageMin);
    return w.damageMin + int(rng.nextFloat() * float(span+1));
}

static inline int applyArmor(int dmg, int armor) {
    int out = dmg - std::max(0, armor);
    return (out<1)?1:out;
}

// ----------------------
// Combat world
// ----------------------
struct World {
    WorldHooks hooks{};
    RNG        rng{12345};
    std::vector<Combatant>  units;
    std::vector<Projectile> shots;

    // ---- High-level API ----
    int spawnUnit(Faction f, pf::Point at, const Stats& st, const Weapon& wp) {
        int id = (int)units.size();
        Combatant c; c.id=id; c.team=f; c.pos=at; c.stats=st; c.weapon=wp; c.home=at;
        units.push_back(c); return id;
    }

    // Tick all combat (dt in seconds)
    void update(float dt) {
        // Update brains
        for (auto& u : units) {
            if (u.stats.hp <= 0) { u.state = Combatant::State::Downed; continue; }
            u.thinkTimer -= dt;
            u.atkCooldown = std::max(0.f, u.atkCooldown - dt);
            if (u.thinkTimer <= 0.f) {
                think(u);
                u.thinkTimer = 0.25f; // re-evaluate 4hz
            }
            stepAlongPath(u, dt);
            attemptFire(u);
        }

        // Projectiles
        for (auto& p : shots) {
            if (p.hit) continue;
            p.t += (p.speedTilesPerSec * dt) / std::max(0.0001f, float(std::max(1, manhattan(p.from, p.to))));
            if (p.t >= 1.f) {
                onProjectileArrive(p);
                p.hit = true;
            }
        }

        // GC finished shots
        shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Projectile& p){ return p.hit; }), shots.end());
    }

    // ---- AI / Behavior ----
    void think(Combatant& u) {
        // Acquire/validate target
        if (u.targetId < 0 || !isValidTarget(u, u.targetId)) {
            u.targetId = findTarget(u);
        }
        if (u.targetId < 0) { u.state = Combatant::State::Patrol; return; }

        auto& t = units[u.targetId];
        const bool los = lineOfSight(hooks, u.pos, t.pos);
        const bool inR = inRange(u.pos, t.pos, u.weapon.range);

        // Simple morale: retreat when <25% HP or high pain
        if (u.stats.hp < (u.stats.maxHP/4) || u.pain >= 40) {
            u.state = Combatant::State::Retreat;
            pathTo(u, safeRetreatTile(u));
            return;
        }

        if (los && inR) {
            u.state = Combatant::State::Engage;
            // hold position; small reposition toward better cover
            auto better = coverPeekTile(u, t.pos, 6);
            if (better && *better != u.pos) pathTo(u, *better);
            return;
        }

        // If no LoS or not in range -> seek cover lane toward target
        u.state = Combatant::State::SeekCover;
        pf::Point goal = coverAdvanceTile(u, t.pos, 8);
        pathTo(u, goal);
    }

    // Choose an enemy id within 20 tiles (expand later)
    int findTarget(const Combatant& u) const {
        int best=-1, bestScore=INT_MIN;
        for (auto& v : units) {
            if (v.id==u.id || v.stats.hp<=0) continue;
            if (v.team == u.team) continue;
            int d = manhattan(u.pos, v.pos);
            if (d > 20) continue;
            // prefer closer and lower HP enemies
            int score = 100 - d + (v.stats.maxHP - v.stats.hp)/2;
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
    void stepAlongPath(Combatant& u, float dt) {
        (void)dt;
        if (u.pathIdx >= (int)u.path.size()) return;
        // Simple tile pop (let your colony sim's mover do actual tweening/physics)
        if (u.pos.x == u.path[u.pathIdx].x && u.pos.y == u.path[u.pathIdx].y) {
            ++u.pathIdx;
        }
        if (u.pathIdx < (int)u.path.size()) {
            // nudge toward next tile (grid snap)
            u.pos = u.path[u.pathIdx];
        }
    }

    void pathTo(Combatant& u, pf::Point goal) {
        u.path.clear(); u.pathIdx = 0;
        if (!hooks.grid) return;
        std::vector<pf::Point> p;
        auto r = pf::aStar(*hooks.grid, u.pos, goal, p, /*maxExpandedNodes*/ 500);
        if (r == pf::Result::Found && !p.empty()) {
            u.path = std::move(p);
            u.pathIdx = 0;
        }
    }

    // Choose a tile with better cover but closer to target
    std::optional<pf::Point> coverPeekTile(const Combatant& u, const pf::Point& target, int radius) {
        if (!hooks.grid) return std::nullopt;
        int bestS=-9999; pf::Point best{u.pos};
        for (int dy=-radius; dy<=radius; ++dy)
        for (int dx=-radius; dx<=radius; ++dx) {
            int x=u.pos.x+dx, y=u.pos.y+dy;
            if (!hooks.grid->inBounds(x,y) || !(hooks.passable?hooks.passable(x,y):hooks.grid->walkable(x,y))) continue;
            int c = coverScore(hooks, x,y);
            int distT = manhattan({x,y}, target);
            // prefer cover that still keeps us in range
            int inRangeBonus = (distT <= u.weapon.range) ? 20 : 0;
            int score = c*2 + inRangeBonus - distT;
            if (score > bestS && lineOfSight(hooks, {x,y}, target)) { bestS=score; best={x,y}; }
        }
        if (bestS == -9999) return std::nullopt;
        return best;
    }

    // Step toward enemy, preferring tiles that increase cover and LoS potential
    pf::Point coverAdvanceTile(const Combatant& u, const pf::Point& target, int probeRadius) {
        pf::Point best = u.pos; int bestScore = INT_MIN;
        for (int dy=-probeRadius; dy<=probeRadius; ++dy)
        for (int dx=-probeRadius; dx<=probeRadius; ++dx) {
            pf::Point p{u.pos.x+dx, u.pos.y+dy};
            if (!hooks.grid->inBounds(p.x,p.y) || !(hooks.passable?hooks.passable(p.x,p.y):hooks.grid->walkable(p.x,p.y))) continue;
            int c = coverScore(hooks, p.x,p.y);
            int dNow  = manhattan(u.pos, target);
            int dProp = manhattan(p, target);
            int score = c*2 + (dNow - dProp)*3 + (lineOfSight(hooks,p,target)?10:0);
            if (score > bestScore) { bestScore=score; best=p; }
        }
        return best;
    }

    // Fallback retreat tile: run ~8 tiles away toward home or opposite direction
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
        if (!lineOfSight(hooks, u.pos, t.pos)) return;
        if (!inRange(u.pos, t.pos, u.weapon.range)) return;

        u.atkCooldown = u.weapon.cooldown;
        const int shotsFired = std::max(1, u.weapon.burst);
        for (int i=0;i<shotsFired;++i) {
            Projectile p;
            p.from = u.pos; p.to = t.pos;
            p.speedTilesPerSec = u.weapon.projectileSpeed;
            p.srcId = u.id; p.dstId = t.id;
            p.dtype = u.weapon.dtype;
            p.dmg   = applyArmor(rollDamage(rng, u.weapon), t.stats.armor);

            // Small dispersion: jitter target tile
            float spread = u.weapon.spread;
            int jx = (rng.nextFloat()<0.5f? -1: 1) * int(rng.nextFloat()<spread ? 1 : 0);
            int jy = (rng.nextFloat()<0.5f? -1: 1) * int(rng.nextFloat()<spread ? 1 : 0);
            p.to.x = clampi(p.to.x + jx, 0, hooks.grid->w-1);
            p.to.y = clampi(p.to.y + jy, 0, hooks.grid->h-1);

            shots.push_back(p);
        }
    }

    void onProjectileArrive(Projectile& p) {
        if (p.dstId < 0 || p.dstId >= (int)units.size()) return;
        auto& t = units[p.dstId];
        if (t.stats.hp <= 0) return;

        // If no LoS at impact time (moved behind wall), count as miss
        if (!lineOfSight(hooks, p.from, t.pos)) return;

        t.stats.hp -= p.dmg;
        t.pain     += std::min(20, p.dmg);
        if (t.stats.hp <= 0) {
            t.stats.hp = 0;
            t.state = Combatant::State::Downed;
        }
    }

}; // struct World

// ----------------------
// Tiny JSON-ish presets (optional glue you can replace later)
// ----------------------
inline Weapon Rifle()        { Weapon w; w.range=10; w.damageMin=8;  w.damageMax=14; w.cooldown=0.6f; w.spread=0.02f; return w; }
inline Weapon SMG()          { Weapon w; w.range=7;  w.damageMin=5;  w.damageMax=9;  w.cooldown=0.2f; w.burst=3; w.spread=0.05f; return w; }
inline Weapon PredatorBite() { Weapon w; w.range=1;  w.damageMin=6;  w.damageMax=10; w.cooldown=0.9f; return w; }

} // namespace cg::combat
