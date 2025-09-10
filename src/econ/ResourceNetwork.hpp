// src/econ/ResourceNetwork.hpp
// ----------------------------------------------------------------------------------
// A self-contained, STL-only logistics / crafting / hauling network for colony sims.
// C++17, header-only, no external dependencies.
//
// WHAT THIS MODULE PROVIDES
//  - Item & Recipe registries
//  - Inventories (Tile, Container, Agent) with capacity & tag filters
//  - Demand/Offer queues with priorities & TTLs
//  - Greedy planner with multi-pickup bundling -> single-drop HaulTasks
//  - Reservations (reservedOut / reservedIn) to prevent double-hauls
//  - Pluggable path & danger cost callbacks (or Manhattan fallback)
//  - Capacity-aware task slicing at assignment time
//  - JSON save/load (lightweight writer/parser for this schema only)
//  - Debug overlay heatmaps (demand, offer, flow)
//
// DESIGN NOTES
//  - Single-threaded: call all APIs from the simulation thread.
//  - Invariants: Each planned HaulTask bundles multiple pickups of ONE item and
//                finishes with exactly ONE drop stop for the same item.
//  - Reservations ensure that simultaneously planned tasks don't double-book.
//  - "Slicing" lets you carve a large task down to an agent’s carry capacity.
// ----------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <cstdlib>      // strtod
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <functional>
#include <optional>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cassert>
#include <sstream>

// ======== Configuration Macros =====================================================
// You can override these in your build system before including this header.
#ifndef ECON_RN_ASSERT
  #define ECON_RN_ASSERT(x) assert(x)
#endif

#ifndef ECON_RN_INLINE
  #if defined(_MSC_VER)
    #define ECON_RN_INLINE __forceinline
  #else
    #define ECON_RN_INLINE inline __attribute__((always_inline))
  #endif
#endif

#ifndef ECON_RN_JSON_ENABLE
  #define ECON_RN_JSON_ENABLE 1
#endif

#ifndef ECON_RN_DEFAULT_CARRY_CAP
  // Max total item quantity the planner initially bundles into a single HaulTask.
  // Agents with lower capacity can "slice" tasks at assignment time.
  #define ECON_RN_DEFAULT_CARRY_CAP 120u
#endif

#ifndef ECON_RN_MAX_SLOTS
  #define ECON_RN_MAX_SLOTS 2048
#endif

#ifndef ECON_RN_MAX_PUB_TASKS
  #define ECON_RN_MAX_PUB_TASKS 128
#endif

#ifndef ECON_RN_VERSION
  #define ECON_RN_VERSION 2
#endif

// ==================================================================================
namespace econ {

using ItemId = uint16_t;
using RecipeId = uint16_t;
using EntityId = uint32_t;
using InventoryId = uint32_t;

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

// Item tags (bitmask)
enum ItemTag : u32 {
  Tag_None     = 0,
  Tag_Food     = 1u << 0,
  Tag_Raw      = 1u << 1,
  Tag_Fuel     = 1u << 2,
  Tag_Metal    = 1u << 3,
  Tag_Wood     = 1u << 4,
  Tag_Medicine = 1u << 5,
  Tag_Custom6  = 1u << 6,
  Tag_Custom7  = 1u << 7,
  // ...extend as needed
};

struct ItemDef {
  std::string name;
  u16   maxStack = 50;     // Max per slot.
  u32   tags     = Tag_None;
  float mass     = 1.0f;   // For future: weight-aware planning.
};

struct ItemStack {
  ItemId id = 0;
  u16    count = 0;
  bool empty() const { return count == 0; }
};

enum class InventoryKind : u8 { Tile, Container, Agent };

struct InventoryDesc {
  InventoryKind kind = InventoryKind::Tile;
  int x = 0, y = 0;              // Tile position (agents can be -1,-1 if not fixed).
  u16 slots = 16;                // Slots.
  u16 slotSize = 50;             // Max per slot.
  u32 filterAnyTags = Tag_None;  // Accept if (item.tags & filterAnyTags) != 0 (0 = accept any).
  u32 filterNoTags  = Tag_None;  // Reject if (item.tags & filterNoTags) != 0.
  int priority = 0;              // Higher = preferred destination.
  std::string name;              // Optional.
};

struct Inventory {
  InventoryId id = 0;
  InventoryDesc d;
  std::unordered_map<ItemId, u32> content;     // Total counts by item
  std::unordered_map<ItemId, u32> reservedOut; // Promised to leave this inventory
  std::unordered_map<ItemId, u32> reservedIn;  // Promised to arrive to this inventory

  u32 capacity() const {
    return static_cast<u32>(d.slots) * static_cast<u32>(d.slotSize);
  }
  // Available free space for an item (ignores filters).
  u32 freeSpaceFor(ItemId item, u16 /*maxStack*/) const {
    u64 cur = 0;
    for (auto &kv : content) cur += kv.second;
    u64 rin = 0;
    if (auto it = reservedIn.find(item); it != reservedIn.end()) rin = it->second;
    u64 cap = capacity();
    return (cap > (cur + rin)) ? static_cast<u32>(cap - (cur + rin)) : 0u;
  }
};

struct RecipeIO { ItemId id; u16 count; };
struct RecipeDef {
  std::string name;
  std::vector<RecipeIO> in, out;
  float workSeconds = 4.0f;
};

enum class DemandKind : u8 { Consume, CraftInput, StockTarget };

struct Demand {
  InventoryId dst{0};
  ItemId id{0};
  u16 need{0};
  int priority{0};
  float ttl_s{10.0f};     // seconds to live
  DemandKind kind{DemandKind::Consume};
  u64 uid{0};             // Unique id (filled by network)
  float postedAt{0.f};    // Simulation time when posted
};

enum class OfferKind : u8 { Stored, OutputReady };
struct Offer {
  InventoryId src{0};
  ItemId id{0};
  u16 have{0};
  int priority{0};
  OfferKind kind{OfferKind::Stored};
  u64 uid{0};
  float postedAt{0.f};
};

struct HaulTask {
  u64 taskId{0};
  EntityId claimer{0};            // 0 = unclaimed
  float estCost{0};
  bool claimed{false};

  struct Stop {
    enum Type { Pickup, Drop } type{Pickup};
    InventoryId inv{0};
    ItemId id{0};
    u16 qty{0};
  };
  std::vector<Stop> plan;

  // For reservation rollback (populated from plan).
  std::unordered_map<InventoryId, std::unordered_map<ItemId, u32>> resOutByInv;
  std::unordered_map<InventoryId, std::unordered_map<ItemId, u32>> resInByInv;
};

// ==================================================================================
// Small utilities
// ==================================================================================
namespace detail {

ECON_RN_INLINE u64 hashMix(u64 x) {
  // xorshift* style mixer
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  return x * 2685821657736338717ULL;
}

template<typename T>
ECON_RN_INLINE void addTo(std::unordered_map<ItemId, T>& map, ItemId id, T v) {
  auto it = map.find(id);
  if (it == map.end()) map.emplace(id, v);
  else {
    if constexpr (std::is_unsigned<T>::value) {
      if ((std::numeric_limits<T>::max)() - it->second < v) it->second = (std::numeric_limits<T>::max)();
      else it->second += v;
    } else {
      it->second += v;
    }
  }
}

template<typename T>
ECON_RN_INLINE void subFrom(std::unordered_map<ItemId, T>& map, ItemId id, T v) {
  auto it = map.find(id);
  if (it == map.end()) return;
  if (it->second <= v) map.erase(it);
  else it->second -= v;
}

// ---- Minimal JSON for our schema --------------------------------------------------
#if ECON_RN_JSON_ENABLE
struct Json {
  enum Kind { Null, Bool, Num, Str, Arr, Obj };
  Kind k{Null};
  bool b{}; double n{}; std::string s; std::vector<Json> a; std::unordered_map<std::string, Json> o;

  static Json makeNull() { return Json(); }
  static Json makeBool(bool v){ Json j; j.k=Bool; j.b=v; return j; }
  static Json makeNum(double v){ Json j; j.k=Num; j.n=v; return j; }
  static Json makeStr(std::string v){ Json j; j.k=Str; j.s=std::move(v); return j; }
  static Json makeArr(){ Json j; j.k=Arr; return j; }
  static Json makeObj(){ Json j; j.k=Obj; return j; }

  bool isNull()const{return k==Null;}
  bool isBool()const{return k==Bool;}
  bool isNum()const{return k==Num;}
  bool isStr()const{return k==Str;}
  bool isArr()const{return k==Arr;}
  bool isObj()const{return k==Obj;}
};

ECON_RN_INLINE void jsonIndent(std::ostringstream& os, int d){ for(int i=0;i<d;i++) os<<' '; }
ECON_RN_INLINE void jsonWriteEscaped(std::ostringstream& os, const std::string& s){
  os<<'"';
  for(char c: s){
    switch(c){
      case '"': os<<"\\\""; break; case '\\': os<<"\\\\"; break;
      case '\n': os<<"\\n"; break; case '\r': os<<"\\r"; break; case '\t': os<<"\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char buf[7]; std::snprintf(buf,sizeof(buf),"\\u%04x", int((unsigned char)c)); os<<buf; }
        else os<<c;
    }
  }
  os<<'"';
}
ECON_RN_INLINE void jsonToStringRec(const Json& j, std::ostringstream& os, int indent, int depth){
  switch(j.k){
    case Json::Null: os<<"null"; break;
    case Json::Bool: os<<(j.b?"true":"false"); break;
    case Json::Num:  os<<j.n; break;
    case Json::Str:  jsonWriteEscaped(os, j.s); break;
    case Json::Arr: {
      os<<'[';
      for(size_t i=0;i<j.a.size();++i){
        if(i) os<<','; if(indent>=0){ os<<'\n'; jsonIndent(os, (depth+1)*indent); }
        jsonToStringRec(j.a[i], os, indent, depth+1);
      }
      if(indent>=0 && !j.a.empty()){ os<<'\n'; jsonIndent(os, depth*indent); }
      os<<']';
    } break;
    case Json::Obj: {
      os<<'{';
      size_t i=0;
      for(const auto &kv: j.o){
        if(i++) os<<','; if(indent>=0){ os<<'\n'; jsonIndent(os, (depth+1)*indent); }
        jsonWriteEscaped(os, kv.first); os<<':'; if(indent>=0) os<<' ';
        jsonToStringRec(kv.second, os, indent, depth+1);
      }
      if(indent>=0 && !j.o.empty()){ os<<'\n'; jsonIndent(os, depth*indent); }
      os<<'}';
    } break;
  }
}
ECON_RN_INLINE std::string jsonToString(const Json& j, int indent=2){ std::ostringstream os; jsonToStringRec(j, os, indent, 0); return os.str(); }

struct JsonParser {
  const char* p{nullptr}; const char* end{nullptr}; bool ok{true}; std::string err;
  explicit JsonParser(std::string_view sv){ p = sv.data(); end = sv.data()+sv.size(); }
  void skipWS(){ while(p<end && (*p==' '||*p=='\n'||*p=='\t'||*p=='\r')) ++p; }
  bool match(char c){ skipWS(); if(p<end && *p==c){ ++p; return true; } return false; }
  Json parse(){ skipWS(); if(p>=end){ ok=false; err="unexpected end"; return Json::makeNull(); }
    char c=*p; if(c=='{') return parseObj(); if(c=='[') return parseArr(); if(c=='"') return parseStr();
    if(c=='t'||c=='f') return parseBool(); if(c=='n') return parseNull(); return parseNum(); }
  Json parseNull(){ if(end-p>=4 && p[0]=='n'&&p[1]=='u'&&p[2]=='l'&&p[3]=='l'){ p+=4; return Json::makeNull(); } ok=false; err="invalid null"; return Json::makeNull(); }
  Json parseBool(){ if(end-p>=4 && !std::memcmp(p,"true",4)){ p+=4; return Json::makeBool(true); }
                    if(end-p>=5 && !std::memcmp(p,"false",5)){ p+=5; return Json::makeBool(false); } ok=false; err="invalid bool"; return Json::makeNull(); }
  Json parseNum(){ skipWS(); const char* s=p; if(p<end && (*p=='+'||*p=='-')) ++p; bool dot=false,exp=false;
    while(p<end){ char c=*p; if(c>='0'&&c<='9'){ ++p; continue; } if(c=='.'&&!dot){ dot=true; ++p; continue; }
      if((c=='e'||c=='E')&&!exp){ exp=true; ++p; if(p<end && (*p=='+'||*p=='-')) ++p; continue; } break; }
    double v=std::strtod(std::string(s,p-s).c_str(), nullptr); return Json::makeNum(v); }
  Json parseStr(){ if(!match('"')){ ok=false; err="expected string"; return Json::makeNull(); }
    std::string out; for(;;){ if(p>=end){ ok=false; err="unterminated string"; break; } char c=*p++; if(c=='"') break;
      if(c=='\\'){ if(p>=end){ ok=false; err="bad escape"; break; } char e=*p++; switch(e){
        case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break;
        case 'n': out.push_back('\n'); break; case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break;
        case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break;
        case 'u': { if(end-p<4){ ok=false; err="short \\u"; break; } out.append("\\u"); out.append(std::string(p,p+4)); p+=4; } break;
        default: out.push_back(e); break; } } else out.push_back(c); }
    return Json::makeStr(std::move(out)); }
  Json parseArr(){ Json j=Json::makeArr(); ECON_RN_ASSERT(*p=='['); ++p; skipWS(); if(match(']')) return j;
    while(true){ Json v=parse(); if(!ok) return Json::makeNull(); j.a.push_back(std::move(v)); skipWS(); if(match(']')) break; if(!match(',')){ ok=false; err="expected ,"; break; } } return j; }
  Json parseObj(){ Json j=Json::makeObj(); ECON_RN_ASSERT(*p=='{'); ++p; skipWS(); if(match('}')) return j;
    while(true){ skipWS(); if(p>=end||*p!='"'){ ok=false; err="expected key string"; break; } Json k=parseStr(); skipWS();
      if(!match(':')){ ok=false; err="expected :"; break; } Json v=parse(); if(!ok) return Json::makeNull();
      j.o.emplace(std::move(k.s), std::move(v)); skipWS(); if(match('}')) break; if(!match(',')){ ok=false; err="expected ,"; break; } } return j; }
};
#endif // ECON_RN_JSON_ENABLE

} // namespace detail

// ==================================================================================
// ResourceNetwork
// ==================================================================================
class ResourceNetwork {
public:
  ResourceNetwork() = default;

  // --- Item & recipe registries ---------------------------------------------------
  ItemId registerItem(const ItemDef& def){
    auto it = nameToItem_.find(def.name);
    if(it != nameToItem_.end()) return it->second;
    ItemId id = static_cast<ItemId>(items_.size());
    items_.push_back(def);
    nameToItem_[def.name] = id;
    return id;
  }

  RecipeId registerRecipe(const RecipeDef& def){
    RecipeId id = static_cast<RecipeId>(recipes_.size());
    recipes_.push_back(def);
    return id;
  }

  const ItemDef* getItem(ItemId id) const {
    return (id < items_.size()) ? &items_[id] : nullptr;
  }

  // --- Inventories ----------------------------------------------------------------
  InventoryId createInventory(const InventoryDesc& d) {
    Inventory inv; inv.id = nextInv_++; inv.d = d;
    ECON_RN_ASSERT(inv.d.slots <= ECON_RN_MAX_SLOTS);
    invs_.emplace(inv.id, std::move(inv));
    return inv.id;
  }

  bool setInventoryFilter(InventoryId inv, u32 anyTags, u32 noTags){
    if (auto it = invs_.find(inv); it!=invs_.end()){
      it->second.d.filterAnyTags = anyTags;
      it->second.d.filterNoTags  = noTags;
      return true;
    }
    return false;
  }

  // Adds items to an inventory (ignores tag filters; used when items actually move).
  // 'accepted' returns how many were accepted (<= s.count).
  bool addItem(InventoryId inv, ItemStack s, u16& accepted){
    auto it = invs_.find(inv); if(it==invs_.end()) return false;
    auto &invref = it->second;
    const ItemDef* idef = getItem(s.id);
    if(!idef){ accepted = 0; return false; }
    u32 canFit = invref.freeSpaceFor(s.id, idef->maxStack);
    u32 will = std::min<u32>(canFit, s.count);
    if(will==0){ accepted = 0; return true; }
    detail::addTo(invref.content, s.id, will);
    accepted = static_cast<u16>(will);
    return true;
  }

  // Removes items (ignores filters); 'taken' returns how many were removed (<= want).
  bool removeItem(InventoryId inv, ItemId id, u16 want, u16& taken){
    auto it = invs_.find(inv); if(it==invs_.end()) return false;
    auto &invref = it->second;
    auto itc = invref.content.find(id);
    if(itc==invref.content.end()){ taken=0; return true; }
    // Do not remove amounts reservedOut for hauling; remove only free stock.
    u32 reservedOut = 0;
    if (auto ito = invref.reservedOut.find(id); ito != invref.reservedOut.end()) reservedOut = ito->second;
    u32 have = (itc->second > reservedOut) ? (itc->second - reservedOut) : 0;
    u32 will = std::min<u32>(have, want);
    if(will==0){ taken=0; return true; }
    itc->second -= will;
    if(itc->second==0) invref.content.erase(itc);
    taken = static_cast<u16>(will);
    return true;
  }

  const Inventory* getInventory(InventoryId id) const {
    if (auto it = invs_.find(id); it!=invs_.end()) return &it->second;
    return nullptr;
  }

  // --- Demand / Offer posting -----------------------------------------------------
  void postDemand(const Demand& d0){
    Demand d = d0;
    if(d.uid==0) d.uid = ++nextDemandUid_;
    d.postedAt = time_;
    demandsByItem_[d.id].push_back(d);
  }

  void postOffer(const Offer& o0){
    Offer o = o0;
    if(o.uid==0) o.uid = ++nextOfferUid_;
    o.postedAt = time_;
    offersByItem_[o.id].push_back(o);
  }

  // --- Simulation tick ------------------------------------------------------------
  void tick(float dtSeconds){
    time_ += dtSeconds;
    pruneExpired();
    validatePublicTasks_();
    planTasksGreedy_();
  }

  // --- Task fetch / claim / finish ------------------------------------------------
  // Returns the next available task (peek) without capacity constraints.
  bool nextHaulTaskFor(EntityId /*agent*/, HaulTask& out){
    auto it = std::find_if(pubTasks_.begin(), pubTasks_.end(),
                           [](const HaulTask& t){ return !t.claimed; });
    if(it==pubTasks_.end()) return false;
    out = *it;
    return true;
  }

  // Returns the next available task, sliced to at most 'carryCap' if needed.
  // If a slice occurs, the public queue is updated in-place:
  //   - The original task is replaced by the remainder (same taskId).
  //   - A new partial task (new taskId) is inserted before it and returned.
  bool nextHaulTaskFor(EntityId /*agent*/, u32 carryCap, HaulTask& out){
    auto it = std::find_if(pubTasks_.begin(), pubTasks_.end(),
                           [](const HaulTask& t){ return !t.claimed; });
    if(it==pubTasks_.end()) return false;

    // If task fits, just return it.
    u32 total = taskTotalQty_(*it);
    if (total <= carryCap) { out = *it; return true; }

    // Slice to capacity (respecting single item + single drop invariant).
    HaulTask partial, remainder;
    if (!sliceTaskToCapacity_(*it, carryCap, partial, remainder)) {
      // If slicing failed for any reason, just return unsliced.
      out = *it;
      return true;
    }

    // Rebuild reservation maps for both tasks (no inventory deltas, sums unchanged).
    fillTaskReservationMaps_(partial);
    fillTaskReservationMaps_(remainder);

    // Replace in public queue: [partial(new), remainder(keep id of original)]
    remainder.taskId = it->taskId;               // original keeps its id
    partial.taskId   = ++nextTaskId_;            // new id for the slice
    partial.claimed = false; partial.claimer = 0;
    remainder.claimed = false; remainder.claimer = 0;

    // Swap-in remainder and insert partial before it
    *it = remainder;
    pubTasks_.insert(it, partial);

    out = partial;
    return true;
  }

  // Claim a task (prevents other agents from taking it).
  bool claimTask(EntityId agent, u64 taskId){
    for(auto &t : pubTasks_){
      if(t.taskId==taskId && !t.claimed){
        t.claimed = true;
        t.claimer = agent;
        claimedTasks_[taskId] = t; // copy snapshot
        return true;
      }
    }
    return false;
  }

  // Remove the front task if it matches 'taskId' (call right after claim).
  bool popFrontTaskIf(u64 taskId){
    if(pubTasks_.empty()) return false;
    if(pubTasks_.front().taskId != taskId) return false;
    pubTasks_.erase(pubTasks_.begin());
    return true;
  }

  // Cancel a task (public or claimed). Rolls back reservations only.
  bool cancelTask(u64 taskId){
    // Claimed pool
    if (auto it = claimedTasks_.find(taskId); it != claimedTasks_.end()){
      rollbackReservations_(it->second);
      claimedTasks_.erase(it);
      // Also remove from pub queue if present
      for(size_t i=0;i<pubTasks_.size();++i){ if(pubTasks_[i].taskId==taskId){ pubTasks_.erase(pubTasks_.begin()+i); break; } }
      return true;
    }
    // Public queue
    for(size_t i=0;i<pubTasks_.size();++i){
      if(pubTasks_[i].taskId==taskId){
        rollbackReservations_(pubTasks_[i]);
        pubTasks_.erase(pubTasks_.begin()+i);
        return true;
      }
    }
    return false;
  }

  // Complete the task: if ok==true -> apply pickups/drops; then clear reservations.
  void completeTask(u64 taskId, bool ok){
    // If it was claimed, use the claimed snapshot
    if (auto it = claimedTasks_.find(taskId); it != claimedTasks_.end()){
      const HaulTask t = it->second;
      if(ok) applyTaskMovements_(t);
      rollbackReservations_(t);
      claimedTasks_.erase(it);
      // remove any stale copy from public queue
      for(size_t i=0;i<pubTasks_.size();++i){ if(pubTasks_[i].taskId==taskId){ pubTasks_.erase(pubTasks_.begin()+i); break; } }
      return;
    }
    // Otherwise, see if it's still public
    for(size_t i=0;i<pubTasks_.size();++i){
      if(pubTasks_[i].taskId==taskId){
        HaulTask t = pubTasks_[i];
        if(ok) applyTaskMovements_(t);
        rollbackReservations_(t);
        pubTasks_.erase(pubTasks_.begin()+i);
        return;
      }
    }
  }

  // --- Hooks ----------------------------------------------------------------------
  void setPathCostFn (std::function<float(int,int,int,int)> fn){ pathCost_ = std::move(fn); }
  void setTileDangerFn(std::function<float(int)> fn){ dangerCost_ = std::move(fn); }
  void setWorldToTile(std::function<int(float,float)> fn){ worldToTile_ = std::move(fn); }

  // --- Persistence / Debug --------------------------------------------------------
#if ECON_RN_JSON_ENABLE
  std::string toJson(int indent=2) const {
    using detail::Json;
    Json root = Json::makeObj();
    root.o["version"] = Json::makeNum(ECON_RN_VERSION);
    root.o["simTime"] = Json::makeNum(time_);

    // items
    {
      Json arr = Json::makeArr();
      for(size_t i=0;i<items_.size();++i){
        Json j = Json::makeObj();
        j.o["id"]   = Json::makeNum(double(i));
        j.o["name"] = Json::makeStr(items_[i].name);
        j.o["maxStack"] = Json::makeNum(items_[i].maxStack);
        j.o["tags"] = Json::makeNum(double(items_[i].tags));
        j.o["mass"] = Json::makeNum(items_[i].mass);
        arr.a.push_back(std::move(j));
      }
      root.o["items"] = std::move(arr);
    }

    // inventories
    {
      Json arr = Json::makeArr();
      for (auto &kv : invs_){
        const Inventory& inv = kv.second;
        Json j = Json::makeObj();
        j.o["id"] = Json::makeNum(double(inv.id));
        j.o["name"] = Json::makeStr(inv.d.name);
        j.o["kind"] = Json::makeNum(double((int)inv.d.kind));
        j.o["x"] = Json::makeNum(inv.d.x);
        j.o["y"] = Json::makeNum(inv.d.y);
        j.o["slots"] = Json::makeNum(inv.d.slots);
        j.o["slotSize"] = Json::makeNum(inv.d.slotSize);
        j.o["filterAnyTags"] = Json::makeNum(double(inv.d.filterAnyTags));
        j.o["filterNoTags"]  = Json::makeNum(double(inv.d.filterNoTags));
        j.o["priority"]      = Json::makeNum(inv.d.priority);

        Json content = Json::makeArr();
        for(auto &cc : inv.content){
          Json c = Json::makeObj();
          c.o["item"] = Json::makeNum(cc.first);
          c.o["count"] = Json::makeNum(cc.second);
          content.a.push_back(std::move(c));
        }
        j.o["content"] = std::move(content);

        Json rin = Json::makeArr();
        for(auto &cc : inv.reservedIn){
          Json c = Json::makeObj();
          c.o["item"] = Json::makeNum(cc.first);
          c.o["count"] = Json::makeNum(cc.second);
          rin.a.push_back(std::move(c));
        }
        j.o["reservedIn"] = std::move(rin);

        Json rout = Json::makeArr();
        for(auto &cc : inv.reservedOut){
          Json c = Json::makeObj();
          c.o["item"] = Json::makeNum(cc.first);
          c.o["count"] = Json::makeNum(cc.second);
          rout.a.push_back(std::move(c));
        }
        j.o["reservedOut"] = std::move(rout);

        arr.a.push_back(std::move(j));
      }
      root.o["inventories"] = std::move(arr);
    }

    // offers / demands
    {
      Json offs = Json::makeArr();
      for(auto &kv : offersByItem_){
        for(auto &o : kv.second){
          Json j=Json::makeObj();
          j.o["uid"] = Json::makeNum(double(o.uid));
          j.o["src"] = Json::makeNum(double(o.src));
          j.o["item"]= Json::makeNum(double(o.id));
          j.o["have"]= Json::makeNum(double(o.have));
          j.o["priority"] = Json::makeNum(double(o.priority));
          j.o["kind"] = Json::makeNum(double((int)o.kind));
          j.o["postedAt"] = Json::makeNum(o.postedAt);
          offs.a.push_back(std::move(j));
        }
      }
      root.o["offers"] = std::move(offs);

      Json dems = Json::makeArr();
      for(auto &kv : demandsByItem_){
        for(auto &d : kv.second){
          Json j=Json::makeObj();
          j.o["uid"] = Json::makeNum(double(d.uid));
          j.o["dst"] = Json::makeNum(double(d.dst));
          j.o["item"]= Json::makeNum(double(d.id));
          j.o["need"]= Json::makeNum(double(d.need));
          j.o["priority"] = Json::makeNum(double(d.priority));
          j.o["kind"] = Json::makeNum(double((int)d.kind));
          j.o["ttl_s"] = Json::makeNum(d.ttl_s);
          j.o["postedAt"] = Json::makeNum(d.postedAt);
          dems.a.push_back(std::move(j));
        }
      }
      root.o["demands"] = std::move(dems);
    }

    // public tasks (claimed tasks are transient)
    {
      Json tasks = Json::makeArr();
      for(const auto &t : pubTasks_){
        Json j=Json::makeObj();
        j.o["taskId"] = Json::makeNum(double(t.taskId));
        j.o["estCost"] = Json::makeNum(t.estCost);
        j.o["claimed"] = Json::makeBool(t.claimed);
        j.o["claimer"] = Json::makeNum(double(t.claimer));
        Json plan = Json::makeArr();
        for(const auto &s : t.plan){
          Json sj = Json::makeObj();
          sj.o["type"] = Json::makeNum(double((int)s.type));
          sj.o["inv"]  = Json::makeNum(double(s.inv));
          sj.o["item"] = Json::makeNum(double(s.id));
          sj.o["qty"]  = Json::makeNum(double(s.qty));
          plan.a.push_back(std::move(sj));
        }
        j.o["plan"] = std::move(plan);
        tasks.a.push_back(std::move(j));
      }
      root.o["tasks"] = std::move(tasks);
    }

    return detail::jsonToString(root, indent);
  }

  bool fromJson(std::string_view sv){
    detail::JsonParser p(sv);
    detail::Json root = p.parse();
    if(!p.ok || !root.isObj()) return false;
    clearAll();

    if (auto it = root.o.find("simTime"); it!=root.o.end() && it->second.isNum())
      time_ = float(it->second.n);

    // items
    if (auto it = root.o.find("items"); it!=root.o.end() && it->second.isArr()){
      for(auto &j : it->second.a){
        if(!j.isObj()) continue;
        ItemDef d;
        if(auto n=j.o.find("name"); n!=j.o.end() && n->second.isStr()) d.name = n->second.s;
        if(auto ms=j.o.find("maxStack"); ms!=j.o.end() && ms->second.isNum()) d.maxStack = (u16)ms->second.n;
        if(auto tg=j.o.find("tags"); tg!=j.o.end() && tg->second.isNum()) d.tags = (u32)tg->second.n;
        if(auto ma=j.o.find("mass"); ma!=j.o.end() && ma->second.isNum()) d.mass = float(ma->second.n);
        registerItem(d);
      }
    }

    // inventories
    if (auto it = root.o.find("inventories"); it!=root.o.end() && it->second.isArr()){
      for(auto &j : it->second.a){
        if(!j.isObj()) continue;
        InventoryDesc d;
        if(auto nm=j.o.find("name"); nm!=j.o.end() && nm->second.isStr()) d.name = nm->second.s;
        if(auto kd=j.o.find("kind"); kd!=j.o.end() && kd->second.isNum()) d.kind = (InventoryKind)(int)kd->second.n;
        if(auto x =j.o.find("x");    x!=j.o.end() && x->second.isNum())     d.x = (int)x->second.n;
        if(auto y =j.o.find("y");    y!=j.o.end() && y->second.isNum())     d.y = (int)y->second.n;
        if(auto sl=j.o.find("slots"); sl!=j.o.end() && sl->second.isNum())  d.slots = (u16)sl->second.n;
        if(auto ss=j.o.find("slotSize"); ss!=j.o.end() && ss->second.isNum()) d.slotSize = (u16)ss->second.n;
        if(auto at=j.o.find("filterAnyTags"); at!=j.o.end() && at->second.isNum()) d.filterAnyTags = (u32)at->second.n;
        if(auto nt=j.o.find("filterNoTags");  nt!=j.o.end() && nt->second.isNum()) d.filterNoTags  = (u32)nt->second.n;
        if(auto pr=j.o.find("priority"); pr!=j.o.end() && pr->second.isNum()) d.priority = (int)pr->second.n;

        InventoryId id = createInventory(d);

        if(auto cc=j.o.find("content"); cc!=j.o.end() && cc->second.isArr()){
          for(auto &c : cc->second.a){
            if(!c.isObj()) continue;
            ItemId item=0; u32 count=0;
            if(auto itI=c.o.find("item"); itI!=c.o.end() && itI->second.isNum()) item=(ItemId)itI->second.n;
            if(auto itC=c.o.find("count"); itC!=c.o.end() && itC->second.isNum()) count=(u32)itC->second.n;
            invs_[id].content[item] = count;
          }
        }
        if(auto cc=j.o.find("reservedIn"); cc!=j.o.end() && cc->second.isArr()){
          for(auto &c : cc->second.a){
            if(!c.isObj()) continue;
            ItemId item=0; u32 count=0;
            if(auto itI=c.o.find("item"); itI!=c.o.end() && itI->second.isNum()) item=(ItemId)itI->second.n;
            if(auto itC=c.o.find("count"); itC!=c.o.end() && itC->second.isNum()) count=(u32)itC->second.n;
            invs_[id].reservedIn[item] = count;
          }
        }
        if(auto cc=j.o.find("reservedOut"); cc!=j.o.end() && cc->second.isArr()){
          for(auto &c : cc->second.a){
            if(!c.isObj()) continue;
            ItemId item=0; u32 count=0;
            if(auto itI=c.o.find("item"); itI!=c.o.end() && itI->second.isNum()) item=(ItemId)itI->second.n;
            if(auto itC=c.o.find("count"); itC!=c.o.end() && itC->second.isNum()) count=(u32)itC->second.n;
            invs_[id].reservedOut[item] = count;
          }
        }
      }
    }

    // offers
    if (auto it = root.o.find("offers"); it!=root.o.end() && it->second.isArr()){
      for(auto &j : it->second.a){
        if(!j.isObj()) continue;
        Offer o;
        if(auto uid= j.o.find("uid"); uid!=j.o.end() && uid->second.isNum()) o.uid = (u64)uid->second.n;
        if(auto src= j.o.find("src"); src!=j.o.end() && src->second.isNum()) o.src = (InventoryId)src->second.n;
        if(auto itI= j.o.find("item"); itI!=j.o.end() && itI->second.isNum()) o.id  = (ItemId)itI->second.n;
        if(auto hv = j.o.find("have"); hv !=j.o.end() && hv->second.isNum()) o.have= (u16)hv->second.n;
        if(auto pr = j.o.find("priority"); pr!=j.o.end() && pr->second.isNum()) o.priority = (int)pr->second.n;
        if(auto kd = j.o.find("kind"); kd!=j.o.end() && kd->second.isNum()) o.kind = (OfferKind)(int)kd->second.n;
        if(auto pa = j.o.find("postedAt"); pa!=j.o.end() && pa->second.isNum()) o.postedAt = (float)pa->second.n;
        if(o.uid==0) o.uid = ++nextOfferUid_;
        offersByItem_[o.id].push_back(o);
      }
    }

    // demands
    if (auto it = root.o.find("demands"); it!=root.o.end() && it->second.isArr()){
      for(auto &j : it->second.a){
        if(!j.isObj()) continue;
        Demand d;
        if(auto uid= j.o.find("uid"); uid!=j.o.end() && uid->second.isNum()) d.uid = (u64)uid->second.n;
        if(auto dst= j.o.find("dst"); dst!=j.o.end() && dst->second.isNum()) d.dst = (InventoryId)dst->second.n;
        if(auto itI= j.o.find("item"); itI!=j.o.end() && itI->second.isNum()) d.id  = (ItemId)itI->second.n;
        if(auto nd = j.o.find("need"); nd !=j.o.end() && nd->second.isNum()) d.need= (u16)nd->second.n;
        if(auto pr = j.o.find("priority"); pr!=j.o.end() && pr->second.isNum()) d.priority = (int)pr->second.n;
        if(auto kd = j.o.find("kind"); kd!=j.o.end() && kd->second.isNum()) d.kind = (DemandKind)(int)kd->second.n;
        if(auto tt = j.o.find("ttl_s"); tt!=j.o.end() && tt->second.isNum()) d.ttl_s = (float)tt->second.n;
        if(auto pa = j.o.find("postedAt"); pa!=j.o.end() && pa->second.isNum()) d.postedAt = (float)pa->second.n;
        if(d.uid==0) d.uid = ++nextDemandUid_;
        demandsByItem_[d.id].push_back(d);
      }
    }

    // public tasks
    if (auto it = root.o.find("tasks"); it!=root.o.end() && it->second.isArr()){
      for(auto &j : it->second.a){
        if(!j.isObj()) continue;
        HaulTask t;
        if(auto id= j.o.find("taskId"); id!=j.o.end() && id->second.isNum()) t.taskId = (u64)id->second.n;
        if(auto ec= j.o.find("estCost"); ec!=j.o.end() && ec->second.isNum()) t.estCost = (float)ec->second.n;
        if(auto cl= j.o.find("claimed"); cl!=j.o.end() && cl->second.isBool()) t.claimed = cl->second.b;
        if(auto cr= j.o.find("claimer"); cr!=j.o.end() && cr->second.isNum()) t.claimer = (EntityId)cr->second.n;
        if(auto pl= j.o.find("plan"); pl!=j.o.end() && pl->second.isArr()){
          for(auto &s : pl->second.a){
            if(!s.isObj()) continue;
            HaulTask::Stop st;
            if(auto ty=s.o.find("type"); ty!=s.o.end() && ty->second.isNum()) st.type = (HaulTask::Stop::Type)(int)ty->second.n;
            if(auto iv=s.o.find("inv");  iv!=s.o.end() && iv->second.isNum()) st.inv  = (InventoryId)iv->second.n;
            if(auto itI=s.o.find("item"); itI!=s.o.end() && itI->second.isNum()) st.id   = (ItemId)itI->second.n;
            if(auto qt=s.o.find("qty");  qt!=s.o.end() && qt->second.isNum()) st.qty  = (u16)qt->second.n;
            t.plan.push_back(st);
          }
        }
        // Ensure reservation maps reflect the plan and apply to inventories.
        fillTaskReservationMaps_(t);
        applyReservationsForTask_(t);
        pubTasks_.push_back(std::move(t));
      }
    }

    // update id counters (best effort)
    for(const auto &t : pubTasks_) nextTaskId_ = std::max(nextTaskId_, t.taskId);
    for(const auto &kv : offersByItem_) for (const auto &o : kv.second) nextOfferUid_ = std::max(nextOfferUid_, o.uid);
    for(const auto &kv : demandsByItem_) for (const auto &d : kv.second) nextDemandUid_ = std::max(nextDemandUid_, d.uid);
    return true;
  }
#else
  std::string toJson(int=0) const { return "{}"; }
  bool fromJson(std::string_view){ return false; }
#endif

  // Returns a brief human-readable snapshot.
  std::string dumpSummary() const {
    std::ostringstream os;
    os<<"ResourceNetwork v"<<ECON_RN_VERSION<<"\n";
    os<<"items="<<items_.size()<<", recipes="<<recipes_.size()<<"\n";
    os<<"inventories="<<invs_.size()<<", pubTasks="<<pubTasks_.size()<<", claimed="<<claimedTasks_.size()<<"\n";
    size_t dem=0, off=0;
    for(auto &kv: demandsByItem_) dem += kv.second.size();
    for(auto &kv: offersByItem_)  off += kv.second.size();
    os<<"demands="<<dem<<", offers="<<off<<"\n";
    return os.str();
  }

  // Upload a small RGBA heatmap. mode: 0=demand, 1=offer, 2=flow (planned drops).
  void debugOverlayUpload(std::vector<u32>& rgba, int& w, int& h, int mode) const {
    int minx=INT32_MAX, miny=INT32_MAX, maxx=INT32_MIN, maxy=INT32_MIN;
    auto consider = [&](int x, int y){ minx = std::min(minx,x); miny=std::min(miny,y); maxx=std::max(maxx,x); maxy=std::max(maxy,y); };
    for(auto &kv: invs_) consider(kv.second.d.x, kv.second.d.y);
    if(minx==INT32_MAX){ w=h=0; rgba.clear(); return; }

    int W = w, H = h;
    if(W<=0||H<=0){
      W = std::min(160, std::max(1, maxx - minx + 1));
      H = std::min(160, std::max(1, maxy - miny + 1));
    }
    rgba.assign((size_t)W*H, 0u);

    auto put = [&](int x, int y, float v){
      int gx = x - minx; int gy = y - miny;
      if(gx<0||gy<0||gx>=W||gy>=H) return;
      v = std::max(0.f, std::min(1.f, v));
      u8 r = (u8)(255 * v);
      u8 g = (u8)(255 * (1.0f - std::fabs(v-0.5f)*2));
      u8 b = (u8)(255 * (1.0f - v));
      rgba[(size_t)gy*W + gx] = (u32(r)<<24) | (u32(g)<<16) | (u32(b)<<8) | 255u;
    };

    // accumulate magnitude by inv
    std::unordered_map<InventoryId, double> mag;
    if(mode==0){ // demand
      for(auto &kv : demandsByItem_){
        for(auto &d : kv.second){
          mag[d.dst] += d.need * (1.0 + 0.05 * d.priority);
        }
      }
    } else if(mode==1){ // offers
      for(auto &kv : offersByItem_){
        for(auto &o : kv.second){
          mag[o.src] += o.have * (1.0 + 0.05 * o.priority);
        }
      }
    } else { // flow from tasks
      for(auto &t : pubTasks_){
        for(auto &s : t.plan){
          if(s.type==HaulTask::Stop::Drop) mag[s.inv] += s.qty;
        }
      }
    }
    // normalize and paint
    double maxv = 0.0;
    for(auto &kv: mag) maxv = std::max(maxv, kv.second);
    if(maxv < 1e-6) maxv = 1.0;
    for(auto &kv: mag){
      auto it = invs_.find(kv.first);
      if(it==invs_.end()) continue;
      put(it->second.d.x, it->second.d.y, float(kv.second / maxv));
    }

    w = W; h = H;
  }

  // Clear all state.
  void clearAll(){
    items_.clear(); nameToItem_.clear();
    recipes_.clear();
    invs_.clear(); nextInv_=1;
    demandsByItem_.clear(); offersByItem_.clear();
    pubTasks_.clear(); claimedTasks_.clear();
    time_=0.f; nextTaskId_=0; nextDemandUid_=0; nextOfferUid_=0;
  }

private:
  // --------------------------------------------------------------------------------
  // Data
  // --------------------------------------------------------------------------------
  std::vector<ItemDef> items_;
  std::unordered_map<std::string, ItemId> nameToItem_;
  std::vector<RecipeDef> recipes_;

  std::unordered_map<InventoryId, Inventory> invs_;
  InventoryId nextInv_{1};

  std::unordered_map<ItemId, std::vector<Demand>> demandsByItem_;
  std::unordered_map<ItemId, std::vector<Offer>>  offersByItem_;

  std::vector<HaulTask> pubTasks_;
  std::unordered_map<u64, HaulTask> claimedTasks_;

  float time_{0.f};
  u64 nextTaskId_{0};
  u64 nextDemandUid_{0};
  u64 nextOfferUid_{0};

  // callbacks
  std::function<float(int,int,int,int)> pathCost_;
  std::function<float(int)> dangerCost_;
  std::function<int(float,float)> worldToTile_;

  // --------------------------------------------------------------------------------
  // Helpers
  // --------------------------------------------------------------------------------
  // Whether an inventory accepts an item given tag filters.
  bool inventoryAccepts_(const Inventory& inv, ItemId item) const {
    const ItemDef* it = getItem(item);
    if(!it) return false;
    if(inv.d.filterAnyTags != Tag_None){
      if( (it->tags & inv.d.filterAnyTags) == 0 ) return false;
    }
    if(inv.d.filterNoTags != Tag_None){
      if( (it->tags & inv.d.filterNoTags) != 0 ) return false;
    }
    return true;
  }

  // Quantity available to haul from 'inv' for item (content minus reservedOut).
  u32 availableToOffer_(const Inventory& inv, ItemId item) const {
    u32 have = 0;
    if (auto it = inv.content.find(item); it != inv.content.end()) have = it->second;
    u32 resOut = 0;
    if (auto it = inv.reservedOut.find(item); it != inv.reservedOut.end()) resOut = it->second;
    return (have > resOut) ? (have - resOut) : 0u;
  }

  // Simple route cost estimate (path + danger surcharge + destination preference).
  float routeCost_(const Inventory& a, const Inventory& b) const {
    float base=0.f;
    if(pathCost_) base = pathCost_(a.d.x,a.d.y,b.d.x,b.d.y);
    else { base = float(std::abs(a.d.x-b.d.x) + std::abs(a.d.y-b.d.y)); }
    if(dangerCost_){
      float da = dangerCost_(a.d.x ^ (a.d.y<<16));
      float db = dangerCost_(b.d.x ^ (b.d.y<<16));
      base += (da + db) * 0.5f;
    }
    // Prefer higher-priority destinations slightly (negative cost).
    base += float(-0.10 * b.d.priority);
    return base;
  }

  // Sum quantities in pickups (assumes single item throughout).
  static u32 taskTotalQty_(const HaulTask& t){
    u32 q=0;
    for (auto &s : t.plan) if (s.type==HaulTask::Stop::Pickup) q += s.qty;
    return q;
  }

  static ItemId taskItemId_(const HaulTask& t){
    for (auto &s : t.plan) if (s.type==HaulTask::Stop::Pickup) return s.id;
    for (auto &s : t.plan) if (s.type==HaulTask::Stop::Drop)   return s.id;
    return ItemId(0);
  }

  static InventoryId taskDropInv_(const HaulTask& t){
    for (auto &s : t.plan) if (s.type==HaulTask::Stop::Drop) return s.inv;
    return InventoryId(0);
  }

  static bool taskHasSingleDropAndOneItem_(const HaulTask& t){
    ItemId it = ItemId(0); int drops=0;
    for (auto &s : t.plan){
      if (it==ItemId(0)) it=s.id;
      else if (s.id!=it) return false;
      if (s.type==HaulTask::Stop::Drop) ++drops;
    }
    return drops==1;
  }

  // Slice a task into (partial<=cap, remainder). Invariant: same item & single drop.
  bool sliceTaskToCapacity_(const HaulTask& src, u32 cap, HaulTask& partial, HaulTask& remainder){
    if (!taskHasSingleDropAndOneItem_(src)) return false;
    const u32 total = taskTotalQty_(src);
    if (total <= cap) { partial = src; remainder = src; return true; } // (unused path)
    if (cap==0) return false;

    partial = {}; remainder = {};
    partial.estCost = 0.f; remainder.estCost = 0.f;
    partial.claimed=false; remainder.claimed=false;
    ItemId item = taskItemId_(src);
    InventoryId dropInv = taskDropInv_(src);

    u32 takeLeft = cap;
    u32 leftForRemainder = total - cap;

    // Partition pickups preserving order/cost roughness
    for (const auto &s : src.plan){
      if (s.type==HaulTask::Stop::Pickup){
        u16 take = (u16)std::min<u32>(s.qty, takeLeft);
        u16 keep = (u16)(s.qty - take);
        if (take>0) { partial.plan.push_back({HaulTask::Stop::Pickup, s.inv, item, take}); partial.estCost += 1.0f; }
        if (keep>0) { remainder.plan.push_back({HaulTask::Stop::Pickup, s.inv, item, keep}); remainder.estCost += 1.0f; }
        takeLeft -= take;
      }
    }

    // Single drop each
    if (cap > 0) partial.plan.push_back({HaulTask::Stop::Drop, dropInv, item, (u16)cap});
    if (leftForRemainder > 0) remainder.plan.push_back({HaulTask::Stop::Drop, dropInv, item, (u16)leftForRemainder});

    // cost: inherit original approximate cost proportionally
    if (total>0) {
      partial.estCost   = src.estCost * (float(cap) / float(total));
      remainder.estCost = src.estCost * (float(leftForRemainder) / float(total));
    }
    return true;
  }

  // Recompute reservation maps from a plan. (Does NOT touch inventories.)
  static void fillTaskReservationMaps_(HaulTask& t){
    t.resOutByInv.clear(); t.resInByInv.clear();
    ItemId item = taskItemId_(t);
    u32 drop=0;
    for (auto &s : t.plan){
      if (s.type==HaulTask::Stop::Pickup)
        detail::addTo(t.resOutByInv[s.inv], item, (u32)s.qty);
      else if (s.type==HaulTask::Stop::Drop)
        drop += s.qty;
    }
    if (drop>0){
      InventoryId dst = taskDropInv_(t);
      detail::addTo(t.resInByInv[dst], item, drop);
    }
  }

  void applyReservationsForTask_(const HaulTask& t){
    for (const auto &iv : t.resOutByInv){
      auto invIt = invs_.find(iv.first); if(invIt==invs_.end()) continue;
      for (const auto &kv : iv.second) detail::addTo(invIt->second.reservedOut, kv.first, kv.second);
    }
    for (const auto &iv : t.resInByInv){
      auto invIt = invs_.find(iv.first); if(invIt==invs_.end()) continue;
      for (const auto &kv : iv.second) detail::addTo(invIt->second.reservedIn, kv.first, kv.second);
    }
  }

  void releaseReservationsForTask_(const HaulTask& t){
    for (const auto &iv : t.resOutByInv){
      auto invIt = invs_.find(iv.first); if(invIt==invs_.end()) continue;
      for (const auto &kv : iv.second) detail::subFrom(invIt->second.reservedOut, kv.first, kv.second);
    }
    for (const auto &iv : t.resInByInv){
      auto invIt = invs_.find(iv.first); if(invIt==invs_.end()) continue;
      for (const auto &kv : iv.second) detail::subFrom(invIt->second.reservedIn, kv.first, kv.second);
    }
  }

  // Apply movements for a completed task (subtract pickups at sources, add to destination).
  void applyTaskMovements_(const HaulTask& t){
    for(const auto &s : t.plan){
      auto itInv = invs_.find(s.inv); if(itInv==invs_.end()) continue;
      Inventory &inv = itInv->second;
      if(s.type==HaulTask::Stop::Pickup){
        auto itc = inv.content.find(s.id);
        if(itc!=inv.content.end()){
          u32 q = std::min<u32>(itc->second, s.qty);
          itc->second -= q; if(itc->second==0) inv.content.erase(itc);
        }
      } else { // Drop
        detail::addTo(inv.content, s.id, (u32)s.qty);
      }
    }
  }

  void rollbackReservations_(const HaulTask& t){
    releaseReservationsForTask_(t);
  }

  // Remove expired demands, and offers with no availability.
  void pruneExpired(){
    // Demands: TTL
    for(auto &kv : demandsByItem_){
      auto &vec = kv.second;
      vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const Demand& d){
        return (time_ - d.postedAt) > d.ttl_s;
      }), vec.end());
    }
    // Offers: source invalid or no stock
    for(auto &kv : offersByItem_){
      auto &vec = kv.second;
      vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const Offer& o){
        auto itInv = invs_.find(o.src);
        if(itInv==invs_.end()) return true;
        return availableToOffer_(itInv->second, o.id) == 0;
      }), vec.end());
    }
  }

  // Validate public tasks still make sense (sources/destination exist).
  void validatePublicTasks_(){
    // We keep the reservations, but if an inventory disappeared, roll back the task.
    for(size_t i=0;i<pubTasks_.size();){
      const HaulTask& t = pubTasks_[i];
      bool invalid=false;
      for(const auto& s : t.plan){
        if (invs_.find(s.inv)==invs_.end()){ invalid=true; break; }
      }
      if (invalid){
        rollbackReservations_(t);
        pubTasks_.erase(pubTasks_.begin()+i);
      } else ++i;
    }
  }

  // Planner: greedily pair demands with cheapest sources; bundle pickups; publish tasks.
  void planTasksGreedy_(){
    if(pubTasks_.size() > ECON_RN_MAX_PUB_TASKS) return;

    // For each item type
    for(auto &kvDem : demandsByItem_){
      ItemId item = kvDem.first;
      auto &dems = kvDem.second;
      if(dems.empty()) continue;

      // Sort demands by (priority desc, time asc)
      std::stable_sort(dems.begin(), dems.end(), [](const Demand& a, const Demand& b){
        if(a.priority != b.priority) return a.priority > b.priority;
        return a.postedAt < b.postedAt;
      });

      // Snapshot offers (mutate local copy; commit back)
      auto itOffs = offersByItem_.find(item);
      if(itOffs==offersByItem_.end() || itOffs->second.empty()) continue;
      std::vector<Offer> offs = itOffs->second;
      // Remove zero-available
      offs.erase(std::remove_if(offs.begin(), offs.end(), [&](const Offer&o){
        auto itInv = invs_.find(o.src); if(itInv==invs_.end()) return true;
        return availableToOffer_(itInv->second, item) == 0;
      }), offs.end());
      if(offs.empty()) continue;

      std::vector<Demand> newDemands; newDemands.reserve(dems.size());

      for(const Demand& d : dems){
        // Validate dest
        auto itDstInv = invs_.find(d.dst); if(itDstInv==invs_.end()) continue;
        Inventory &dst = itDstInv->second;
        if(!inventoryAccepts_(dst, item)) { newDemands.push_back(d); continue; }

        u32 need = d.need;
        if(need==0) continue;

        // Destination capacity
        const ItemDef* idef = getItem(item); if(!idef) continue;
        u32 freeSpace = dst.freeSpaceFor(item, idef->maxStack);
        if(freeSpace==0) { newDemands.push_back(d); continue; }
        need = std::min<u32>(need, freeSpace);

        // Candidate sources sorted by (route cost asc, offer priority desc)
        struct Cand { size_t offIdx; InventoryId src; float cost; u32 avail; int pr; };
        std::vector<Cand> cands; cands.reserve(offs.size());
        for(size_t i=0;i<offs.size();++i){
          const auto &o = offs[i];
          auto itSrcInv = invs_.find(o.src);
          if(itSrcInv==invs_.end()) continue;
          const Inventory &src = itSrcInv->second;
          u32 avail = std::min<u32>( (u32)o.have, availableToOffer_(src, item) );
          if(avail==0) continue;
          cands.push_back({i, o.src, routeCost_(src, dst), avail, o.priority});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand&a, const Cand&b){
          if(a.cost != b.cost) return a.cost < b.cost;
          return a.pr > b.pr;
        });
        if(cands.empty()) { newDemands.push_back(d); continue; }

        // Build task plan up to carry cap
        HaulTask t; t.taskId = ++nextTaskId_; t.estCost = 0.f; t.claimed=false;
        const u32 carryCap = ECON_RN_DEFAULT_CARRY_CAP;
        u32 picked = 0;

        for(const auto &c : cands){
          if(need==0 || picked>=carryCap) break;
          u32 take = std::min<u32>(c.avail, std::min<u32>(need, carryCap - picked));
          if(take==0) continue;
          t.plan.push_back({HaulTask::Stop::Pickup, c.src, item, (u16)take});
          picked += take;
          need   -= take;
          t.estCost += c.cost;

          // reduce local copy of the offer
          offs[c.offIdx].have = (u16)( (offs[c.offIdx].have > take) ? (offs[c.offIdx].have - take) : 0 );
        }

        if (picked>0){
          // Single drop at destination
          t.plan.push_back({HaulTask::Stop::Drop, dst.id, item, (u16)picked});

          // Build reservation maps and APPLY reservations to inventories
          fillTaskReservationMaps_(t);
          applyReservationsForTask_(t);

          pubTasks_.push_back(std::move(t));

          // if still need remaining, keep a residual for next planning pass
          if(need>0){
            Demand residual = d; residual.need = (u16)need;
            newDemands.push_back(residual);
          }
        } else {
          // keep unmet demand
          newDemands.push_back(d);
        }
      }

      // Replace demand vector with residuals
      kvDem.second.swap(newDemands);
      // Commit offers mutations
      itOffs->second = std::move(offs);
      if(pubTasks_.size() > ECON_RN_MAX_PUB_TASKS) return;
    }
  }

}; // class ResourceNetwork

// ==================================================================================
// Example usage (pseudocode)
//
//   econ::ResourceNetwork rn;
//   auto wood = rn.registerItem({"Wood", 100, econ::Tag_Wood});
//   econ::InventoryId forest = rn.createInventory({econ::InventoryKind::Tile,  2,2, 16,100, 0,0,0, "Forest Pile"});
//   econ::InventoryId stock  = rn.createInventory({econ::InventoryKind::Container, 10,8, 32,100, econ::Tag_Wood,0, 5, "Main Stock"});
//
//   uint16_t acc=0; rn.addItem(forest, {wood, 240}, acc);
//   rn.postOffer({forest, wood, 240, 0, econ::OfferKind::Stored});
//   rn.postDemand({stock,  wood, 180, 5, 60.0f, econ::DemandKind::StockTarget});
//
//   rn.tick(0.1f);
//
//   // Capacity-aware fetch (e.g., agent can carry 60 items):
//   econ::HaulTask t;
//   if (rn.nextHaulTaskFor(/*agent*/ 1, /*carryCap*/ 60, t)) {
//     rn.claimTask(1, t.taskId);
//     rn.popFrontTaskIf(t.taskId);
//     // ... run the plan ...
//     rn.completeTask(t.taskId, /*ok=*/true);
//   }
//
// ==================================================================================
} // namespace econ
