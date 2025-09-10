// src/ColonySimAI.hpp
// MIT License (c) 2025 YourName or masterblaster1999
//
// Single-file colony-sim core:
// - Grid + tiles (costs, doors, reservations, zones)
// - A* pathfinding (+ optional JPS toggle), terrain costs, dynamic obstacles, path cache
// - Event bus
// - Items, inventories, ground items, stockpile zones
// - Job system (mine/chop/haul/build/farm + craft/cook/research/heal/train/tame/patrol/trade)
// - Colonists (skills, schedules, needs), greedy job assignment
// - Header-only GOAP-ish planner (actions with preconditions/effects → sequences of jobs)
// - Workstations & recipes (sawmill/kitchen/research bench/forge) + auto job spawner
// - Persistence (save/load) & replay trace
// - Debug ASCII overlay renderers
//
// Usage (minimal):
//   #include "ColonySimAI.hpp"
//   colony::World world(96, 64);
//   colony::JobQueue jobs;
//   world.spawnColonist({3,3});
//   jobs.push(colony::Job::Chop({10,7}));
//   while (running) world.update(dt, jobs);
//
// Compile: C++17+ (header-only). No external deps.

#pragma once

// ------------------------------ Includes ------------------------------
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
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
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ------------------------------ Namespace ------------------------------
namespace colony {

// ------------------------------ Config toggles ------------------------------
#ifndef COLONY_SIM_ENABLE_JPS
#define COLONY_SIM_ENABLE_JPS 1     // Jump Point Search pruning (simple impl)
#endif
#ifndef COLONY_SIM_PATHCACHE_MAX
#define COLONY_SIM_PATHCACHE_MAX 4096
#endif
#ifndef COLONY_SIM_DEBUG
#define COLONY_SIM_DEBUG 0
#endif

// ------------------------------ Utilities ------------------------------
struct Vec2i {
    int x = 0, y = 0;
    constexpr Vec2i() = default;
    constexpr Vec2i(int x_, int y_) : x(x_), y(y_) {}
    constexpr bool operator==(const Vec2i& o) const noexcept { return x==o.x && y==o.y; }
    constexpr bool operator!=(const Vec2i& o) const noexcept { return !(*this==o); }
    constexpr bool operator<(const Vec2i& o) const noexcept { return (y<o.y)||((y==o.y)&&(x<o.x)); }
    constexpr Vec2i operator+(const Vec2i& o) const noexcept { return {x+o.x, y+o.y}; }
    constexpr Vec2i operator-(const Vec2i& o) const noexcept { return {x-o.x, y-o.y}; }
    constexpr int manhattan(const Vec2i& o) const noexcept { return std::abs(x-o.x)+std::abs(y-o.y); }
    constexpr int chebyshev(const Vec2i& o) const noexcept { return std::max(std::abs(x-o.x), std::abs(y-o.y)); }
};

struct Hasher {
    size_t operator()(const Vec2i& v) const noexcept {
        uint64_t a = (static_cast<uint64_t>(static_cast<uint32_t>(v.x)) << 32) ^ static_cast<uint32_t>(v.y);
        a ^= (a >> 33); a *= 0xff51afd7ed558ccdULL; a ^= (a >> 33);
        a *= 0xc4ceb9fe1a85ec53ULL; a ^= (a >> 33);
        return static_cast<size_t>(a);
    }
};

template <class T, class U>
struct PairHasher {
    size_t operator()(const std::pair<T,U>& p) const noexcept {
        std::hash<T> ht; std::hash<U> hu;
        uint64_t a = static_cast<uint64_t>(ht(p.first));
        uint64_t b = static_cast<uint64_t>(hu(p.second));
        a ^= (b + 0x9e3779b97f4a7c15ULL + (a<<6) + (a>>2));
        return static_cast<size_t>(a);
    }
};

class StopWatch {
public:
    using clock = std::chrono::high_resolution_clock;
    StopWatch() : start_(clock::now()) {}
    void reset() { start_ = clock::now(); }
    double seconds() const {
        return std::chrono::duration<double>(clock::now()-start_).count();
    }
private:
    clock::time_point start_;
};

class RNG {
public:
    explicit RNG(uint64_t seed = 0xC01oNYULL) : eng_(seed ? seed : std::random_device{}()) {}
    int uniformInt(int a, int b){ std::uniform_int_distribution<int> d(a,b); return d(eng_);}
    double uniform01(){ std::uniform_real_distribution<double> d(0,1); return d(eng_);}
    template<typename It>
    auto pick(It begin, It end){
        auto n = std::distance(begin,end);
        if(n<=0) return begin;
        std::uniform_int_distribution<long long> d(0, n-1);
        std::advance(begin, d(eng_));
        return begin;
    }
    uint64_t seed() const { return seed_; }
private:
    uint64_t seed_ = 0xC01oNYULL;
    std::mt19937_64 eng_;
};

static inline std::string join(const std::vector<std::string>& v, char sep=','){
    std::ostringstream o;
    for(size_t i=0;i<v.size();++i){ if(i) o<<sep; o<<v[i]; }
    return o.str();
}
static inline std::vector<std::string> split(const std::string& s, char sep=' '){
    std::vector<std::string> out; std::string cur;
    for(char c: s){ if(c==sep){ if(!cur.empty()) out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if(!cur.empty()) out.push_back(cur);
    return out;
}

// ------------------------------ Items & Inventory ------------------------------

enum class ItemId : uint16_t {
    None=0, Log, Plank, Ore, Ingot, RawFood, Meal, Herb, Medicine, Paper, ResearchData, Tool, Seed, Crop, Stone
};

static inline const char* itemName(ItemId id){
    switch(id){
        case ItemId::Log: return "Log"; case ItemId::Plank: return "Plank";
        case ItemId::Ore: return "Ore"; case ItemId::Ingot: return "Ingot";
        case ItemId::RawFood: return "RawFood"; case ItemId::Meal: return "Meal";
        case ItemId::Herb: return "Herb"; case ItemId::Medicine: return "Medicine";
        case ItemId::Paper: return "Paper"; case ItemId::ResearchData: return "ResearchData";
        case ItemId::Tool: return "Tool"; case ItemId::Seed: return "Seed";
        case ItemId::Crop: return "Crop"; case ItemId::Stone: return "Stone";
        default: return "None";
    }
}

struct ItemStack {
    ItemId id = ItemId::None;
    int qty = 0;
    bool empty() const { return id==ItemId::None || qty<=0; }
};

class Inventory {
public:
    explicit Inventory(int cap=16) : cap_(cap) {}
    int capacity() const { return cap_; }
    int count(ItemId id) const {
        int n=0; for(auto& s: slots_) if(s.id==id) n+=s.qty; return n;
    }
    int total() const { int n=0; for(auto&s:slots_) n+=s.qty; return n; }
    bool has(ItemId id, int qty) const { return count(id) >= qty; }

    // add up to qty; returns leftover
    int add(ItemId id, int qty){
        if(id==ItemId::None || qty<=0) return 0;
        // try stack
        for(auto& s: slots_) if(s.id==id && s.qty>0){ s.qty += qty; return 0; }
        // add new slot
        if((int)slots_.size()<cap_){ slots_.push_back({id, qty}); return 0; }
        return qty; // no space, all leftover
    }
    // remove up to qty; returns removed
    int remove(ItemId id, int qty){
        int need=qty, got=0;
        for(auto& s: slots_){
            if(s.id!=id || s.qty<=0) continue;
            int take = std::min(s.qty, need);
            s.qty -= take; got += take; need -= take;
            if(s.qty<=0){ s.id=ItemId::None; s.qty=0; }
            if(need<=0) break;
        }
        // cleanup empties
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const ItemStack& s){ return s.empty(); }), slots_.end());
        return got;
    }
    const std::vector<ItemStack>& slots() const { return slots_; }
    std::vector<ItemStack>& slots() { return slots_; }
private:
    int cap_;
    std::vector<ItemStack> slots_;
};

// ------------------------------ Grid & Tiles ------------------------------

struct Tile {
    bool walkable = true;
    bool reserved = false;
    uint8_t material = 0;   // 0 soil, 1 tree, 2 rock, 3 water, 4 crop
    uint8_t terrain = 0;    // user-defined terrain kind (0 default)
    bool isDoor = false;
    bool doorOpen = false;
    uint16_t zoneId = 0;    // stockpile or room id
    uint16_t moveCost = 10; // base move cost (>=10)
};

class Grid {
public:
    Grid(int w, int h) : w_(w), h_(h), tiles_(w*h) {}
    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }

    bool inBounds(const Vec2i& p) const noexcept {
        return (p.x>=0 && p.y>=0 && p.x<w_ && p.y<h_);
    }
    Tile& at(const Vec2i& p){ return tiles_[idx(p)]; }
    const Tile& at(const Vec2i& p) const { return tiles_[idx(p)]; }

    bool walkable(const Vec2i& p) const {
        if(!inBounds(p)) return false;
        const auto& t = at(p);
        return t.walkable && !t.reserved && (!t.isDoor || t.doorOpen); // closed door treated as blocked for path; open on approach
    }
    bool occupiable(const Vec2i& p) const {
        if(!inBounds(p)) return false;
        const auto& t = at(p);
        return t.walkable && !t.reserved; // doors considered occupiable (agent can open on arrival)
    }

    int moveCost(const Vec2i& p) const {
        if(!inBounds(p)) return 1000000;
        const auto& t = at(p);
        int c = t.moveCost;
        if(t.terrain==3) c += 15;           // e.g., shallow water penalty
        if(t.material==4) c += 5;           // crops slow a bit
        if(t.isDoor && !t.doorOpen) c += 25; // opening door cost
        return c;
    }

    std::array<Vec2i,8> neighbors8(const Vec2i& p) const {
        return {Vec2i{p.x+1,p.y}, {p.x-1,p.y}, {p.x,p.y+1}, {p.x,p.y-1},
                {p.x+1,p.y+1},{p.x+1,p.y-1},{p.x-1,p.y+1},{p.x-1,p.y-1}};
    }
    std::array<Vec2i,4> neighbors4(const Vec2i& p) const {
        return {Vec2i{p.x+1,p.y}, {p.x-1,p.y}, {p.x,p.y+1}, {p.x,p.y-1}};
    }

    void setObstacle(const Vec2i& p, bool blocked=true){
        if(!inBounds(p)) return; tiles_[idx(p)].walkable = !blocked; bumpStamp();
    }
    void setMaterial(const Vec2i& p, uint8_t m){
        if(!inBounds(p)) return; tiles_[idx(p)].material = m; bumpStamp();
    }
    void setTerrainCost(const Vec2i& p, uint16_t c){
        if(!inBounds(p)) return; tiles_[idx(p)].moveCost = std::max<uint16_t>(10, c); bumpStamp();
    }
    void setZoneId(const Vec2i& p, uint16_t id){
        if(!inBounds(p)) return; tiles_[idx(p)].zoneId = id; bumpStamp();
    }
    void setDoor(const Vec2i& p, bool isDoor, bool open=false){
        if(!inBounds(p)) return; auto& t=tiles_[idx(p)]; t.isDoor=isDoor; t.doorOpen=open; bumpStamp();
    }
    void openDoor(const Vec2i& p){ if(!inBounds(p)) return; tiles_[idx(p)].doorOpen=true; bumpStamp(); }
    void closeDoor(const Vec2i& p){ if(!inBounds(p)) return; tiles_[idx(p)].doorOpen=false; bumpStamp(); }

    void reserve(const Vec2i& p){ if(inBounds(p)){ tiles_[idx(p)].reserved = true; bumpStamp(); } }
    void unreserve(const Vec2i& p){ if(inBounds(p)){ tiles_[idx(p)].reserved = false; bumpStamp(); } }

    uint64_t stamp() const { return stamp_; }

private:
    size_t idx(const Vec2i& p) const { return static_cast<size_t>(p.y*w_ + p.x); }
    void bumpStamp(){ ++stamp_; }
    int w_{}, h_{};
    std::vector<Tile> tiles_;
    uint64_t stamp_ = 1; // increments on structural change
};

// ------------------------------ Stockpiles & Ground Items ------------------------------

struct StockpileZone {
    uint16_t id = 0;
    std::unordered_set<Vec2i, Hasher> cells;
    std::unordered_set<ItemId> allow;   // empty = allow all
    int priority = 0;                   // 0 normal, higher first
};

class Stockpiles {
public:
    uint16_t createZone(int priority=0){
        uint16_t id = ++nextId_;
        zones_.push_back({id,{}, {}, priority});
        return id;
    }
    void addCell(uint16_t id, const Vec2i& p){
        if(auto* z = find(id)) z->cells.insert(p);
    }
    void setAllow(uint16_t id, const std::vector<ItemId>& items){
        if(auto* z = find(id)){ z->allow.clear(); z->allow.insert(items.begin(), items.end()); }
    }
    std::optional<uint16_t> zoneIdAt(const Vec2i& p) const {
        for (auto& z: zones_) if(z.cells.count(p)) return z.id; return std::nullopt;
    }
    const std::vector<StockpileZone>& zones() const { return zones_; }

    // choose best cell for item; naive: any empty cell in highest-priority matching zone
    std::optional<Vec2i> pickDestination(ItemId item, const Vec2i& near) const {
        const StockpileZone* bestZ = nullptr;
        for (auto& z: zones_) if(z.allow.empty() || z.allow.count(item)){
            if(!bestZ || z.priority>bestZ->priority) bestZ=&z;
        }
        if(!bestZ) return std::nullopt;
        // pick by nearest to 'near'
        const Vec2i* bestCell=nullptr; int bestDist=INT_MAX;
        for (const auto& c: bestZ->cells){
            int d = c.manhattan(near);
            if(d<bestDist){ bestDist=d; bestCell=&c; }
        }
        if(bestCell) return *bestCell; return std::nullopt;
    }
private:
    StockpileZone* find(uint16_t id){
        for(auto& z: zones_) if(z.id==id) return &z; return nullptr;
    }
    uint16_t nextId_ = 0;
    std::vector<StockpileZone> zones_;
};

class GroundItems {
public:
    void drop(const Vec2i& at, ItemId id, int qty){
        if(qty<=0 || id==ItemId::None) return;
        auto& v = items_[at];
        // stack same id
        for(auto& s: v){ if(s.id==id){ s.qty+=qty; return; } }
        v.push_back({id, qty});
    }
    // take up to qty; returns removed
    int take(const Vec2i& at, ItemId id, int qty){
        if(qty<=0) return 0;
        auto it = items_.find(at);
        if(it==items_.end()) return 0;
        int need=qty, got=0;
        auto& v = it->second;
        for(auto& s: v){
            if(s.id!=id) continue;
            int take = std::min(s.qty, need);
            s.qty -= take; got += take; need -= take;
            if(need<=0) break;
        }
        v.erase(std::remove_if(v.begin(), v.end(), [](const ItemStack& s){ return s.empty(); }), v.end());
        if(v.empty()) items_.erase(at);
        return got;
    }
    const std::vector<ItemStack>* at(const Vec2i& p) const {
        auto it = items_.find(p);
        if(it==items_.end()) return nullptr; return &it->second;
    }
    std::unordered_map<Vec2i, std::vector<ItemStack>, Hasher>& mut(){ return items_; }
    const std::unordered_map<Vec2i, std::vector<ItemStack>, Hasher>& all() const { return items_; }
private:
    std::unordered_map<Vec2i, std::vector<ItemStack>, Hasher> items_;
};

// ------------------------------ Jobs ------------------------------

enum class JobKind : uint16_t {
    None=0,
    MoveTo, Chop, Mine, Haul, Build, Farm,
    Craft, Cook, Research, Heal, Train, Tame, Patrol, Trade, Deliver,
    JobKind_Count
};
static inline const char* jobName(JobKind k){
    switch(k){
        case JobKind::MoveTo: return "MoveTo"; case JobKind::Chop: return "Chop";
        case JobKind::Mine: return "Mine"; case JobKind::Haul: return "Haul";
        case JobKind::Build: return "Build"; case JobKind::Farm: return "Farm";
        case JobKind::Craft: return "Craft"; case JobKind::Cook: return "Cook";
        case JobKind::Research: return "Research"; case JobKind::Heal: return "Heal";
        case JobKind::Train: return "Train"; case JobKind::Tame: return "Tame";
        case JobKind::Patrol: return "Patrol"; case JobKind::Trade: return "Trade";
        case JobKind::Deliver: return "Deliver";
        default: return "None";
    }
}

struct Job {
    JobKind kind = JobKind::None;
    Vec2i target{};      // primary tile
    Vec2i aux{};         // secondary tile (e.g., haul destination)
    int   workTicks = 60;
    // Item payloads (for haul/craft/cook/trade)
    ItemId item = ItemId::None;
    int    amount = 0;

    // Factories
    static Job MoveTo(Vec2i t){ return Job{JobKind::MoveTo, t, {}, 0, ItemId::None, 0}; }
    static Job Chop(Vec2i t, int ticks=120){ return Job{JobKind::Chop, t, {}, ticks, ItemId::None, 0}; }
    static Job Mine(Vec2i t, int ticks=160){ return Job{JobKind::Mine, t, {}, ticks, ItemId::None, 0}; }
    static Job Haul(Vec2i from, Vec2i to, ItemId id, int qty){ return Job{JobKind::Haul, from, to, 30, id, qty}; }
    static Job Build(Vec2i t, int ticks=200){ return Job{JobKind::Build, t, {}, ticks, ItemId::None, 0}; }
    static Job Farm(Vec2i t, int ticks=100){ return Job{JobKind::Farm, t, {}, ticks, ItemId::None, 0}; }
    static Job Craft(Vec2i ws, int ticks, ItemId out=ItemId::None, int qty=0){ return Job{JobKind::Craft, ws, {}, ticks, out, qty}; }
    static Job Cook(Vec2i ws, int ticks, ItemId out=ItemId::Meal, int qty=1){ return Job{JobKind::Cook, ws, {}, ticks, out, qty}; }
    static Job Research(Vec2i ws, int ticks=200){ return Job{JobKind::Research, ws, {}, ticks, ItemId::ResearchData, 1}; }
    static Job Patrol(Vec2i a, Vec2i b, int ticks=0){ return Job{JobKind::Patrol, a, b, ticks, ItemId::None, 0}; }
    static Job Deliver(Vec2i from, Vec2i to, ItemId id, int qty){ return Job{JobKind::Deliver, from, to, 10, id, qty}; }
};

struct JobPriority {
    int p = 0; // higher = sooner
    uint64_t createdOrder = 0;  // FIFO within equal priority
};

// Replayable events
enum class EventKind : uint8_t {
    JobStarted, JobCompleted, PathFound, PathFailed, TileChanged, Debug
};
struct Event {
    EventKind kind;
    Vec2i a{}, b{};
    int   agentId = -1;
    JobKind job{};
    std::string msg;
};

// ------------------------------ Event Bus ------------------------------
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    int subscribe(EventKind k, Handler h) {
        int id = ++sid_;
        subs_[k].emplace_back(id, std::move(h));
        return id;
    }
    void unsubscribeAll() { subs_.clear(); }
    void publish(const Event& e) {
        replay_.push_back({stamp_++, e});
        if(auto it=subs_.find(e.kind); it!=subs_.end())
            for (auto& [_,h]: it->second) h(e);
    }
    void clearReplay(){ replay_.clear(); }
    struct ReplayEntry{ uint64_t t; Event e; };
    const std::vector<ReplayEntry>& replay() const { return replay_; }
private:
    int sid_ = 0;
    uint64_t stamp_ = 0;
    std::unordered_map<EventKind, std::vector<std::pair<int,Handler>>> subs_;
    std::vector<ReplayEntry> replay_;
};

// ------------------------------ Pathfinding ------------------------------
struct Path { std::vector<Vec2i> points; bool empty() const { return points.empty(); } void clear(){ points.clear(); } };

struct PathCacheEntry {
    std::vector<Vec2i> pts;
    uint64_t gridStamp = 0;
    uint64_t lastUsed = 0;
};

class Pathfinder {
public:
    explicit Pathfinder(const Grid* g) : g_(g) {}
    void setDiagonal(bool allow){ allowDiag_ = allow; }
    void setMaxSearch(int nodes){ maxSearch_ = nodes; }
    void setDynamicBlocker(const std::function<bool(const Vec2i&)>& f){ isBlocked_ = f; }

    // Returns optimal (or best-effort) path. Caches results until grid stamp changes.
    Path find(const Vec2i& start, const Vec2i& goal, uint64_t timeStamp=0) {
        Path path;
        if(!g_->inBounds(start) || !g_->inBounds(goal)){
            return path;
        }
        if(start==goal){ path.points.push_back(start); return path; }

        // Path cache lookup
        auto key = std::make_pair(start, goal);
        auto it = cache_.find(key);
        if(it!=cache_.end() && it->second.gridStamp==g_->stamp()){
            it->second.lastUsed = timeStamp;
            path.points = it->second.pts; return path;
        }

        // A* with optional simple JPS pruning
        struct Node{ Vec2i p; int g=0, f=0; Vec2i parent{-999,-999}; };
        auto h = [&](const Vec2i& a){ return (allowDiag_ ? a.chebyshev(goal) : a.manhattan(goal)) * 10; };

        struct PQE{ int f; uint64_t id; Vec2i p; };
        struct PQCmp{ bool operator()(const PQE& a, const PQE& b) const { return a.f > b.f; }};
        std::priority_queue<PQE, std::vector<PQE>, PQCmp> open;
        std::unordered_map<Vec2i, Node, Hasher> all;
        std::unordered_map<Vec2i, int, Hasher> openG;

        auto passable = [&](const Vec2i& p)->bool{
            if(isBlocked_ && isBlocked_(p) && p!=goal) return false;
            return g_->walkable(p) || p==goal;
        };

        auto pushOpen = [&](const Vec2i& p, int g, const Vec2i& parent){
            Node n{p,g,g + h(p), parent};
            all[p] = n;
            openG[p] = g;
            open.push(PQE{n.f, counter_++, p});
        };

        pushOpen(start, 0, {-999,-999});

        int expanded = 0;
        while(!open.empty()){
            auto cur = open.top(); open.pop();
            auto itn = all.find(cur.p);
            if(itn==all.end()) continue;
            Node node = itn->second;
            if (node.f != cur.f) continue;

            if (++expanded > maxSearch_) break;
            if (node.p == goal){
                // Reconstruct
                std::vector<Vec2i> rev;
                Vec2i p = node.p;
                while(p.x!=-999){
                    rev.push_back(p);
                    p = all[p].parent;
                }
                std::reverse(rev.begin(), rev.end());
                path.points = std::move(rev);
                smooth(path, passable);
                // store cache
                ensureCacheBudget();
                cache_[key] = PathCacheEntry{path.points, g_->stamp(), timeStamp};
                return path;
            }

            auto visitNeighbor = [&](const Vec2i& np, int stepCost){
                if(!g_->inBounds(np) || !passable(np)) return;
                // Avoid cutting corners
                if (allowDiag_ && np.x!=node.p.x && np.y!=node.p.y){
                    Vec2i a{np.x, node.p.y}, b{node.p.x, np.y};
                    if(!passable(a) || !passable(b)) return;
                }
                int cost = stepCost + g_->moveCost(np);
                int tentative = node.g + cost;
                auto og = openG.find(np);
                if(og==openG.end() || tentative < og->second) pushOpen(np, tentative, node.p);
            };

#if COLONY_SIM_ENABLE_JPS
            // Simple pruning: prefer straight jumps when possible (not full JPS but reduces branching)
            static const int DIRS8[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            for(int d=0; d<(allowDiag_?8:4); ++d){
                Vec2i dir{DIRS8[d][0], DIRS8[d][1]};
                Vec2i np = node.p + dir;
                if(!g_->inBounds(np) || !passable(np)) continue;
                // jump until forced neighbor or blocked
                int step = (dir.x!=0 && dir.y!=0) ? 14 : 10;
                Vec2i curp = np;
                while(true){
                    if(!g_->inBounds(curp) || !passable(curp)) break;
                    // forced neighbor detection (very simplified)
                    if(allowDiag_){
                        bool forced = false;
                        if(dir.x!=0 && dir.y!=0){
                            Vec2i a{curp.x-dir.x, curp.y};
                            Vec2i b{curp.x, curp.y-dir.y};
                            if(!passable(a) || !passable(b)) forced = true;
                        }
                        if(forced || curp==goal){
                            visitNeighbor(curp, step);
                            break;
                        }
                    } else {
                        // 4-dir: check side blocks
                        if(curp==goal){ visitNeighbor(curp, step); break; }
                    }
                    // continue jump
                    curp = curp + dir;
                    step += (dir.x!=0 && dir.y!=0) ? 14 : 10;
                }
            }
#else
            // Vanilla neighbors
            const auto neigh4 = g_->neighbors4(node.p);
            const auto neigh8 = g_->neighbors8(node.p);
            const auto& neigh = allowDiag_ ? neigh8 : neigh4;
            for(const auto& np: neigh){
                int step = (np.x!=node.p.x && np.y!=node.p.y) ? 14 : 10;
                visitNeighbor(np, step);
            }
#endif
        }
        return path; // empty if failed
    }

    void clearCache(){ cache_.clear(); }

private:
    template<typename PassableFn>
    void smooth(Path& p, PassableFn passable) const {
        if(p.points.size()<3) return;
        std::vector<Vec2i> out;
        out.push_back(p.points.front());
        size_t k = 2;
        while(k < p.points.size()){
            Vec2i a = out.back();
            Vec2i b = p.points[k];
            if(hasLineOfSight(a,b, passable)){
                // skip middle point
            } else {
                out.push_back(p.points[k-1]);
            }
            ++k;
        }
        out.push_back(p.points.back());
        p.points.swap(out);
    }
    template<typename PassableFn>
    bool hasLineOfSight(Vec2i a, Vec2i b, PassableFn passable) const {
        int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
        int sx = (a.x < b.x) ? 1 : -1;
        int sy = (a.y < b.y) ? 1 : -1;
        int err = dx - dy;
        while(true){
            if(!passable(a)) return false;
            if(a==b) break;
            int e2 = err*2;
            if(e2 > -dy){ err -= dy; a.x += sx; }
            if(e2 <  dx){ err += dx; a.y += sy; }
        }
        return true;
    }
    void ensureCacheBudget(){
        if(cache_.size()<COLONY_SIM_PATHCACHE_MAX) return;
        // evict least-recently-used ~10%
        std::vector<std::pair<std::pair<Vec2i,Vec2i>, PathCacheEntry>> vec(cache_.begin(), cache_.end());
        std::nth_element(vec.begin(), vec.begin()+vec.size()/10, vec.end(),
            [](auto& a, auto& b){ return a.second.lastUsed < b.second.lastUsed; });
        for(size_t i=0;i<vec.size()/10;++i) cache_.erase(vec[i].first);
    }

    const Grid* g_;
    bool allowDiag_ = true;
    int maxSearch_ = 20000;
    uint64_t counter_ = 0;

    std::function<bool(const Vec2i&)> isBlocked_;
    std::unordered_map<std::pair<Vec2i,Vec2i>, PathCacheEntry, PairHasher<Vec2i,Vec2i>> cache_;
};

// ------------------------------ Agents (Colonists) ------------------------------

enum class AgentState : uint8_t { Idle, AcquireJob, Plan, Navigate, Work, Deliver, Sleep, Leisure };

struct Schedule {
    enum Block { Work, Sleep, Leisure } perHour[24];
    Schedule(){
        for(int h=0;h<24;++h) perHour[h] = Work;
        for(int h=0;h<6;++h) perHour[h] = Sleep; // 00-05
        perHour[6] = Leisure;
        for(int h=7; h<=18; ++h) perHour[h] = Work; // 07-18
        for(int h=19; h<=21; ++h) perHour[h] = Leisure;
        perHour[22] = Work;
        perHour[23] = Sleep;
    }
    Block blockAtMinute(int minuteOfDay) const {
        int h = (minuteOfDay/60)%24;
        return perHour[h];
    }
};

struct Skills {
    // simple per job skill [0..10]
    std::array<int, static_cast<size_t>(JobKind::JobKind_Count)> level{};
    int operator[](JobKind k) const { return level[static_cast<size_t>(k)]; }
    int& operator[](JobKind k) { return level[static_cast<size_t>(k)]; }
};

struct Agent {
    int id = -1;
    Vec2i pos{};
    AgentState state = AgentState::Idle;
    std::optional<Job> job;
    std::deque<Job> plan;     // upcoming jobs (GOAP / scheduler)
    Path path;
    int workLeft = 0;
    Vec2i carryTo{};          // for haul destination
    Inventory inv{8};

    // Needs (0..100; higher hunger = worse)
    int hunger = 20; // grows toward 100
    int rest = 80;   // decays toward 0
    int morale = 70;

    // Preferences
    Schedule schedule;
    Skills skills;
    int tilesPerTick = 1;
};

// ------------------------------ Workstations & Recipes ------------------------------

enum class BuildingType : uint8_t { None=0, Sawmill, Kitchen, ResearchBench, Forge };

struct Recipe {
    std::string name;
    std::vector<ItemStack> inputs;
    std::vector<ItemStack> outputs;
    int workTicks = 150;
    JobKind jobKind = JobKind::Craft;
};

struct Workstation {
    BuildingType type = BuildingType::None;
    Vec2i pos{};
    std::vector<Recipe> recipes;
    bool busy=false;
    // local buffers (very lightweight)
    std::vector<ItemStack> inbuf, outbuf;
};

class BuildingManager {
public:
    int add(BuildingType t, Vec2i p){
        Workstation w; w.type=t; w.pos=p;
        switch(t){
            case BuildingType::Sawmill:
                w.recipes.push_back({"Planks", {{ItemId::Log,1}}, {{ItemId::Plank,1}}, 120, JobKind::Craft});
                break;
            case BuildingType::Kitchen:
                w.recipes.push_back({"CookMeal", {{ItemId::RawFood,1}}, {{ItemId::Meal,1}}, 140, JobKind::Cook});
                break;
            case BuildingType::ResearchBench:
                w.recipes.push_back({"Research", {{ItemId::Paper,1}}, {{ItemId::ResearchData,1}}, 200, JobKind::Research});
                break;
            case BuildingType::Forge:
                w.recipes.push_back({"Smelt", {{ItemId::Ore,1}}, {{ItemId::Ingot,1}}, 180, JobKind::Craft});
                break;
            default: break;
        }
        ws_.push_back(std::move(w));
        return (int)ws_.size()-1;
    }
    std::vector<Workstation>& all(){ return ws_; }
    const std::vector<Workstation>& all() const { return ws_; }
    Workstation* nearest(BuildingType t, const Vec2i& from){
        Workstation* best=nullptr; int bd=INT_MAX;
        for(auto& w: ws_) if(w.type==t){
            int d=from.manhattan(w.pos); if(d<bd){ bd=d; best=&w; }
        }
        return best;
    }
private:
    std::vector<Workstation> ws_;
};

// ------------------------------ GOAP-lite Actions ------------------------------
// Each action can generate a sequence of concrete Jobs for the agent to execute.
// Preconditions are checked against agent + world. Effects mutate an internal "needs" projection.

struct World; // fwd

struct WorldState {
    int hunger, rest, morale;
    bool hasMeal = false;
};

struct GoapAction {
    std::string name;
    int cost = 1;
    std::function<bool(const Agent&, const World& , const WorldState&)> pre;
    std::function<void(WorldState&)> eff;
    std::function<std::vector<Job>(Agent&, World&)> makeJobs;
};

class ActionLibrary {
public:
    void add(GoapAction a){ lib_.push_back(std::move(a)); }
    const std::vector<GoapAction>& all() const { return lib_; }
private:
    std::vector<GoapAction> lib_;
};

// ------------------------------ Job Queue (with agent-aware selection) ------------------------------
class JobQueue {
public:
    void push(const Job& j, int priority=0){
        queue_.push(Entry{counter_++, {priority, seq_++}, j});
    }
    bool empty() const { return queue_.empty(); }
    size_t size() const { return queue_.size(); }

    // Pop best-scoring job for agent among top K entries.
    std::optional<Job> popBestFor(const Agent& agent, const Grid& grid, int minuteOfDay, int K=12){
        if(queue_.empty()) return std::nullopt;
        std::vector<Entry> tmp;
        for(int i=0; i<K && !queue_.empty(); ++i){ tmp.push_back(queue_.top()); queue_.pop(); }
        int bestIdx=-1; double bestScore=-1e18;
        for(size_t i=0;i<tmp.size();++i){
            const auto& e = tmp[i];
            double s = score(e, agent, grid, minuteOfDay);
            if(s>bestScore){ bestScore=s; bestIdx=(int)i; }
        }
        std::optional<Job> res;
        for(size_t i=0;i<tmp.size();++i){
            if((int)i==bestIdx){ res = tmp[i].job; }
            else queue_.push(tmp[i]);
        }
        return res;
    }

private:
    struct Entry {
        uint64_t id;
        JobPriority pri;
        Job job;
    };
    struct Cmp {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.pri.p != b.pri.p) return a.pri.p < b.pri.p; // max-heap
            return a.pri.createdOrder > b.pri.createdOrder;   // FIFO
        }
    };

    double score(const Entry& e, const Agent& a, const Grid& grid, int minuteOfDay) const {
        // base: priority
        double s = e.pri.p * 10.0;
        // closer is better
        int dist = a.pos.manhattan(e.job.target);
        s -= dist * 0.5;
        // skill bonus
        s += a.skills[e.job.kind] * 2.0;
        // schedule: slight penalty when not in Work block
        Schedule::Block b = a.schedule.blockAtMinute(minuteOfDay);
        if(b!=Schedule::Work) s -= 10.0;
        // needs: if food job and hungry, bump
        if((e.job.kind==JobKind::Cook || e.job.kind==JobKind::Farm) && a.hunger>60) s += 8.0;
        return s;
    }

    std::priority_queue<Entry, std::vector<Entry>, Cmp> queue_;
    uint64_t counter_ = 0;
    uint64_t seq_ = 0;
};

// ------------------------------ World Orchestrator ------------------------------

class World {
public:
    World(int w, int h)
    : grid_(w,h), pf_(&grid_) {
        // Seed demo terrain & materials
        RNG rng;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++){
            if(rng.uniform01() < 0.02) grid_.setObstacle({x,y}, true);
            double r = rng.uniform01();
            if(r < 0.05) grid_.setMaterial({x,y}, 1);      // tree
            else if(r < 0.08) grid_.setMaterial({x,y}, 2); // rock
            grid_.setTerrainCost({x,y}, 10);
        }
        // A few stations
        buildings_.add(BuildingType::Sawmill, {w/2-3, h/2});
        buildings_.add(BuildingType::Kitchen, {w/2, h/2});
        buildings_.add(BuildingType::ResearchBench, {w/2+3, h/2});

        // Default action library
        buildActionLibrary();
        pf_.setDynamicBlocker([this](const Vec2i& p){
            return occupied_.count(p)>0;
        });
    }

    // --- Public API ---
    int spawnColonist(const Vec2i& p){
        int id = nextAgentId_++;
        Agent a; a.id=id; a.pos=p;
        for(size_t i=0;i<a.skills.level.size();++i) a.skills.level[i]=1;
        a.skills[JobKind::Chop]=3; a.skills[JobKind::Mine]=2; a.skills[JobKind::Craft]=2; a.skills[JobKind::Cook]=1;
        agents_.push_back(a);
        return id;
    }

    Grid& grid(){ return grid_; }
    const Grid& grid() const { return grid_; }
    EventBus& events(){ return bus_; }
    Stockpiles& stockpiles(){ return stockpiles_; }
    GroundItems& ground(){ return ground_; }
    BuildingManager& buildings(){ return buildings_; }
    Pathfinder& pathfinder(){ return pf_; }

    // Convenience: add a stockpile area
    uint16_t addStockpileRect(const Vec2i& a, const Vec2i& b, int priority, const std::vector<ItemId>& allow){
        uint16_t id = stockpiles_.createZone(priority);
        for(int y=std::min(a.y,b.y); y<=std::max(a.y,b.y); ++y)
            for(int x=std::min(a.x,b.x); x<=std::max(a.x,b.x); ++x){
                stockpiles_.addCell(id, {x,y});
                grid_.setZoneId({x,y}, id);
            }
        stockpiles_.setAllow(id, allow);
        return id;
    }

    // Drop ground items
    void drop(const Vec2i& p, ItemId id, int qty){ ground_.drop(p, id, qty); }

    // Save/Load
    bool save(const std::string& file){ std::ofstream f(file); if(!f) return false; saveTo(f); return true; }
    bool load(const std::string& file){ std::ifstream f(file); if(!f) return false; loadFrom(f); return true; }

    // ASCII overlay for debugging
    std::string renderAscii(int x0=0,int y0=0,int w=-1,int h=-1) const {
        if(w<0) w=grid_.width(); if(h<0) h=grid_.height();
        std::ostringstream o;
        std::unordered_set<Vec2i,Hasher> agentPos;
        for(auto& a: agents_) agentPos.insert(a.pos);
        for(int y=y0; y<y0+h && y<grid_.height(); ++y){
            for(int x=x0; x<x0+w && x<grid_.width(); ++x){
                Vec2i p{x,y};
                char c='.';
                const auto& t = grid_.at(p);
                if(!t.walkable) c='#';
                else if(agentPos.count(p)) c='@';
                else if(t.isDoor) c = t.doorOpen?'/':'|';
                else if(t.material==1) c='T';
                else if(t.material==2) c='R';
                else if(t.material==4) c='*';
                else {
                    auto it = ground_.all().find(p);
                    if(it!=ground_.all().end() && !it->second.empty()) c='i';
                    else if(t.zoneId) c='+';
                }
                o<<c;
            }
            o<<"\n";
        }
        return o.str();
    }

    // --- Main tick ---
    void update(double dt, JobQueue& externalJobs){
        timeAcc_ += dt;
        if(timeAcc_ >= tickSeconds_){
            timeAcc_ -= tickSeconds_;
            tick(externalJobs);
        }
    }

private:
    // =====================================================================
    // Core Tick
    // =====================================================================
    void tick(JobQueue& externalJobs){
        ++tickCount_;
        minuteOfDay_ = (minuteOfDay_ + 1) % 1440; // 1 minute per tick

        // Recompute occupied tiles set for pathfinder dynamic blockers
        occupied_.clear();
        for (auto& a : agents_) occupied_.insert(a.pos);

        // Auto-spawn jobs from stations if needed
        autoEnqueueWorkstationJobs(externalJobs);

        // Advance agents
        for(auto& a : agents_){
            // Needs progression
            a.hunger = std::min(100, a.hunger + 1);
            a.rest   = std::max(0, a.rest - 1);
            if(a.state==AgentState::Sleep) a.rest = std::min(100, a.rest + 3);
            if(a.state==AgentState::Leisure) a.morale = std::min(100, a.morale + 1);

            switch(a.state){
                case AgentState::Idle: handleIdle(a); break;
                case AgentState::AcquireJob: handleAcquireJob(a, externalJobs); break;
                case AgentState::Plan: handlePlan(a); break;
                case AgentState::Navigate: handleNavigate(a); break;
                case AgentState::Work: handleWork(a); break;
                case AgentState::Deliver: handleDeliver(a); break;
                case AgentState::Sleep: handleSleep(a); break;
                case AgentState::Leisure: handleLeisure(a); break;
            }
        }
    }

    // =====================================================================
    // State handlers
    // =====================================================================
    void handleIdle(Agent& a){
        // schedule-based idle behavior
        auto block = a.schedule.blockAtMinute(minuteOfDay_);
        if(block==Schedule::Sleep && a.rest<95){
            a.state = AgentState::Sleep;
            return;
        }
        if(block==Schedule::Leisure){
            a.state = AgentState::Leisure;
            return;
        }
        a.state = AgentState::AcquireJob;
    }

    void handleAcquireJob(Agent& a, JobQueue& jq){
        // if agent has a plan, use it
        if(!a.plan.empty()){
            a.job = a.plan.front(); a.plan.pop_front();
            beginJob(a);
            return;
        }

        // GOAP: if hungry, try to plan cook+eat; if low rest but schedule not sleep, skip
        if(a.hunger > 70){
            a.state = AgentState::Plan; return;
        }

        // Pull best job from queue
        if(jq.empty()){ a.state = AgentState::Idle; return; }
        auto j = jq.popBestFor(a, grid_, minuteOfDay_);
        if(!j){ a.state = AgentState::Idle; return; }

        a.job = *j;
        beginJob(a);
    }

    void handlePlan(Agent& a){
        WorldState st{a.hunger, a.rest, a.morale, a.inv.has(ItemId::Meal,1)};
        // find first applicable action (greedy)
        for(const auto& act : actions_.all()){
            if(act.pre && act.pre(a, *this, st)){
                if(act.eff) act.eff(st);
                if(act.makeJobs){
                    auto js = act.makeJobs(a, *this);
                    for(auto& j: js) a.plan.push_back(j);
                    break;
                }
            }
        }
        a.state = AgentState::AcquireJob;
    }

    void handleNavigate(Agent& a){
        if(a.path.points.empty()){
            // arrived
            if(a.job && a.pos == a.job->target){
                // Door opening if needed
                auto& t = grid_.at(a.pos);
                if(t.isDoor && !t.doorOpen){ grid_.openDoor(a.pos); bus_.publish({EventKind::TileChanged, a.pos, {}, a.id, a.job->kind, "Door opened"}); }
                a.workLeft = std::max(0, a.job->workTicks);
                a.state = AgentState::Work;
            } else {
                a.state = AgentState::Idle;
            }
            return;
        }
        Vec2i next = a.path.points.front();
        if(next == a.pos){
            a.path.points.erase(a.path.points.begin());
            if(a.path.points.empty()) return;
            next = a.path.points.front();
        }
        // move
        a.pos = next;
    }

    void handleWork(Agent& a){
        if(!a.job){ a.state = AgentState::Idle; return; }
        if(a.workLeft > 0){ --a.workLeft; return; }
        // complete
        applyJobEffect(a, *a.job);
        bus_.publish(Event{EventKind::JobCompleted, a.job->target, a.job->aux, a.id, a.job->kind});

        // For haul/deliver, may need a Deliver step
        if(a.job->kind == JobKind::Haul){
            a.carryTo = a.job->aux;
            a.path = pf_.find(a.pos, a.carryTo, tickCount_);
            a.state = a.path.empty() ? AgentState::Idle : AgentState::Deliver;
        } else {
            a.job.reset();
            a.state = AgentState::AcquireJob;
        }
    }

    void handleDeliver(Agent& a){
        if(a.path.points.empty()){
            // delivered
            // drop carried item at destination stockpile (modeled as inventory -> ground)
            if(a.job){
                int removed = a.inv.remove(a.job->item, a.job->amount);
                ground_.drop(a.pos, a.job->item, removed);
            }
            a.job.reset();
            a.state = AgentState::AcquireJob;
            return;
        }
        Vec2i next = a.path.points.front();
        if(next == a.pos){
            a.path.points.erase(a.path.points.begin());
            if(a.path.points.empty()) return;
            next = a.path.points.front();
        }
        a.pos = next;
    }

    void handleSleep(Agent& a){
        if(a.rest >= 95){ a.state = AgentState::Idle; return; }
        // chance to wake for urgent need (hunger very high)
        if(a.hunger > 90){ a.state = AgentState::Plan; return; }
    }

    void handleLeisure(Agent& a){
        // simple wander between adjacent walkable tiles
        std::array<Vec2i,4> dirs{{{1,0},{-1,0},{0,1},{0,-1}}};
        for(const auto& d : dirs){
            Vec2i np = a.pos + d;
            if(grid_.occupiable(np)){ a.pos=np; break; }
        }
        // if hunger spikes, switch to plan
        if(a.hunger>80) a.state = AgentState::Plan;
    }

    // =====================================================================
    // Job begin/apply effect
    // =====================================================================
    void beginJob(Agent& a){
        bus_.publish(Event{EventKind::JobStarted, a.job->target, a.job->aux, a.id, a.job->kind});
        // Move if needed
        if(a.pos != a.job->target){
            a.path = pf_.find(a.pos, a.job->target, tickCount_);
            if(a.path.empty()){
                bus_.publish(Event{EventKind::PathFailed, a.pos, a.job->target, a.id, a.job->kind});
                a.job.reset(); a.state = AgentState::Idle; return;
            }
            bus_.publish(Event{EventKind::PathFound, a.pos, a.job->target, a.id, a.job->kind});
            a.state = AgentState::Navigate; return;
        }
        a.workLeft = std::max(0, a.job->workTicks);
        a.state = AgentState::Work;
    }

    void applyJobEffect(Agent& a, const Job& j){
        switch(j.kind){
            case JobKind::Chop: {
                auto& t = grid_.at(j.target);
                if (t.material == 1) { t.material = 0; ground_.drop(j.target, ItemId::Log, 1); }
                break;
            }
            case JobKind::Mine: {
                auto& t = grid_.at(j.target);
                if (t.material == 2) { t.material = 0; ground_.drop(j.target, ItemId::Stone, 1); ground_.drop(j.target, ItemId::Ore, 1); }
                break;
            }
            case JobKind::Build: {
                // Mark walkable floor/road
                grid_.setObstacle(j.target, false);
                break;
            }
            case JobKind::Farm: {
                // Toggle crop
                grid_.setMaterial(j.target, 4);
                ground_.drop(j.target, ItemId::Crop, 1);
                break;
            }
            case JobKind::Haul: {
                // pick up ground items into inventory
                int got = ground_.take(j.target, j.item, j.amount);
                int left = a.inv.add(j.item, got);
                if(left>0){ ground_.drop(j.target, j.item, left); } // overflow back to ground
                break;
            }
            case JobKind::Deliver: {
                // handled in Deliver state by moving & dropping
                break;
            }
            case JobKind::Cook:
            case JobKind::Craft: {
                // consume inputs from ground on target tile (as if pre-hauled), produce outputs
                // find a workstation
                Workstation* ws=nullptr;
                for(auto& w: buildings_.all()) if(w.pos==j.target){ ws=&w; break; }
                if(ws){
                    const Recipe* rec = nullptr;
                    for(auto& r: ws->recipes) if((j.kind==JobKind::Cook ? r.jobKind==JobKind::Cook : r.jobKind==JobKind::Craft)){ rec=&r; break; }
                    if(rec){
                        // consume inputs from ground at ws
                        bool ok=true;
                        for(auto in: rec->inputs){
                            int got = ground_.take(ws->pos, in.id, in.qty);
                            if(got < in.qty){ ok=false; break; }
                        }
                        if(ok){
                            for(auto out: rec->outputs) ground_.drop(ws->pos, out.id, out.qty);
                            if(j.kind==JobKind::Cook) { a.hunger = std::max(0, a.hunger - 25); a.morale = std::min(100, a.morale + 3); }
                        }
                    }
                }
                break;
            }
            case JobKind::Research: {
                // consume paper at bench, produce research data
                ground_.take(j.target, ItemId::Paper, 1);
                ground_.drop(j.target, ItemId::ResearchData, 1);
                a.morale = std::min(100, a.morale + 2);
                break;
            }
            case JobKind::Heal: {
                if(a.inv.remove(ItemId::Medicine,1)) a.morale = std::min(100, a.morale+10);
                break;
            }
            case JobKind::Train: {
                // improve a random skill a bit
                a.skills[JobKind::Craft] = std::min(10, a.skills[JobKind::Craft]+1);
                break;
            }
            case JobKind::Tame: {
                // placeholder: morale reward
                a.morale = std::min(100, a.morale+5);
                break;
            }
            case JobKind::Patrol: {
                // enqueue move to aux and back
                break;
            }
            case JobKind::Trade: {
                // Drop some items on market tile (aux) and maybe receive others
                int removed = a.inv.remove(j.item, j.amount);
                ground_.drop(j.aux, j.item, removed);
                // receive payment (planks for logs?)
                if(j.item==ItemId::Log) ground_.drop(j.aux, ItemId::Plank, removed/2);
                break;
            }
            case JobKind::MoveTo:
            case JobKind::None: break;
            case JobKind::Deliver: break;
        }
        // notify paint
        bus_.publish(Event{EventKind::TileChanged, j.target, {}, a.id, j.kind});
    }

    // =====================================================================
    // Workstation job spawner
    // =====================================================================
    void autoEnqueueWorkstationJobs(JobQueue& jq){
        // Simple: if there's input on ground near a station and not enough output, enqueue craft/cook.
        for(auto& w: buildings_.all()){
            for(auto& r: w.recipes){
                // heuristic: if >=1 input available at ws tile, enqueue one job
                bool hasInput=true;
                for(auto in: r.inputs){
                    auto v = ground_.at(w.pos);
                    int cnt=0; if(v) for(auto& s:*v) if(s.id==in.id) cnt+=s.qty;
                    if(cnt<in.qty){ hasInput=false; break; }
                }
                if(hasInput){
                    if(r.jobKind==JobKind::Cook) jq.push(Job::Cook(w.pos, r.workTicks, r.outputs[0].id, r.outputs[0].qty), /*priority*/5);
                    else if(r.jobKind==JobKind::Research) jq.push(Job::Research(w.pos, r.workTicks), 4);
                    else jq.push(Job::Craft(w.pos, r.workTicks, r.outputs[0].id, r.outputs[0].qty), 3);
                } else {
                    // else enqueue hauls for missing inputs from nearby ground to station
                    for(auto in: r.inputs){
                        int have = 0; if(auto v=ground_.at(w.pos)) for(auto&s:*v) if(s.id==in.id) have += s.qty;
                        int need = std::max(0, in.qty - have);
                        if(need>0){
                            // find nearest tile with item
                            Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                            for(auto& kv: ground_.all()){
                                int cnt=0; for(auto&s: kv.second) if(s.id==in.id) cnt+=s.qty;
                                if(cnt>0){
                                    int d = kv.first.manhattan(w.pos);
                                    if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; }
                                }
                            }
                            if(bestQty>0){
                                int qty = std::min(bestQty, need);
                                jq.push(Job::Haul(bestPos, w.pos, in.id, qty), 6);
                            }
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // Action Library
    // =====================================================================
    void buildActionLibrary(){
        // Eat (if very hungry) — cook if meal not available, else pick up & eat
        actions_.add(GoapAction{
            "Eat",
            1,
            [](const Agent& a, const World&, const WorldState& st){
                return st.hunger > 60; // desire
            },
            [](WorldState& st){ st.hunger = std::max(0, st.hunger-40); st.hasMeal=true; },
            [](Agent& a, World& w){
                std::vector<Job> js;
                // if meal on ground at kitchen, haul to self then eat; else cook first
                Workstation* k = w.buildings().nearest(BuildingType::Kitchen, a.pos);
                if(k){
                    // ensure meal exists; if not, cook
                    int meals=0; if(auto v=w.ground().at(k->pos)) for(auto&s:*v) if(s.id==ItemId::Meal) meals+=s.qty;
                    if(meals<=0){
                        // need raw food hauled then cook
                        // haul raw food
                        Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                        for(auto& kv: w.ground().all()){
                            int cnt=0; for(auto&s: kv.second) if(s.id==ItemId::RawFood) cnt+=s.qty;
                            if(cnt>0){
                                int d = kv.first.manhattan(k->pos);
                                if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; }
                            }
                        }
                        if(bestQty>0) js.push_back(Job::Haul(bestPos, k->pos, ItemId::RawFood, 1));
                        js.push_back(Job::Cook(k->pos, 140));
                    }
                    // deliver meal to a stockpile near agent (simulate "pick up & eat")
                    js.push_back(Job::Deliver(k->pos, a.pos, ItemId::Meal, 1));
                } else {
                    // fallback: farm crop
                    js.push_back(Job::Farm(a.pos, 80));
                }
                return js;
            }
        });

        // Sleep (if schedule says sleep or very low rest)
        actions_.add(GoapAction{
            "Sleep",
            1,
            [](const Agent& a, const World&, const WorldState& st){
                (void)st;
                return a.rest < 30;
            },
            [](WorldState& st){ st.rest = std::min(100, st.rest + 60); },
            [](Agent& a, World&){ std::vector<Job> js; js.push_back(Job::MoveTo(a.pos)); return js; }
        });

        // Craft Planks at sawmill (if logs around)
        actions_.add(GoapAction{
            "CraftPlanks",
            2,
            [](const Agent& a, const World& w, const WorldState&){
                (void)a;
                // desire if logs exist anywhere
                for(auto& kv: w.ground().all())
                    for(auto&s: kv.second) if(s.id==ItemId::Log && s.qty>0) return true;
                return false;
            },
            [](WorldState& st){ st.morale = std::min(100, st.morale+1); },
            [](Agent& a, World& w){
                std::vector<Job> js;
                Workstation* s = w.buildings().nearest(BuildingType::Sawmill, a.pos);
                if(!s) return js;
                // haul log to sawmill, then craft
                Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                for(auto& kv: w.ground().all()){
                    int cnt=0; for(auto& st: kv.second) if(st.id==ItemId::Log) cnt+=st.qty;
                    if(cnt>0){ int d=kv.first.manhattan(s->pos); if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; } }
                }
                if(bestQty>0) js.push_back(Job::Haul(bestPos, s->pos, ItemId::Log, 1));
                js.push_back(Job::Craft(s->pos, 120, ItemId::Plank, 1));
                // deliver planks to nearest stockpile cell to agent
                auto dest = w.stockpiles().pickDestination(ItemId::Plank, a.pos);
                if(dest) js.push_back(Job::Deliver(s->pos, *dest, ItemId::Plank, 1));
                return js;
            }
        });

        // Research (if paper exists)
        actions_.add(GoapAction{
            "Research",
            2,
            [](const Agent&, const World& w, const WorldState&){
                for(auto& kv: w.ground().all())
                    for(auto&s: kv.second) if(s.id==ItemId::Paper && s.qty>0) return true;
                return false;
            },
            [](WorldState& st){ st.morale = std::min(100, st.morale+2); },
            [](Agent& a, World& w){
                std::vector<Job> js;
                Workstation* r = w.buildings().nearest(BuildingType::ResearchBench, a.pos);
                if(!r) return js;
                Vec2i bestPos{}; int bestD=INT_MAX; int bestQty=0;
                for(auto& kv: w.ground().all()){
                    int cnt=0; for(auto& st: kv.second) if(st.id==ItemId::Paper) cnt+=st.qty;
                    if(cnt>0){ int d=kv.first.manhattan(r->pos); if(d<bestD){ bestD=d; bestPos=kv.first; bestQty=cnt; } }
                }
                if(bestQty>0) js.push_back(Job::Haul(bestPos, r->pos, ItemId::Paper, 1));
                js.push_back(Job::Research(r->pos, 200));
                return js;
            }
        });

        // Patrol (walk between two points)
        actions_.add(GoapAction{
            "Patrol",
            3,
            [](const Agent&, const World&, const WorldState&){ return true; },
            [](WorldState&){},
            [](Agent& a, World& w){
                (void)w;
                std::vector<Job> js;
                Vec2i a0 = a.pos, a1 = a.pos + Vec2i{2,0};
                js.push_back(Job::Patrol(a0, a1, 0));
                js.push_back(Job::MoveTo(a0));
                return js;
            }
        });
    }

    // =====================================================================
    // Persistence
    // =====================================================================
    void saveTo(std::ostream& f){
        f << "WORLD " << grid_.width() << " " << grid_.height() << " " << minuteOfDay_ << " " << tickCount_ << "\n";
        // tiles (walkable, material, terrain, door, zone, cost)
        for(int y=0;y<grid_.height();++y){
            for(int x=0;x<grid_.width();++x){
                const auto& t = grid_.at({x,y});
                f << "T " << x << " " << y << " " << t.walkable << " " << int(t.material) << " " << int(t.terrain)
                  << " " << t.isDoor << " " << t.doorOpen << " " << t.zoneId << " " << t.moveCost << "\n";
            }
        }
        // agents
        for(const auto& a : agents_){
            f << "A " << a.id << " " << a.pos.x << " " << a.pos.y << " " << int(a.state)
              << " " << a.hunger << " " << a.rest << " " << a.morale << " " << a.inv.capacity() << "\n";
            for(auto& s: a.inv.slots()) f << "AS " << int(s.id) << " " << s.qty << "\n";
        }
        // ground items
        for(auto& kv : ground_.all()){
            for(auto& s : kv.second){
                f << "G " << kv.first.x << " " << kv.first.y << " " << int(s.id) << " " << s.qty << "\n";
            }
        }
        // stockpiles
        for(auto& z : stockpiles_.zones()){
            f << "Z " << z.id << " " << z.priority << "\n";
            for(auto i : z.allow) f << "ZA " << z.id << " " << int(i) << "\n";
            for(auto c : z.cells) f << "ZC " << z.id << " " << c.x << " " << c.y << "\n";
        }
        // workstations
        int idx=0;
        for(auto& w: buildings_.all()){
            f << "W " << idx++ << " " << int(w.type) << " " << w.pos.x << " " << w.pos.y << "\n";
        }
    }

    void loadFrom(std::istream& f){
        // reset
        agents_.clear(); ground_.mut().clear();
        stockpiles_ = Stockpiles();
        buildings_ = BuildingManager();

        std::string tok; int W=0,H=0;
        int invCap=0;
        while(f >> tok){
            if(tok=="WORLD"){
                f >> W >> H >> minuteOfDay_ >> tickCount_;
                grid_ = Grid(W,H);
                pf_ = Pathfinder(&grid_);
                pf_.setDynamicBlocker([this](const Vec2i& p){ return occupied_.count(p)>0; });
            } else if(tok=="T"){
                int x,y; bool walk; int mat, ter; bool isDoor, open; int zid; int cost;
                f >> x >> y >> walk >> mat >> ter >> isDoor >> open >> zid >> cost;
                auto& t = grid_.at({x,y});
                t.walkable = walk; t.material=uint8_t(mat); t.terrain=uint8_t(ter);
                t.isDoor=isDoor; t.doorOpen=open; t.zoneId=uint16_t(zid); t.moveCost=uint16_t(cost);
            } else if(tok=="A"){
                Agent a;
                int st; f >> a.id >> a.pos.x >> a.pos.y >> st >> a.hunger >> a.rest >> a.morale >> invCap;
                a.state = static_cast<AgentState>(st);
                a.inv = Inventory(invCap);
                agents_.push_back(std::move(a));
            } else if(tok=="AS"){
                if(agents_.empty()) continue;
                int id, qty; f >> id >> qty;
                agents_.back().inv.add(static_cast<ItemId>(id), qty);
            } else if(tok=="G"){
                int x,y,id,qty; f >> x >> y >> id >> qty; ground_.drop({x,y}, static_cast<ItemId>(id), qty);
            } else if(tok=="Z"){
                uint16_t id; int pri; f >> id >> pri;
                // create exact id: hacky (create new and set id)
                uint16_t newId = stockpiles_.createZone(pri);
                (void)newId; // id may differ, but we'll set cells/allow using provided id mapping if needed
                // (for simplicity, we ignore preserving exact zone IDs)
            } else if(tok=="ZA"){
                uint16_t id; int item; f >> id >> item; (void)id; (void)item;
                // simplified: ignoring allow reconstruction mapping here to keep loader concise
            } else if(tok=="ZC"){
                uint16_t id; int x,y; f >> id >> x >> y; (void)id; (void)x; (void)y;
                // simplified loader: not reconstructing zones exactly in this compact example
            } else if(tok=="W"){
                int idx,type,x,y; f >> idx >> type >> x >> y;
                buildings_.add(static_cast<BuildingType>(type), Vec2i{x,y});
            }
        }
    }

private:
    // ---------------------- Members ----------------------
    Grid grid_{1,1};
    Pathfinder pf_{&grid_};
    EventBus bus_;
    std::vector<Agent> agents_;
    Stockpiles stockpiles_;
    GroundItems ground_;
    BuildingManager buildings_;
    ActionLibrary actions_;

    // Time
    double timeAcc_ = 0.0;
    const double tickSeconds_ = 0.1; // 10 ticks / sec
    uint64_t tickCount_ = 0;
    int minuteOfDay_ = 8*60; // start at 08:00

    // Pathfinding dynamic blockers
    std::unordered_set<Vec2i,Hasher> occupied_;

    int nextAgentId_ = 1;
};

// ============================ End namespace ============================
} // namespace colony
