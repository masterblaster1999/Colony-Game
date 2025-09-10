#pragma once
#ifndef GOAP_PLANNER_HPP
#define GOAP_PLANNER_HPP
//
// GOAP + Utility AI + Sensors + Multi-Agent JobBoard + Grid A* + Plan Cache + ThreadPool + Serialization + Debug tooling
// Single-header implementation for colony sims (C++17).
//
// Build demo:   g++ -std=c++17 -O2 -pthread -DGOAP_DEMO_MAIN -o goap_demo src/ai/GoapPlanner.hpp
// Run tests:    g++ -std=c++17 -O2 -pthread -DGOAP_ENABLE_TESTS -o goap_tests src/ai/GoapPlanner.hpp && ./goap_tests
//
// Notes:
// - This header is self-contained (no third-party deps).
// - You can plug in your own path/danger via PlanningContext hooks.
// - World facts are numeric/bool (int/double/bool).
//

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifndef GOAP_DEBUG
#define GOAP_DEBUG 0
#endif
#ifndef GOAP_TRACE_EXPANSIONS
#define GOAP_TRACE_EXPANSIONS 0
#endif

namespace goap {

// ============================================================================
// Symbol interning
// ============================================================================
using Symbol = uint32_t;

static inline uint32_t fnv1a32(std::string_view s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h ? h : 1u;
}

struct SymbolTable {
    std::unordered_map<Symbol, std::string> names;
    Symbol intern(std::string_view s) {
        Symbol id = fnv1a32(s);
        if (!names.count(id)) names.emplace(id, std::string(s));
        return id;
    }
    const std::string& name(Symbol id) const {
        static const std::string unk = "?";
        auto it = names.find(id);
        return (it == names.end()) ? unk : it->second;
    }
};
static inline SymbolTable& symbols() { static SymbolTable t; return t; }
static inline Symbol S(std::string_view s) { return symbols().intern(s); }

// ============================================================================
// RNG (deterministic; for epsilon-greedy, tie-breaks, tests)
// ============================================================================
struct XorShift64 {
    uint64_t s;
    explicit XorShift64(uint64_t seed = 0x9E3779B97F4A7C15ull): s(seed?seed:0x9E3779B97F4A7C15ull){}
    uint64_t next(){ uint64_t x=s; x^=x<<13; x^=x>>7; x^=x<<17; return s=x; }
    double uniform01(){ return (next() >> 11) * (1.0/9007199254740992.0); }
    int rangei(int a,int b){ if(b<a) std::swap(a,b); return a + int(next()%(uint64_t(b-a+1))); }
};

// ============================================================================
// World values, conditions, effects, world state
// ============================================================================
using Value = std::variant<int,double,bool>;
enum class Cmp{ EQ,NEQ,GT,GTE,LT,LTE,EXISTS };
enum class EffOp{ SET,ADD,SUB,DEL };

static inline double toNumber(const Value& v){
    if(std::holds_alternative<int>(v)) return double(std::get<int>(v));
    if(std::holds_alternative<double>(v)) return std::get<double>(v);
    if(std::holds_alternative<bool>(v)) return std::get<bool>(v)?1.0:0.0;
    return 0.0;
}
static inline std::string toString(const Value& v){
    if(std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    if(std::holds_alternative<double>(v)){ std::ostringstream o; o<<std::setprecision(6)<<std::fixed<<std::get<double>(v); return o.str(); }
    if(std::holds_alternative<bool>(v)) return std::get<bool>(v)?"true":"false";
    return "null";
}

struct Condition{ Symbol key{}; Cmp op{Cmp::EQ}; Value value{0}; };
struct Effect   { Symbol key{}; EffOp op{EffOp::SET}; Value value{0}; };

struct WorldState {
    std::unordered_map<Symbol,Value> facts;

    void set(Symbol k,const Value& v){ facts[k]=v; }
    bool has(Symbol k) const { return facts.find(k)!=facts.end(); }

    template<class T> T get(Symbol k,const T& def) const{
        auto it=facts.find(k);
        if(it==facts.end()) return def;
        if constexpr(std::is_same_v<T,int>){
            if(std::holds_alternative<int>(it->second)) return std::get<int>(it->second);
            if(std::holds_alternative<double>(it->second)) return int(std::llround(std::get<double>(it->second)));
            if(std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second)?1:0;
        } else if constexpr(std::is_same_v<T,double>){
            return toNumber(it->second);
        } else if constexpr(std::is_same_v<T,bool>){
            if(std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            return toNumber(it->second)!=0.0;
        }
        return def;
    }

    static bool compare(const Value& cur,Cmp op,const Value& rhs){
        if(op==Cmp::EXISTS) return true;
        double a=toNumber(cur), b=toNumber(rhs);
        switch(op){
            case Cmp::EQ:  return std::fabs(a-b)<1e-9;
            case Cmp::NEQ: return std::fabs(a-b)>=1e-9;
            case Cmp::GT:  return a>b+1e-9;
            case Cmp::GTE: return a>=b-1e-9;
            case Cmp::LT:  return a<b-1e-9;
            case Cmp::LTE: return a<=b+1e-9;
            default: return false;
        }
    }
    bool satisfies(const std::vector<Condition>& conds) const{
        for(const auto& c:conds){
            auto it=facts.find(c.key);
            if(c.op==Cmp::EXISTS){ if(it==facts.end()) return false; continue; }
            if(it==facts.end()) return false;
            if(!compare(it->second,c.op,c.value)) return false;
        }
        return true;
    }
    void apply(const Effect& e){
        switch(e.op){
            case EffOp::SET: set(e.key,e.value); break;
            case EffOp::ADD: set(e.key, get<double>(e.key,0.0)+toNumber(e.value)); break;
            case EffOp::SUB: set(e.key, get<double>(e.key,0.0)-toNumber(e.value)); break;
            case EffOp::DEL: facts.erase(e.key); break;
        }
    }
    size_t hashCoarse(double bucket=1.0) const{
        uint32_t h=2166136261u; auto mix=[&](uint32_t x){h^=x; h*=16777619u;};
        for(const auto& kv:facts){
            mix(kv.first); uint32_t vbits=0u;
            if(std::holds_alternative<int>(kv.second)) vbits=uint32_t(std::get<int>(kv.second));
            else if(std::holds_alternative<double>(kv.second)){ int q=int(std::floor(std::get<double>(kv.second)/bucket)); vbits=uint32_t(q); }
            else if(std::holds_alternative<bool>(kv.second)) vbits=std::get<bool>(kv.second)?1u:0u;
            mix(vbits);
        }
        return size_t(h?h:1u);
    }
};

static inline Condition C(Symbol k,Cmp op,Value v=0){ return Condition{k,op,v}; }
static inline Effect   E(Symbol k,EffOp op,Value v=0){ return Effect{k,op,v}; }

// ============================================================================
// Minimal JSON writer/reader (enough for our own emitted JSON)
// ============================================================================
struct Json {
    std::ostringstream oss; bool first=true;
    static std::string esc(const std::string& s){ std::ostringstream o; for(char c:s){
        switch(c){ case '\"':o<<"\\\""; break; case '\\':o<<"\\\\"; break; case '\n':o<<"\\n"; break; case '\r':o<<"\\r"; break; case '\t':o<<"\\t"; break; default:o<<c; } } return o.str(); }
    void beginObj(){ oss<<'{'; first=true; }
    void endObj(){ oss<<'}'; }
    void beginArr(){ oss<<'['; first=true; }
    void endArr(){ oss<<']'; }
    void comma(){ if(!first) oss<<','; first=false; }
    void kv(const std::string& k,const std::string& v){ comma(); oss<<"\""<<esc(k)<<"\":\""<<esc(v)<<"\""; }
    void kv(const std::string& k,double v){ comma(); oss<<"\""<<esc(k)<<"\":"
        << std::setprecision(6) << std::fixed << v; }
    void kv(const std::string& k,int v){ comma(); oss<<"\""<<esc(k)<<"\":"
        << v; }
    void kv(const std::string& k,bool v){ comma(); oss<<"\""<<esc(k)<<"\":"
        <<(v?"true":"false"); }
    std::string str() const { return oss.str(); }
};

// Tiny reader (supports { "k": number/bool/string, ... } flat objects)
struct JsonReader {
    const char* p; const char* end; bool ok=true;
    explicit JsonReader(const std::string& s): p(s.data()), end(s.data()+s.size()){}
    void ws(){ while(p<end && std::isspace((unsigned char)*p)) ++p; }
    bool match(char c){ ws(); if(p<end && *p==c){ ++p; return true; } return false; }
    bool parseString(std::string& out){
        ws(); if(p>=end||*p!='\"') return ok=false;
        ++p; std::ostringstream o;
        while(p<end){
            char c=*p++;
            if(c=='\"') break;
            if(c=='\\' && p<end){
                char e=*p++;
                switch(e){ case 'n': o<<'\n'; break; case 'r': o<<'\r'; break; case 't': o<<'\t'; break; case '\\': o<<'\\'; break; case '"': o<<'"'; break; default: o<<e; }
            } else { o<<c; }
        }
        out=o.str(); return ok;
    }
    bool parseBool(bool& b){
        ws();
        if(end-p>=4 && std::equal(p,p+4,"true")){ b=true; p+=4; return true; }
        if(end-p>=5 && std::equal(p,p+5,"false")){ b=false; p+=5; return true; }
        return ok=false;
    }
    bool parseNumber(double& d){
        ws(); const char* s=p; if(s<end && (*s=='-'||*s=='+')) ++s;
        bool dot=false; while(s<end && (std::isdigit((unsigned char)*s)||*s=='.')){ if(*s=='.') dot=true; ++s; }
        if(s==p) return ok=false;
        d = std::strtod(p,nullptr); p=s; return true;
    }
    // Parses { ... } into map<string,Value>. Only numbers and bools are supported.
    bool parseFlatObject(std::unordered_map<std::string,Value>& out){
        if(!match('{')) return ok=false;
        ws(); if(match('}')) return true;
        for(;;){
            std::string key; if(!parseString(key)) return ok=false;
            if(!match(':')) return ok=false;
            ws();
            if(p<end && *p=='\"'){ std::string s; if(!parseString(s)) return ok=false; out[key]=s=="true"?Value(true):s=="false"?Value(false):Value(0); }
            else if(p<end && (*p=='t'||*p=='f')){ bool b; if(!parseBool(b)) return ok=false; out[key]=b; }
            else { double d; if(!parseNumber(d)) return ok=false; // prefer int if integral
                   if(std::fabs(d-std::llround(d))<1e-9) out[key]=int(std::llround(d)); else out[key]=d; }
            ws();
            if(match('}')) return true;
            if(!match(',')) return ok=false;
        }
    }
};

// Serialize/deserialize WorldState (facts only)
static inline std::string serializeWorld(const WorldState& w){
    Json j; j.beginObj(); j.kv("type","WorldState"); j.comma();
    j.oss<<"\"facts\":{"; bool first=true;
    for(const auto& kv:w.facts){
        if(!first) j.oss<<","; first=false;
        j.oss<<"\""<<Json::esc(symbols().name(kv.first))<<"\":";
        if(std::holds_alternative<int>(kv.second)) j.oss<<std::get<int>(kv.second);
        else if(std::holds_alternative<double>(kv.second)) j.oss<<std::setprecision(6)<<std::fixed<<std::get<double>(kv.second);
        else if(std::holds_alternative<bool>(kv.second)) j.oss<<(std::get<bool>(kv.second)?"true":"false");
    }
    j.oss<<"}"; j.endObj(); return j.str();
}

static inline bool deserializeWorld(const std::string& s, WorldState& out){
    // Very simple: look for top-level object with "facts":{...}
    auto pos = s.find("\"facts\"");
    if(pos==std::string::npos) return false;
    auto brace = s.find('{', pos);
    if(brace==std::string::npos) return false;
    // Find matching closing }
    int depth=0; size_t i=brace; for(; i<s.size(); ++i){ if(s[i]=='{') ++depth; else if(s[i]=='}'){ if(--depth==0){ break; } } }
    if(i>=s.size()) return false;
    std::string view = s.substr(brace, i-brace+1);
    JsonReader jr(view);
    std::unordered_map<std::string,Value> flat;
    if(!jr.parseFlatObject(flat)) return false;
    for(auto& kv : flat){
        out.set(S(kv.first), kv.second);
    }
    return true;
}

// ============================================================================
// Keys (common facts used by default actions/goals/sensors)
// ============================================================================
struct Keys {
    // Agent metrics
    static Symbol x(){return S("agent_x");}
    static Symbol y(){return S("agent_y");}
    static Symbol hunger(){return S("hunger");}     // 0..100
    static Symbol thirst(){return S("thirst");}     // 0..100
    static Symbol fatigue(){return S("fatigue");}   // 0..100
    static Symbol temp(){return S("temperature");}  // (C)
    static Symbol health(){return S("health");}     // 0..100
    static Symbol safety(){return S("safety");}     // 0..100
    static Symbol time(){return S("time");}         // tick

    // Inventory/resources
    static Symbol wood(){return S("wood");}
    static Symbol ore(){return S("ore");}
    static Symbol ingot(){return S("ingot");}
    static Symbol meal(){return S("meal");}
    static Symbol water(){return S("water");}
    static Symbol herb(){return S("herb");}
    static Symbol leather(){return S("leather");}
    static Symbol tool_quality(){return S("tool_quality");}

    // Equipment flags
    static Symbol has_axe(){return S("has_axe");}
    static Symbol has_pick(){return S("has_pickaxe");}
    static Symbol has_knife(){return S("has_knife");}

    // Fire/warmth
    static Symbol has_fire(){return S("has_fire");}
    static Symbol fire_fuel(){return S("fire_fuel");}

    // Coordinates of stations/resources
    static Symbol tree_x(){return S("tree_x");} static Symbol tree_y(){return S("tree_y");}
    static Symbol ore_x(){return S("ore_x");}   static Symbol ore_y(){return S("ore_y");}
    static Symbol water_x(){return S("well_x");}static Symbol water_y(){return S("well_y");}
    static Symbol bed_x(){return S("bed_x");}   static Symbol bed_y(){return S("bed_y");}
    static Symbol kitchen_x(){return S("kitchen_x");} static Symbol kitchen_y(){return S("kitchen_y");}
    static Symbol furnace_x(){return S("furnace_x");} static Symbol furnace_y(){return S("furnace_y");}
    static Symbol stock_x(){return S("stock_x");} static Symbol stock_y(){return S("stock_y");}
    static Symbol wall_x(){return S("wall_x");}  static Symbol wall_y(){return S("wall_y");}
    static Symbol bench_x(){return S("bench_x");}static Symbol bench_y(){return S("bench_y");}
    static Symbol field_x(){return S("field_x");}static Symbol field_y(){return S("field_y");}
    static Symbol fire_x(){return S("fire_x");}  static Symbol fire_y(){return S("fire_y");}
    static Symbol trader_x(){return S("trader_x");} static Symbol trader_y(){return S("trader_y");}
    static Symbol hunt_x(){return S("hunt_x");}  static Symbol hunt_y(){return S("hunt_y");}

    // Distances sensed (for quick utility/dyn cost)
    static Symbol dist_tree(){return S("dist_tree");}
    static Symbol dist_ore(){return S("dist_ore");}
    static Symbol dist_well(){return S("dist_well");}
    static Symbol dist_fire(){return S("dist_fire");}
    static Symbol dist_bed(){return S("dist_bed");}
    static Symbol ambient_danger(){return S("ambient_danger");}

    // Stocks
    static Symbol wood_stock(){return S("wood_stock");}
    static Symbol ore_stock(){return S("ore_stock");}
    static Symbol meal_stock(){return S("meal_stock");}
    static Symbol water_stock(){return S("water_stock");}
    static Symbol ingot_stock(){return S("ingot_stock");}

    // Job/Reservation helpers
    static Symbol job_type(){ return S("job_type"); } // current job symbol (advisory)
    static Symbol job_x(){ return S("job_x"); }
    static Symbol job_y(){ return S("job_y"); }
    static Symbol job_id(){ return S("job_id"); } // last claimed job id (int)
};

// Small helpers
static inline void tickTime(WorldState& w,double dt=1.0){ w.set(Keys::time(), w.get<double>(Keys::time(),0.0)+dt); }
static inline void addFatigue(WorldState& w,double df){ w.set(Keys::fatigue(), w.get<double>(Keys::fatigue(),0.0)+df); }

// ============================================================================
// Grid & path: small A* fallback
// ============================================================================
struct Grid{
    int w=0,h=0; std::function<bool(int,int)> passable;
    static inline int idx(int x,int y,int w){return y*w+x;}
    static inline bool inside(int x,int y,int w,int h){return x>=0 && y>=0 && x<w && y<h;}
};
struct AStar {
    struct Node{ int x,y; double g,f; int parent; };
    static double manhattan(int ax,int ay,int bx,intby){ return std::abs(ax-bx)+std::abs(ay-by); }
    static double findCost(const Grid& g,int sx,int sy,int tx,int ty,int maxExpand=3000){
        if(sx==tx && sy==ty) return 0;
        if(!g.passable) return manhattan(sx,sy,tx,ty);
        auto pass=[&](int x,int y){ return Grid::inside(x,y,g.w,g.h) && g.passable(x,y); };
        std::vector<Node> nodes; nodes.reserve(1024);
        std::vector<int> open; auto cmp=[&](int a,int b){ return nodes[a].f > nodes[b].f; };
        std::unordered_map<int,double> best;
        auto push=[&](int x,int y,double gval,int parent){ double h=manhattan(x,y,tx,ty); nodes.push_back({x,y,gval,gval+h,parent}); int idx=int(nodes.size()-1); open.push_back(idx); std::push_heap(open.begin(),open.end(),cmp); };
        push(sx,sy,0,-1); best[Grid::idx(sx,sy,g.w)]=0.0;
        int expanded=0;
        static const int DX[4]={1,-1,0,0}, DY[4]={0,0,1,-1};
        while(!open.empty() && expanded<maxExpand){
            std::pop_heap(open.begin(),open.end(),cmp); int i=open.back(); open.pop_back();
            auto n=nodes[i]; if(n.x==tx && n.y==ty) return n.g;
            for(int k=0;k<4;++k){
                int nx=n.x+DX[k], ny=n.y+DY[k]; if(!pass(nx,ny)) continue;
                double g2=n.g+1.0; int id=Grid::idx(nx,ny,g.w); auto it=best.find(id);
                if(it!=best.end() && g2>=it->second) continue; best[id]=g2;
                nodes.push_back({nx,ny,g2,g2+manhattan(nx,ny,tx,ty),i});
                open.push_back(int(nodes.size()-1)); std::push_heap(open.begin(),open.end(),cmp);
            }
            ++expanded;
        }
        return manhattan(sx,sy,tx,ty)+5.0;
    }
};

// ============================================================================
// Planning context (hooks)
// ============================================================================
struct PlanningContext {
    Grid grid;
    std::function<double(int,int,int,int)> distance; // optional
    std::function<double(int,int)> dangerAt;         // optional
    uint64_t agentId=0;
    double tick=0.0;
    bool debug=false;
    double epsilon=0.05;

    double pathCost(int ax,int ay,int bx,int by) const {
        if(distance) return distance(ax,ay,bx,by);
        if(grid.w>0 && grid.h>0) return AStar::findCost(grid,ax,ay,bx,by);
        return std::abs(ax-bx)+std::abs(ay-by);
    }
    double danger(int x,int y) const { return dangerAt ? dangerAt(x,y) : 0.0; }
};

// ============================================================================
// Job system (multi-agent): postings + claims
// ============================================================================
struct Job {
    uint64_t id{};
    Symbol type{};
    int x=0,y=0;
    int qty=1;
    int priority=0;
    double postedAt=0.0;
    double expiresAt=std::numeric_limits<double>::infinity();
    uint32_t assignedTo=0; // 0 = none
    bool active=true;
};

struct MultiJobBoard {
    std::unordered_map<uint64_t,Job> jobs;
    std::multimap<Symbol,uint64_t> byType;
    std::atomic<uint64_t> seq{1};
    mutable std::mutex mtx;

    uint64_t post(Symbol type,int x,int y,int qty,int prio,double now,double ttl=120.0){
        std::scoped_lock lk(mtx);
        uint64_t id=seq++;
        Job j; j.id=id; j.type=type; j.x=x; j.y=y; j.qty=qty; j.priority=prio; j.postedAt=now; j.expiresAt=now+ttl; j.active=true;
        jobs.emplace(id,j); byType.emplace(type,id);
        return id;
    }
    void cancel(uint64_t id){
        std::scoped_lock lk(mtx);
        auto it=jobs.find(id); if(it==jobs.end()) return;
        it->second.active=false; it->second.expiresAt=0;
    }
    void complete(uint64_t id, uint32_t agent){
        std::scoped_lock lk(mtx);
        auto it=jobs.find(id); if(it==jobs.end()) return;
        if(it->second.assignedTo==agent) it->second.active=false;
    }
    void cleanup(double now){
        std::scoped_lock lk(mtx);
        for(auto it=jobs.begin(); it!=jobs.end();){
            if(!it->second.active || it->second.expiresAt<=now){
                // remove byType ref
                auto range = byType.equal_range(it->second.type);
                for(auto jt=range.first; jt!=range.second; ){
                    if(jt->second==it->first) jt = byType.erase(jt); else ++jt;
                }
                it = jobs.erase(it);
            } else ++it;
        }
    }
    // Peek nearest open job of type; does NOT claim.
    std::optional<Job> nearest(Symbol type,int ax,int ay,const PlanningContext& ctx,double now) const {
        std::scoped_lock lk(mtx);
        double best=std::numeric_limits<double>::infinity(); std::optional<Job> out;
        auto range = byType.equal_range(type);
        for(auto it=range.first; it!=range.second; ++it){
            auto jt=jobs.find(it->second); if(jt==jobs.end()) continue;
            const Job& j = jt->second;
            if(!j.active || j.expiresAt<=now || j.assignedTo!=0) continue;
            double d = ctx.pathCost(ax,ay,j.x,j.y) + 0.5*j.priority;
            if(d<best){ best=d; out=j; }
        }
        return out;
    }
    // Try to claim a specific job id
    bool tryClaim(uint64_t id, uint32_t agent,double /*now*/){
        std::scoped_lock lk(mtx);
        auto it=jobs.find(id); if(it==jobs.end()) return false;
        if(!it->second.active || it->second.assignedTo!=0) return false;
        it->second.assignedTo=agent; return true;
    }
};

// ============================================================================
// Actions
// ============================================================================
struct Action {
    Symbol id{};
    std::string name;
    std::vector<Condition> pre;
    std::vector<Effect> eff;
    double baseCost{1.0};

    // Dynamic hooks (pure functions)
    std::function<bool(const WorldState&, const PlanningContext&)> extraPre;
    std::function<double(const WorldState&, const PlanningContext&)> dynamicCost;
    std::function<void(WorldState&, const PlanningContext&)> simulate;

    // (Optional) job integration: if set, action can try to claim a job at execution time
    Symbol jobType{};              // e.g., S("job_chop"), S("job_mine")
    std::function<std::optional<Job>(const WorldState&, const PlanningContext&)> jobPicker; // if provided, overrides default board-based selection

    bool applicable(const WorldState& w, const PlanningContext& ctx) const {
        if(!w.satisfies(pre)) return false;
        if(extraPre && !extraPre(w,ctx)) return false;
        return true;
    }
    double cost(const WorldState& w, const PlanningContext& ctx) const {
        return baseCost + (dynamicCost ? dynamicCost(w,ctx) : 0.0);
    }
    WorldState apply(const WorldState& w, const PlanningContext& ctx) const {
        WorldState out=w;
        for(const auto& e: eff) out.apply(e);
        if(simulate) { WorldState copy=out; (void)copy; simulate(out,ctx); }
        return out;
    }
};

// ============================================================================
// Goals, Utilities, selection
// ============================================================================
struct PlanningContext; // fwd

struct Goal {
    Symbol id{};
    std::string name;
    std::vector<Condition> conds;
    std::function<double(const WorldState&, const PlanningContext&)> desirability; // 0..1
    double cooldown=5.0;
    double lastChosenAt=-1.0;
    int priorityBias=0;
};

struct Util {
    static double saturate(double x){ return x<0?0:(x>1?1:x); }
    static double ramp01(double a,double b,double x){
        if(b==a) return x>=b?1.0:0.0; double t=(x-a)/(b-a); return t<0?0:(t>1?1:t);
    }
    static double invRamp01(double a,double b,double x){ return 1.0 - ramp01(a,b,x); }
};

struct GoalPick { const Goal* goal{}; double util{}; };
static inline std::vector<GoalPick> pickTopK(const std::vector<Goal>& goals,const WorldState& w,const PlanningContext& ctx,size_t k=3){
    std::vector<GoalPick> v; v.reserve(goals.size());
    for(const auto& g:goals){
        if(!g.desirability) continue;
        double cd = (g.lastChosenAt<0)?1.0: std::min(1.0, std::max(0.0, (ctx.tick-g.lastChosenAt)/std::max(1e-6,g.cooldown)));
        double u = std::max(0.0, g.desirability(w,ctx)) * cd + 0.001*g.priorityBias;
        v.push_back({&g,u});
    }
    std::sort(v.begin(),v.end(),[](auto&a,auto&b){ return a.util>b.util; });
    if(v.size()>k) v.resize(k);
    return v;
}

// ============================================================================
// Sensors & Sensor Suite
// ============================================================================
struct Sensor {
    Symbol id{};
    std::string name;
    double period=1.0;
    double lastTick=-1.0;
    std::function<void(WorldState&, const PlanningContext&)> read; // update facts
};

struct SensorSuite {
    std::vector<Sensor> sensors;
    void step(WorldState& w, const PlanningContext& ctx){
        for(auto& s : sensors){
            if(s.lastTick<0 || ctx.tick - s.lastTick >= s.period){
                if(s.read) s.read(w, ctx);
                s.lastTick = ctx.tick;
            }
        }
    }
};

static inline double dist2i(int ax,int ay,int bx,int by){ return std::abs(ax-bx)+std::abs(ay-by); }

// Default sensors: metabolism, proximity, ambient danger, fire decay
static inline SensorSuite defaultSensors(){
    SensorSuite ss;
    // Metabolism
    ss.sensors.push_back(Sensor{
        S("sensor_metabolism"), "Metabolism", 1.0, -1.0,
        [](WorldState& w,const PlanningContext&){
            w.set(Keys::hunger(),  std::min(100.0, w.get<double>(Keys::hunger(),0.0)+2.0));
            w.set(Keys::thirst(),  std::min(100.0, w.get<double>(Keys::thirst(),0.0)+2.5));
            // passive temp drift (cooling unless near fire)
            double temp = w.get<double>(Keys::temp(), 18.0);
            if(!w.get<bool>(Keys::has_fire(), false)) temp -= 0.25;
            w.set(Keys::temp(), temp);
            tickTime(w, 0.5);
        }
    });
    // Proximity distances to key targets
    ss.sensors.push_back(Sensor{
        S("sensor_proximity"), "Proximity", 0.5, -1.0,
        [](WorldState& w,const PlanningContext& ctx){
            int ax=w.get<int>(Keys::x(),0), ay=w.get<int>(Keys::y(),0);
            auto upd=[&](Symbol dx,Symbol dy,Symbol out){
                int tx=w.get<int>(dx,ax), ty=w.get<int>(dy,ay);
                w.set(out, dist2i(ax,ay,tx,ty));
            };
            upd(Keys::tree_x(),Keys::tree_y(),Keys::dist_tree());
            upd(Keys::ore_x(),Keys::ore_y(),Keys::dist_ore());
            upd(Keys::water_x(),Keys::water_y(),Keys::dist_well());
            upd(Keys::fire_x(),Keys::fire_y(),Keys::dist_fire());
            upd(Keys::bed_x(),Keys::bed_y(),Keys::dist_bed());
            w.set(Keys::ambient_danger(), ctx.danger(ax,ay));
        }
    });
    // Fire fuel decay
    ss.sensors.push_back(Sensor{
        S("sensor_fire"), "FireDecay", 1.0, -1.0,
        [](WorldState& w,const PlanningContext&){
            if(!w.get<bool>(Keys::has_fire(),false)) return;
            double fuel = w.get<double>(Keys::fire_fuel(), 0.0) - 0.5;
            if(fuel<=0){ w.set(Keys::has_fire(), false); w.set(Keys::fire_fuel(), 0.0); }
            else w.set(Keys::fire_fuel(), fuel);
        }
    });
    return ss;
}

// ============================================================================
// Planner (A* over world states) + trace + config
// ============================================================================
struct PlannerConfig{
    size_t maxExpansions=20000;
    size_t maxQueue=15000;
    size_t maxDepth=48;
    double heuristicWeight=1.0; // >=1.0 okay; 1.0 => admissible under our heuristic
    bool allowDuplicates=false;
};

struct ExpansionTrace {
    struct Row { size_t idx; double g,h,f; std::string via; size_t depth; };
    std::vector<Row> rows;
    void clear(){ rows.clear(); }
};

struct Planner {
    struct Node { WorldState st; double g{0},h{0}; int parent{-1}; const Action* via{nullptr}; int depth{0}; };
    static double heuristic(const WorldState& w,const Goal& g){
        double miss=0; for(const auto& c : g.conds){
            auto it=w.facts.find(c.key);
            if(c.op==Cmp::EXISTS){ if(it==w.facts.end()) miss+=1; continue; }
            if(it==w.facts.end()){ miss+=1; continue; }
            if(!WorldState::compare(it->second,c.op,c.value)) miss+=1;
        } return miss;
    }
    static std::vector<const Action*> plan(
        const WorldState& start,const Goal& goal,const std::vector<Action>& lib,const PlanningContext& ctx,const PlannerConfig& cfg, ExpansionTrace* trace=nullptr)
    {
        if(start.satisfies(goal.conds)) return {};
        struct QItem{ double f; int idx; bool operator<(const QItem& o) const { return f>o.f; } };
        std::priority_queue<QItem> open;
        std::vector<Node> nodes; nodes.reserve(1024);
        std::unordered_map<size_t,double> bestG;

        double h0=heuristic(start,goal);
        nodes.push_back(Node{start,0.0,h0,-1,nullptr,0});
        open.push({cfg.heuristicWeight*h0,0});
        bestG.emplace(start.hashCoarse(), 0.0);

        size_t expansions=0;
        while(!open.empty()){
            int curIdx=open.top().idx; open.pop();
            const Node cur=nodes[curIdx];
            if(trace) trace->rows.push_back({(size_t)curIdx,cur.g,cur.h,cur.g+cfg.heuristicWeight*cur.h, cur.via?cur.via->name:"<start>", (size_t)cur.depth});
            if(cur.st.satisfies(goal.conds)){
                std::vector<const Action*> plan; plan.reserve(16);
                for(int i=curIdx;i!=-1;i=nodes[i].parent) if(nodes[i].via) plan.push_back(nodes[i].via);
                std::reverse(plan.begin(),plan.end()); return plan;
            }
            if(++expansions>cfg.maxExpansions) break;
            if(cur.depth>=int(cfg.maxDepth)) continue;

            for(const auto& act : lib){
                if(!act.applicable(cur.st, ctx)) continue;
                WorldState nxt = act.apply(cur.st, ctx);
                double g2 = cur.g + std::max(1e-4, act.cost(cur.st, ctx));
                size_t sig = nxt.hashCoarse();
                auto it=bestG.find(sig);
                if(it!=bestG.end() && g2>=it->second && !cfg.allowDuplicates) continue;
                double h2 = heuristic(nxt, goal);
                int idx=(int)nodes.size();
                nodes.push_back(Node{nxt,g2,h2,curIdx,&act,cur.depth+1});
                open.push({g2+cfg.heuristicWeight*h2, idx});
                bestG[sig]=g2;
                if(nodes.size()>cfg.maxQueue) goto end;
            }
        }
        end:
        return {};
    }
};

// ============================================================================
// Planning cache & replan policy
// ============================================================================
struct PlanCache {
    struct Entry { Symbol goalId{}; size_t sig{}; std::vector<Symbol> actIds; uint64_t stamp{}; };
    size_t capacity=64;
    std::list<Entry> lru;
    std::unordered_map<uint64_t, std::list<Entry>::iterator> index;

    static uint64_t key(Symbol goalId,size_t sig){ return (uint64_t(goalId)<<32) ^ uint64_t(sig); }
    void put(Symbol goalId,size_t sig,const std::vector<const Action*>& plan,uint64_t stamp){
        uint64_t k=key(goalId,sig);
        auto it=index.find(k); if(it!=index.end()){ lru.erase(it->second); index.erase(it); }
        Entry e; e.goalId=goalId; e.sig=sig; e.stamp=stamp;
        for(auto* a:plan) e.actIds.push_back(a->id);
        lru.push_front(std::move(e)); index[k]=lru.begin();
        while(lru.size()>capacity){ auto it2=--lru.end(); index.erase(key(it2->goalId,it2->sig)); lru.erase(it2); }
    }
    std::vector<Symbol> get(Symbol goalId,size_t sig){
        uint64_t k=key(goalId,sig); auto it=index.find(k); if(it==index.end()) return {};
        auto node=it->second; lru.splice(lru.begin(), lru, node);
        return node->actIds;
    }
    void clear(){ lru.clear(); index.clear(); }
};

struct ReplanPolicy {
    double maxPlanAge=12.0;
    double epsilonGreedy=0.05;
    int changeThreshold=3;
    bool shouldReplan(double lastPlanAt,double now,int delta,XorShift64& rng) const {
        if(now-lastPlanAt>maxPlanAge) return true;
        if(delta>=changeThreshold) return true;
        if(rng.uniform01()<epsilonGreedy) return true;
        return false;
    }
};

// ============================================================================
// Debug timeline + scoped timer + DOT exporter
// ============================================================================
struct Timeline {
    struct Ev{ double t; std::string msg; };
    std::vector<Ev> ring; size_t maxSize=512;
    void log(double t,std::string s){ if(ring.size()>=maxSize) ring.erase(ring.begin()); ring.push_back({t,std::move(s)}); }
    std::string toJson() const {
        Json j; j.beginArr(); bool first=true;
        for(const auto& e:ring){ if(!first) j.oss<<","; first=false; Json o; o.beginObj(); o.kv("t",e.t); o.kv("msg",e.msg); o.endObj(); j.oss<<o.str(); }
        j.endArr(); return j.str();
    }
};

struct ScopedTimer {
    std::chrono::steady_clock::time_point st;
    double* out=nullptr;
    ScopedTimer(double* sink): st(std::chrono::steady_clock::now()), out(sink){}
    ~ScopedTimer(){ if(out){ auto ed=std::chrono::steady_clock::now(); *out = std::chrono::duration<double, std::milli>(ed-st).count(); } }
};

static inline std::string planToDOT(const std::vector<const Action*>& plan){
    std::ostringstream o; o<<"digraph Plan {\n  rankdir=LR;\n";
    for(size_t i=0;i<plan.size();++i){
        o<<"  n"<<i<<" [label=\""<<plan[i]->name<<"\"];\n";
        if(i>0) o<<"  n"<<(i-1)<<" -> n"<<i<<";\n";
    }
    o<<"}\n"; return o.str();
}
static inline std::string planToString(const std::vector<const Action*>& plan){
    std::ostringstream o; if(plan.empty()){ o<<"(no actions)"; return o.str(); }
    for(size_t i=0;i<plan.size();++i) o<< (i+1) << ". " << plan[i]->name << "\n";
    return o.str();
}
static inline void printState(const WorldState& w){
    std::vector<std::pair<std::string,std::string>> v;
    for(const auto& kv:w.facts) v.emplace_back(symbols().name(kv.first), toString(kv.second));
    std::sort(v.begin(),v.end(),[](auto&a,auto&b){ return a.first<b.first; });
    std::cout<<"{ "; for(size_t i=0;i<v.size();++i){ if(i) std::cout<<", "; std::cout<<v[i].first<<":"<<v[i].second; } std::cout<<" }\n";
}

// ============================================================================
// Thread pool (simple)
// ============================================================================
class ThreadPool {
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> q;
    std::mutex m; std::condition_variable cv; bool stop=false;
public:
    explicit ThreadPool(size_t n = std::thread::hardware_concurrency()){ if(n==0) n=1;
        workers.reserve(n);
        for(size_t i=0;i<n;++i){
            workers.emplace_back([this]{ for(;;){
                std::function<void()> task;
                { std::unique_lock lk(m); cv.wait(lk,[&]{ return stop || !q.empty();});
                  if(stop && q.empty()) return; task=std::move(q.front()); q.pop_front(); }
                task();
            }});
        }
    }
    ~ThreadPool(){ { std::lock_guard lk(m); stop=true; } cv.notify_all(); for(auto& t:workers) if(t.joinable()) t.join(); }
    template<class F> auto enqueue(F&& f)->std::future<decltype(f())>{
        using R = decltype(f());
        auto p = std::make_shared<std::promise<R>>(); std::future<R> fut=p->get_future();
        { std::lock_guard lk(m); q.emplace_back([p=std::move(p), fn=std::forward<F>(f)]() mutable {
            try{ if constexpr(std::is_void_v<R>){ fn(); p->set_value(); } else { p->set_value(fn()); } }
            catch(...){ p->set_exception(std::current_exception()); }
        }); }
        cv.notify_one(); return fut;
    }
};

// ============================================================================
// Default action library (40+ actions with dynamic costs & simple sims)
// ============================================================================

static inline double distCost(const WorldState& w, Symbol ax,Symbol ay, Symbol bx,Symbol by, const PlanningContext& ctx){
    int x1=w.get<int>(ax,0), y1=w.get<int>(ay,0), x2=w.get<int>(bx,0), y2=w.get<int>(by,0);
    double p = ctx.pathCost(x1,y1,x2,y2);
    return p + 2.0*ctx.danger(x2,y2);
}

static Action makeMove(Symbol tx,Symbol ty,const std::string& label){
    Action a; a.id=S(label); a.name=label; a.baseCost=0.0;
    a.dynamicCost=[tx,ty](const WorldState& w,const PlanningContext& ctx){ return distCost(w, Keys::x(),Keys::y(), tx,ty, ctx); };
    a.simulate=[tx,ty](WorldState& w,const PlanningContext&){ w.set(Keys::x(), w.get<int>(tx, w.get<int>(Keys::x(),0))); w.set(Keys::y(), w.get<int>(ty, w.get<int>(Keys::y(),0))); addFatigue(w,0.4); tickTime(w,0.5); };
    return a;
}

static std::vector<Action> standardActions() {
    std::vector<Action> L; L.reserve(64);
    // Movement primitives
    L.push_back(makeMove(Keys::tree_x(),Keys::tree_y(),"MoveToTree"));
    L.push_back(makeMove(Keys::ore_x(), Keys::ore_y(), "MoveToOre"));
    L.push_back(makeMove(Keys::water_x(),Keys::water_y(),"MoveToWell"));
    L.push_back(makeMove(Keys::bed_x(),  Keys::bed_y(),  "MoveToBed"));
    L.push_back(makeMove(Keys::kitchen_x(),Keys::kitchen_y(),"MoveToKitchen"));
    L.push_back(makeMove(Keys::furnace_x(),Keys::furnace_y(),"MoveToFurnace"));
    L.push_back(makeMove(Keys::stock_x(), Keys::stock_y(),"MoveToStockpile"));
    L.push_back(makeMove(Keys::wall_x(),  Keys::wall_y(),  "MoveToWall"));
    L.push_back(makeMove(Keys::bench_x(), Keys::bench_y(), "MoveToWorkbench"));
    L.push_back(makeMove(Keys::field_x(), Keys::field_y(), "MoveToField"));
    L.push_back(makeMove(Keys::fire_x(),  Keys::fire_y(),  "MoveToCampfire"));
    L.push_back(makeMove(Keys::trader_x(),Keys::trader_y(),"MoveToTrader"));
    L.push_back(makeMove(Keys::hunt_x(),  Keys::hunt_y(),  "MoveToHunt"));

    // Wood/Fire chain
    L.push_back(Action{ S("ChopTree"), "ChopTree",
        { C(Keys::has_axe(), Cmp::EQ, true) },
        { E(Keys::wood(), EffOp::ADD, 3), E(Keys::fatigue(),EffOp::ADD,6) },
        2.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::tree_x(),Keys::tree_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,2.5); }, S("job_chop"), {}
    });
    L.push_back(Action{ S("FellTree"), "FellTree",
        { C(Keys::has_axe(), Cmp::EQ, true) },
        { E(Keys::wood(), EffOp::ADD, 6), E(Keys::fatigue(),EffOp::ADD,10) },
        3.5, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ double q=w.get<double>(Keys::tool_quality(),50.0); return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::tree_x(),Keys::tree_y(),ctx) - 0.02*q; },
        [](WorldState& w,const PlanningContext&){ tickTime(w,3.5); }
    });
    L.push_back(Action{ S("SplitLogs"), "SplitLogs",
        { C(Keys::wood(), Cmp::GTE, 2) },
        { E(Keys::wood(), EffOp::SUB, 2), E(Keys::fire_fuel(), EffOp::ADD, 6) },
        1.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,1.0); }
    });
    L.push_back(Action{ S("BuildCampfire"), "BuildCampfire",
        { C(Keys::wood(), Cmp::GTE, 2) },
        { E(Keys::wood(), EffOp::SUB,2), E(Keys::has_fire(), EffOp::SET, true), E(Keys::fire_fuel(), EffOp::ADD, 4) },
        2.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::fire_x(),Keys::fire_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });
    L.push_back(Action{ S("StokeFire"), "StokeFire",
        { C(Keys::has_fire(), Cmp::EQ, true), C(Keys::wood(), Cmp::GTE, 1) },
        { E(Keys::wood(), EffOp::SUB,1), E(Keys::fire_fuel(), EffOp::ADD, 3), E(Keys::temp(), EffOp::ADD, 2) },
        1.0, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,0.5); }
    });
    L.push_back(Action{ S("WarmUp"), "WarmUp",
        { C(Keys::has_fire(), Cmp::EQ, true) },
        { E(Keys::temp(), EffOp::ADD, 10) },
        1.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.1*distCost(w,Keys::x(),Keys::y(),Keys::fire_x(),Keys::fire_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.0); }
    });

    // Ore/Metal chain
    L.push_back(Action{ S("MineOre"), "MineOre",
        { C(Keys::has_pick(), Cmp::EQ, true) },
        { E(Keys::ore(), EffOp::ADD, 2), E(Keys::fatigue(),EffOp::ADD,6) },
        2.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::ore_x(),Keys::ore_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,2.5); }, S("job_mine"), {}
    });
    L.push_back(Action{ S("ProspectOre"), "ProspectOre",
        { C(Keys::has_pick(), Cmp::EQ, true) },
        { E(Keys::ore(), EffOp::ADD, 1), E(Keys::tool_quality(), EffOp::ADD, 1) },
        1.5, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::ore_x(),Keys::ore_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });
    L.push_back(Action{ S("SmeltOre"), "SmeltOre",
        { C(Keys::ore(), Cmp::GTE, 2), C(Keys::wood(), Cmp::GTE, 1) },
        { E(Keys::ore(), EffOp::SUB, 2), E(Keys::wood(), EffOp::SUB, 1), E(Keys::ingot(), EffOp::ADD, 1) },
        3.5, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::furnace_x(),Keys::furnace_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }
    });
    L.push_back(Action{ S("SmeltBatch"), "SmeltBatch",
        { C(Keys::ore(), Cmp::GTE, 6), C(Keys::wood(), Cmp::GTE, 2) },
        { E(Keys::ore(), EffOp::SUB, 6), E(Keys::wood(), EffOp::SUB, 2), E(Keys::ingot(), EffOp::ADD, 3) },
        6.5, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.35*distCost(w,Keys::x(),Keys::y(),Keys::furnace_x(),Keys::furnace_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,5.0); }
    });

    // Tooling
    L.push_back(Action{ S("ForgeAxe"), "ForgeAxe",
        { C(Keys::ingot(), Cmp::GTE, 1), C(Keys::wood(), Cmp::GTE, 1) },
        { E(Keys::ingot(), EffOp::SUB, 1), E(Keys::wood(), EffOp::SUB,1), E(Keys::has_axe(), EffOp::SET, true), E(Keys::tool_quality(), EffOp::ADD, 8) },
        4.0, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }
    });
    L.push_back(Action{ S("ForgePickaxe"), "ForgePickaxe",
        { C(Keys::ingot(), Cmp::GTE, 1), C(Keys::wood(), Cmp::GTE, 1) },
        { E(Keys::ingot(), EffOp::SUB, 1), E(Keys::wood(), EffOp::SUB,1), E(Keys::has_pick(), EffOp::SET, true), E(Keys::tool_quality(), EffOp::ADD, 8) },
        4.0, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }
    });
    L.push_back(Action{ S("SharpenTools"), "SharpenTools",
        { },
        { E(Keys::tool_quality(), EffOp::ADD, 5) },
        1.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,1.0); }
    });

    // Cooking / eating / drinking
    L.push_back(Action{ S("CookMeal"), "CookMeal",
        { C(Keys::water(), Cmp::GTE, 1), C(Keys::wood(), Cmp::GTE, 1) },
        { E(Keys::water(), EffOp::SUB,1), E(Keys::wood(), EffOp::SUB,1), E(Keys::meal(), EffOp::ADD, 1) },
        2.5, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::kitchen_x(),Keys::kitchen_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,2.0); }
    });
    L.push_back(Action{ S("CampCook"), "CampCook",
        { C(Keys::wood(), Cmp::GTE, 1), C(Keys::has_fire(), Cmp::EQ, true) },
        { E(Keys::wood(), EffOp::SUB,1), E(Keys::meal(), EffOp::ADD,1) },
        2.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.15*distCost(w,Keys::x(),Keys::y(),Keys::fire_x(),Keys::fire_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });
    L.push_back(Action{ S("EatMeal"), "EatMeal",
        { C(Keys::meal(), Cmp::GTE, 1) },
        { E(Keys::meal(), EffOp::SUB,1), E(Keys::hunger(),EffOp::SUB, 35), E(Keys::health(), EffOp::ADD,2) },
        1.0, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,0.5); }
    });
    L.push_back(Action{ S("EatRation"), "EatRation",
        { C(Keys::meal(), Cmp::GTE, 1) },
        { E(Keys::meal(), EffOp::SUB,1), E(Keys::hunger(),EffOp::SUB, 20) },
        0.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,0.25); }
    });
    L.push_back(Action{ S("FetchWater"), "FetchWater",
        { },
        { E(Keys::water(), EffOp::ADD, 2) },
        2.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::water_x(),Keys::water_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });
    L.push_back(Action{ S("DrinkWater"), "DrinkWater",
        { C(Keys::water(), Cmp::GTE, 1) },
        { E(Keys::water(), EffOp::SUB,1), E(Keys::thirst(), EffOp::SUB, 40) },
        0.8, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,0.25); }
    });
    L.push_back(Action{ S("BoilWater"), "BoilWater",
        { C(Keys::water(), Cmp::GTE, 1), C(Keys::has_fire(), Cmp::EQ, true) },
        { E(Keys::water(), EffOp::ADD, 0) /* keep */, E(Keys::health(), EffOp::ADD, 1) },
        1.0, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,1.0); }
    });

    // Resting / healing
    L.push_back(Action{ S("Sleep"), "Sleep",
        { },
        { E(Keys::fatigue(), EffOp::SUB, 55), E(Keys::health(), EffOp::ADD, 4) },
        3.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::bed_x(),Keys::bed_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,6.0); }
    });
    L.push_back(Action{ S("Nap"), "Nap",
        { },
        { E(Keys::fatigue(), EffOp::SUB, 25) },
        1.2, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,2.5); }
    });
    L.push_back(Action{ S("Heal"), "Heal",
        { C(Keys::herb(), Cmp::GTE, 1) },
        { E(Keys::herb(), EffOp::SUB,1), E(Keys::health(), EffOp::ADD, 22) },
        1.6, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,1.0); }
    });

    // Construction / defense
    L.push_back(Action{ S("BuildWall"), "BuildWall",
        { C(Keys::wood(), Cmp::GTE, 2) },
        { E(Keys::wood(), EffOp::SUB,2), E(Keys::safety(), EffOp::ADD, 10) },
        3.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::wall_x(),Keys::wall_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }, S("job_build"), {}
    });
    L.push_back(Action{ S("BuildDoor"), "BuildDoor",
        { C(Keys::wood(), Cmp::GTE, 3) },
        { E(Keys::wood(), EffOp::SUB,3), E(Keys::safety(), EffOp::ADD, 12) },
        3.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }
    });
    L.push_back(Action{ S("Repair"), "Repair",
        { C(Keys::wood(), Cmp::GTE, 1) },
        { E(Keys::wood(), EffOp::SUB,1), E(Keys::safety(), EffOp::ADD, 6) },
        2.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::wall_x(),Keys::wall_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,2.0); }, S("job_repair"), {}
    });

    // Hauling / stockpiles
    L.push_back(Action{ S("HaulResources"), "HaulResources",
        { },
        { E(Keys::wood_stock(), EffOp::ADD, 3), E(Keys::ore_stock(), EffOp::ADD, 2), E(Keys::wood(),EffOp::SET,0), E(Keys::ore(),EffOp::SET,0) },
        1.8, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::stock_x(),Keys::stock_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }, S("job_haul"), {}
    });

    // Research & tech
    L.push_back(Action{ S("Research"), "Research",
        { },
        { E(Keys::tool_quality(), EffOp::ADD, 6), E(Keys::fatigue(),EffOp::ADD,8) },
        2.3, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.2*distCost(w,Keys::x(),Keys::y(),Keys::bench_x(),Keys::bench_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }
    });
    L.push_back(Action{ S("ResearchAdvanced"), "ResearchAdvanced",
        { C(Keys::tool_quality(), Cmp::GTE, 30) },
        { E(Keys::tool_quality(), EffOp::ADD, 12), E(Keys::fatigue(),EffOp::ADD,10) },
        3.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,4.0); }
    });

    // Farming
    L.push_back(Action{ S("PlantCrop"), "PlantCrop",
        { C(Keys::water(), Cmp::GTE, 1) },
        { E(Keys::water(), EffOp::SUB,1), E(Keys::meal_stock(), EffOp::ADD, 1) },
        1.8, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::field_x(),Keys::field_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });
    L.push_back(Action{ S("IrrigateField"), "IrrigateField",
        { C(Keys::water(), Cmp::GTE, 1) },
        { E(Keys::water(), EffOp::SUB,1), E(Keys::meal_stock(), EffOp::ADD, 1) },
        1.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });
    L.push_back(Action{ S("HarvestCrop"), "HarvestCrop",
        { },
        { E(Keys::meal(), EffOp::ADD, 1) },
        1.8, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::field_x(),Keys::field_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });

    // Trade / social
    L.push_back(Action{ S("Trade"), "Trade",
        { },
        { E(Keys::meal(), EffOp::ADD,2), E(Keys::water(), EffOp::ADD,2), E(Keys::wood_stock(), EffOp::SUB,2) },
        2.6, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.35*distCost(w,Keys::x(),Keys::y(),Keys::trader_x(),Keys::trader_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,2.5); }, S("job_trade"), {}
    });

    // Hunting / taming
    L.push_back(Action{ S("Hunt"), "Hunt",
        { C(Keys::has_knife(), Cmp::EQ, true) },
        { E(Keys::leather(), EffOp::ADD, 1), E(Keys::meal(), EffOp::ADD,1), E(Keys::fatigue(),EffOp::ADD, 6) },
        3.2, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.3*distCost(w,Keys::x(),Keys::y(),Keys::hunt_x(),Keys::hunt_y(),ctx) + 2.0*ctx.danger(w.get<int>(Keys::hunt_x(),0), w.get<int>(Keys::hunt_y(),0)); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,3.0); }, S("job_hunt"), {}
    });
    L.push_back(Action{ S("TameAnimal"), "TameAnimal",
        { C(Keys::has_knife(), Cmp::EQ, true) },
        { E(Keys::leather(), EffOp::ADD, 1), E(Keys::safety(), EffOp::ADD,3) },
        3.0, nullptr,
        [](const WorldState& w,const PlanningContext& ctx){ return 0.25*distCost(w,Keys::x(),Keys::y(),Keys::field_x(),Keys::field_y(),ctx); },
        [](WorldState& w,const PlanningContext&){ tickTime(w,2.5); }
    });

    // Safety patrol
    L.push_back(Action{ S("Patrol"), "Patrol",
        { },
        { E(Keys::safety(), EffOp::ADD, 5), E(Keys::fatigue(), EffOp::ADD, 5) },
        1.5, nullptr, nullptr, [](WorldState& w,const PlanningContext&){ tickTime(w,1.5); }
    });

    return L;
}

// ============================================================================
// Standard goals (utility curves)
// ============================================================================
static std::vector<Goal> standardGoals(){
    std::vector<Goal> G;
    G.push_back(Goal{ S("GoalEat"), "Eat",
        { C(Keys::hunger(), Cmp::LTE, 35.0) },
        [](const WorldState& w,const PlanningContext&){ return Util::ramp01(40,80,w.get<double>(Keys::hunger(),0.0)); }, 4.0,-1.0,3 });
    G.push_back(Goal{ S("GoalDrink"), "Drink",
        { C(Keys::thirst(), Cmp::LTE, 25.0) },
        [](const WorldState& w,const PlanningContext&){ return Util::ramp01(30,70,w.get<double>(Keys::thirst(),0.0)); }, 4.0,-1.0,3 });
    G.push_back(Goal{ S("GoalSleep"), "Sleep",
        { C(Keys::fatigue(), Cmp::LTE, 30.0) },
        [](const WorldState& w,const PlanningContext&){ return Util::ramp01(50,85,w.get<double>(Keys::fatigue(),0.0)); }, 6.0,-1.0,2 });
    G.push_back(Goal{ S("GoalWarmUp"), "WarmUp",
        { C(Keys::temp(), Cmp::GTE, 18.0) },
        [](const WorldState& w,const PlanningContext&){ return Util::invRamp01(10,20,w.get<double>(Keys::temp(),18.0)); }, 6.0,-1.0,1 });
    G.push_back(Goal{ S("GoalHeal"), "Heal",
        { C(Keys::health(), Cmp::GTE, 85.0) },
        [](const WorldState& w,const PlanningContext&){ return Util::invRamp01(60,90,w.get<double>(Keys::health(),100.0)); }, 8.0,-1.0,0 });
    G.push_back(Goal{ S("GoalSecure"), "SecureBase",
        { C(Keys::safety(), Cmp::GTE, 75.0) },
        [](const WorldState& w,const PlanningContext&){ return Util::invRamp01(40,70,w.get<double>(Keys::safety(),50.0)); }, 6.0,-1.0,0 });
    G.push_back(Goal{ S("GoalStock"), "Stockpile",
        { C(Keys::wood_stock(), Cmp::GTE, 12.0) },
        [](const WorldState& w,const PlanningContext&){ return 0.25 + 0.01*w.get<double>(Keys::wood_stock(),0.0); }, 4.0,-1.0,0 });
    G.push_back(Goal{ S("GoalTech"), "ResearchTools",
        { C(Keys::tool_quality(), Cmp::GTE, 55.0) },
        [](const WorldState&,const PlanningContext&){ return 0.2; }, 6.0,-1.0,0 });
    return G;
}

// ============================================================================
// Agent planner (cache + trace + goal pick)
// ============================================================================
struct AgentPlanner {
    PlannerConfig cfg;
    PlanCache cache;
    ReplanPolicy policy;
    Timeline timeline;
    double lastPlanAt= -1.0;

    std::vector<const Action*> planOnce(WorldState& w, std::vector<Goal>& goals, const std::vector<Action>& lib, PlanningContext& ctx, XorShift64& rng, ExpansionTrace* trace=nullptr){
        auto top = pickTopK(goals,w,ctx,3);
        if(top.empty()) return {};
        const Goal* chosen = top[0].goal;
        // Try cache
        size_t sig = w.hashCoarse(2.5);
        if(auto ids = cache.get(chosen->id, sig); !ids.empty()){
            std::vector<const Action*> reb; reb.reserve(ids.size());
            for(auto id : ids){ auto it = std::find_if(lib.begin(),lib.end(),[&](const Action& a){return a.id==id;}); if(it!=lib.end()) reb.push_back(&*it); }
            if(!reb.empty()){ timeline.log(ctx.tick, "Cache hit: "+chosen->name); lastPlanAt=ctx.tick; return reb; }
        }
        // Plan
        double ms=0; ScopedTimer t(&ms);
        auto plan = Planner::plan(w,*chosen,lib,ctx,cfg,trace);
        lastPlanAt=ctx.tick;
        if(!plan.empty()){
            cache.put(chosen->id, sig, plan, (uint64_t)ctx.tick);
            for(auto& g:goals) if(g.id==chosen->id) g.lastChosenAt=ctx.tick;
            timeline.log(ctx.tick, "Planned "+chosen->name+" in "+std::to_string(int(ms))+"ms ["+std::to_string(plan.size())+" steps]");
        } else {
            timeline.log(ctx.tick, "Plan FAILED for "+chosen->name);
        }
        return plan;
    }
};

// ============================================================================
// Simple action executor with job claims (first-step execution pattern)
// ============================================================================
struct Executor {
    MultiJobBoard* jobBoard=nullptr;

    // Attempt to claim job if action has a jobType; writes job_id/job_x/job_y into world on success.
    bool ensureJobClaim(const Action& a, WorldState& w, PlanningContext& ctx){
        if(!jobBoard || a.jobType==Symbol{}) return true; // nothing to claim
        // find nearest job by type
        int ax=w.get<int>(Keys::x(),0), ay=w.get<int>(Keys::y(),0);
        auto opt = jobBoard->nearest(a.jobType, ax,ay, ctx, ctx.tick);
        if(!opt) return false;
        if(!jobBoard->tryClaim(opt->id, (uint32_t)ctx.agentId, ctx.tick)) return false;
        w.set(Keys::job_id(), (int)opt->id); w.set(Keys::job_x(), opt->x); w.set(Keys::job_y(), opt->y); w.set(Keys::job_type(), (int)a.jobType);
        return true;
    }

    // Execute the first action in the plan (claim reservation if needed), and apply effects to world.
    // Returns true if an action was executed.
    bool executeFirst(const std::vector<const Action*>& plan, WorldState& w, PlanningContext& ctx){
        if(plan.empty()) return false;
        const Action* a = plan[0];
        if(!ensureJobClaim(*a,w,ctx)) return false; // need to replan if job unavailable
        WorldState nxt = a->apply(w, ctx);
        w = std::move(nxt);
        return true;
    }

    // Mark job complete if we have a job claimed
    void completeJobIfAny(WorldState& w, PlanningContext& ctx){
        if(!jobBoard) return;
        int jid = w.get<int>(Keys::job_id(), 0);
        if(jid>0){ jobBoard->complete((uint64_t)jid, (uint32_t)ctx.agentId); w.set(Keys::job_id(),0); }
    }
};

// ============================================================================
// Tests (optional)
// ============================================================================
#ifdef GOAP_ENABLE_TESTS
static inline bool test_world_basic(){
    WorldState w; auto A=S("A"); w.set(A,5); if(!w.satisfies({C(A,Cmp::EQ,5)})) return false;
    w.apply(E(A,EffOp::ADD,2)); if(w.get<int>(A,0)!=7) return false; w.apply(E(A,EffOp::DEL,0)); if(w.has(A)) return false; return true;
}
static inline bool test_json_roundtrip(){
    WorldState w; w.set(Keys::x(),3); w.set(Keys::has_fire(), true); w.set(Keys::hunger(), 42.0);
    std::string s = serializeWorld(w); WorldState r; if(!deserializeWorld(s,r)) return false;
    return r.get<int>(Keys::x(),0)==3 && r.get<bool>(Keys::has_fire(),false)==true && std::fabs(r.get<double>(Keys::hunger(),0.0)-42.0)<1e-6;
}
static inline bool test_planner_goal(){
    WorldState w; w.set(Keys::hunger(), 80.0); w.set(Keys::meal(), 1);
    PlanningContext ctx; auto L = standardActions(); auto G = standardGoals();
    AgentPlanner ap; XorShift64 rng(123);
    auto plan = ap.planOnce(w, G, L, ctx, rng);
    return !plan.empty();
}
static inline int run_all_tests(){
    int pass=0, tot=3;
    pass += test_world_basic();
    pass += test_json_roundtrip();
    pass += test_planner_goal();
    std::cout<<"GOAP tests "<<pass<<"/"<<tot<<" passed\n";
    return (pass==tot)?0:1;
}
#endif

// ============================================================================
// Demo main (optional) — simulates a few ticks with sensors + planning + exec
// ============================================================================
#ifdef GOAP_DEMO_MAIN
int main(){
    using std::cout; using std::endl;

    // Initial world
    WorldState w;
    w.set(Keys::x(), 5); w.set(Keys::y(), 5);
    w.set(Keys::tree_x(), 3); w.set(Keys::tree_y(), 8);
    w.set(Keys::ore_x(), 10); w.set(Keys::ore_y(), 3);
    w.set(Keys::water_x(), 6); w.set(Keys::water_y(), 9);
    w.set(Keys::bed_x(),  1); w.set(Keys::bed_y(),  1);
    w.set(Keys::kitchen_x(),4); w.set(Keys::kitchen_y(),6);
    w.set(Keys::furnace_x(),8); w.set(Keys::furnace_y(),6);
    w.set(Keys::stock_x(), 3); w.set(Keys::stock_y(), 3);
    w.set(Keys::wall_x(),  7); w.set(Keys::wall_y(),  7);
    w.set(Keys::bench_x(), 6); w.set(Keys::bench_y(),  3);
    w.set(Keys::field_x(), 9); w.set(Keys::field_y(),  9);
    w.set(Keys::fire_x(),  4); w.set(Keys::fire_y(),  4);
    w.set(Keys::trader_x(),0);  w.set(Keys::trader_y(),  9);
    w.set(Keys::hunt_x(),  8);  w.set(Keys::hunt_y(),   10);

    w.set(Keys::has_axe(), true);
    w.set(Keys::has_pick(), true);
    w.set(Keys::has_knife(), true);

    w.set(Keys::hunger(), 50.0);
    w.set(Keys::thirst(), 50.0);
    w.set(Keys::fatigue(), 30.0);
    w.set(Keys::temp(), 16.0);
    w.set(Keys::health(), 80.0);
    w.set(Keys::safety(), 50.0);

    // Planning context with grid + danger
    PlanningContext ctx;
    ctx.agentId = 1;
    ctx.grid.w=12; ctx.grid.h=12;
    ctx.grid.passable = [&](int x,int y){ return !(x==5 && y==6); };
    ctx.dangerAt = [&](int x,int y){ return (x==7 && y==7)?5.0:0.0; };

    // Job board: post a few jobs
    MultiJobBoard jb;
    jb.post(S("job_chop"), 3,8, 1, 0, ctx.tick, 60.0);
    jb.post(S("job_mine"), 10,3, 1, 0, ctx.tick, 60.0);
    jb.post(S("job_repair"), 7,7, 1, 1, ctx.tick, 60.0); // higher prio

    // Sensors
    SensorSuite sensors = defaultSensors();

    // Library/goals/planner
    auto L = standardActions();
    auto G = standardGoals();

    AgentPlanner ap;
    ap.cfg.maxDepth = 28;
    XorShift64 rng(0xBADF00D);
    Executor ex; ex.jobBoard=&jb;

    // Simulate ticks
    for(int t=0;t<8;++t){
        cout << "\n=== TICK " << t << " ===\n";
        sensors.step(w, ctx);
        printState(w);

        ExpansionTrace trace;
        auto plan = ap.planOnce(w, G, L, ctx, rng, &trace);
        cout << "Plan:\n" << planToString(plan);

        // Execute first action (with job claim if needed)
        bool did = ex.executeFirst(plan, w, ctx);
        if(!did) cout << "Could not execute (job unavailable or no plan)\n";

        // If a job was associated with this action, mark complete (toy demo)
        ex.completeJobIfAny(w, ctx);

        // natural drift for interest
        w.set(Keys::hunger(), std::min(100.0, w.get<double>(Keys::hunger(),0.0)+4.0));
        w.set(Keys::thirst(), std::min(100.0, w.get<double>(Keys::thirst(),0.0)+5.0));
        w.set(Keys::temp(),   w.get<double>(Keys::temp(),16.0) - 0.25);
        w.set(Keys::fatigue(),w.get<double>(Keys::fatigue(),0.0) + 1.0);
        ctx.tick += 1.0;
    }

    // Serialize
    std::string js = serializeWorld(w);
    cout << "\nSerialized World:\n" << js << "\n";

    // DOT export for last plan
    // cout << "\nDOT:\n" << planToDOT(ap.planOnce(w,G,L,ctx,rng)) << "\n";

#ifdef GOAP_ENABLE_TESTS
    cout << "\nRunning tests...\n"; return run_all_tests();
#else
    return 0;
#endif
}
#endif // GOAP_DEMO_MAIN

} // namespace goap
#endif // GOAP_PLANNER_HPP
