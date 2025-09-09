// src/game/Storyteller.cpp
// Single-file "incident director" for Colony-Game.
// No external deps. Deterministic RNG via seed.
// Expand with more Incident classes & tuning to ~3000 LOC.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cg {

// ---------------- Public surface (kept here so no header is needed) -------------
struct StorytellerBindings {
  // colony snapshot
  std::function<int()>                getColonistCount;
  std::function<int()>                getWealth;
  std::function<int()>                getHostileCount;
  std::function<int()>                getAverageMood; // 0..100
  std::function<int()>                getDayIndex;

  // actuators
  std::function<void(int)>            spawnRaid;        // strength points
  std::function<void(const char*,int)>grantResource;    // id, amount
  std::function<void(const std::string&)> toast;        // HUD/log
};

void Storyteller_Init(const StorytellerBindings& b, uint64_t seed);
void Storyteller_Update(float dtSeconds);
void Storyteller_Save(std::ostream& out);
bool Storyteller_Load(std::istream& in);

// ---------------- Implementation ------------------------------------------------
namespace {

using Clock = std::chrono::steady_clock;

// Small PRNG wrapper (deterministic)
struct Rng {
  std::mt19937_64 eng;
  explicit Rng(uint64_t seed) : eng(seed ? seed : 0xC01onyULL) {}
  int   nextInt(int lo, int hi) { std::uniform_int_distribution<int> d(lo,hi); return d(eng); }
  float next01() { std::uniform_real_distribution<float> d(0.f,1.f); return d(eng); }
};

// Snapshot of current colony for decision-making
struct ColonySnap {
  int colonists = 1;
  int wealth = 0;       // abstract points
  int hostiles = 0;
  int mood = 60;        // 0..100
  int day = 0;
};

// Budget & pacing state
struct Pacing {
  // Threat points we can spend on negative incidents this day.
  float threatBudget = 0.f;
  // Cooldowns by category
  float cdRaid = 0.f, cdDisease = 0.f, cdWeather = 0.f, cdGood = 0.f;
  // Time since last incident (seconds)
  float sinceIncident = 0.f;
};

// Basic incident interface
enum class IncidentKind { Raid, Disease, HeatWave, ColdSnap, Fire, Trader, DropPod, Animals, MoodBreak, Quest };

struct IncidentCtx {
  ColonySnap snap;
  Rng*       rng;
  StorytellerBindings* api;
  // Severity target 0..1, converted into points per incident type.
  float severity = 0.5f;
};

struct Incident {
  IncidentKind kind;
  float        weightBase; // baseline weight
  float        cooldown;   // seconds
  float        minSpacing; // seconds since any incident
  // Decide if this incident can fire now and suggest its point cost (if negative).
  virtual bool canFire(const IncidentCtx& c) const = 0;
  virtual float pointsCost(const IncidentCtx& c) const { (void)c; return 0.f; } // negative uses budget
  virtual void fire(const IncidentCtx& c) = 0;
  virtual ~Incident() = default;
};

// --- Helper: weighted choice ----------------------------------------------------
template <class T, class WFn>
const T* weightedPick(const std::vector<T>& arr, WFn w, Rng& rng) {
  double total = 0.0;
  for (auto& x : arr) {
    double wx = (double)w(x);
    if (wx > 0) total += wx;
  }
  if (total <= 0) return nullptr;
  double r = std::uniform_real_distribution<double>(0.0, total)(rng.eng);
  for (auto& x : arr) {
    double wx = (double)w(x);
    if (wx <= 0) continue;
    if (r <= wx) return &x;
    r -= wx;
  }
  return nullptr;
}

// --- Concrete incidents (add more to grow file) ---------------------------------

// Raiders: spend threat points based on colonists + wealth. Blocked by raid CD.
struct IncRaid final : Incident {
  IncRaid() { kind = IncidentKind::Raid; weightBase = 1.0f; cooldown = 60.f*6; minSpacing=60.f*3; }
  bool canFire(const IncidentCtx& c) const override {
    if (c.snap.colonists <= 0) return false;
    return true;
  }
  float pointsCost(const IncidentCtx& c) const override {
    // Threat scales with wealth and colonists; mood reduces it a bit.
    float base = 25.f + c.snap.colonists * 12.f + (c.snap.wealth * 0.02f);
    base *= (0.75f + (1.0f - c.snap.mood / 100.f) * 0.25f); // bad mood -> slightly lower pressure
    // Severity morphs scale.
    base *= (0.5f + c.severity);
    return -base; // negative consumes budget
  }
  void fire(const IncidentCtx& c) override {
    int points = (int)std::max(10.f, -pointsCost(c));
    if (c.api->spawnRaid) c.api->spawnRaid(points);
    if (c.api->toast) c.api->toast("⚔️ Raid warning! Strength ~" + std::to_string(points));
  }
};

// Trader visit: good event; gives small resources. Affects goodwill pacing.
struct IncTrader final : Incident {
  IncTrader() { kind = IncidentKind::Trader; weightBase = 0.6f; cooldown = 60.f*8; minSpacing=60.f*2; }
  bool canFire(const IncidentCtx& c) const override {
    return c.snap.colonists >= 1;
  }
  void fire(const IncidentCtx& c) override {
    if (c.api->toast) c.api->toast("🧳 A trader caravan arrives.");
    // Optionally drop some goods as a teaser:
    if (c.api->grantResource) {
      int amt = 20 + c.rng->nextInt(0, 60);
      c.api->grantResource("silver", amt);
    }
  }
};

// Drop-pod resource cache: immediate reward.
struct IncDropPod final : Incident {
  IncDropPod() { kind = IncidentKind::DropPod; weightBase = 0.8f; cooldown = 60.f*4; minSpacing=60.f*1.5f; }
  bool canFire(const IncidentCtx& c) const override { return c.snap.colonists >= 1; }
  void fire(const IncidentCtx& c) override {
    if (c.api->toast) c.api->toast("📦 A mysterious supply pod crashes nearby!");
    if (c.api->grantResource) {
      static const char* ids[] = {"steel","wood","medicine","components"};
      const char* id = ids[c.rng->nextInt(0,3)];
      int amt = 30 + c.rng->nextInt(0, 70);
      c.api->grantResource(id, amt);
    }
  }
};

// Weather: Heat wave / Cold snap
struct IncHeatWave final : Incident {
  IncHeatWave(){ kind=IncidentKind::HeatWave; weightBase=0.35f; cooldown=60.f*10; minSpacing=60.f*2; }
  bool canFire(const IncidentCtx& c) const override { return c.snap.colonists>0; }
  void fire(const IncidentCtx& c) override {
    if (c.api->toast) c.api->toast("☀️ Heat wave! Keep colonists hydrated and indoors.");
  }
};
struct IncColdSnap final : Incident {
  IncColdSnap(){ kind=IncidentKind::ColdSnap; weightBase=0.35f; cooldown=60.f*10; minSpacing=60.f*2; }
  bool canFire(const IncidentCtx& c) const override { return c.snap.colonists>0; }
  void fire(const IncidentCtx& c) override {
    if (c.api->toast) c.api->toast("❄️ Cold snap! Heat your rooms to avoid hypothermia.");
  }
};

// Disease outbreak
struct IncDisease final : Incident {
  IncDisease(){ kind=IncidentKind::Disease; weightBase=0.45f; cooldown=60.f*12; minSpacing=60.f*2; }
  bool canFire(const IncidentCtx& c) const override { return c.snap.colonists>=2; }
  void fire(const IncidentCtx& c) override {
    if (c.api->toast) c.api->toast("🦠 A disease spreads among your colonists.");
  }
};

// Mood break (negative, but cheap): fires when mood is low.
struct IncMoodBreak final : Incident {
  IncMoodBreak(){ kind=IncidentKind::MoodBreak; weightBase=0.35f; cooldown=60.f*3; minSpacing=60.f*1; }
  bool canFire(const IncidentCtx& c) const override { return c.snap.mood < 35; }
  void fire(const IncidentCtx& c) override {
    if (c.api->toast) c.api->toast("💥 A colonist has a mental break!");
  }
};

// --- Director state -------------------------------------------------------------
struct Director {
  StorytellerBindings api{};
  Rng rng{0};
  Pacing pace{};
  float tick = 0.f;              // accum dt
  float checkEvery = 7.5f;       // seconds between scheduling checks
  float time = 0.f;              // lifetime seconds

  // cooldown trackers
  float cdRaid=0, cdDisease=0, cdWeather=0, cdGood=0;

  // incidents registry
  std::vector<IncRaid>     raid{1};
  std::vector<IncTrader>   trader{1};
  std::vector<IncDropPod>  droppod{1};
  std::vector<IncHeatWave> heat{1};
  std::vector<IncColdSnap> cold{1};
  std::vector<IncDisease>  disease{1};
  std::vector<IncMoodBreak> mood{1};

  // schedule queue (simple: fire immediately once picked; extend to delayed)
  struct Fired { IncidentKind kind; float at; };
  std::deque<Fired> recent;

  // Serialize
  void save(std::ostream& out) {
    out << "STORYTELLER v1\n";
    out << "time " << time << "\n";
    out << "budget " << pace.threatBudget << "\n";
    out << "cd " << cdRaid << " " << cdDisease << " " << cdWeather << " " << cdGood << "\n";
    out << "recent " << recent.size() << "\n";
    for (auto& f: recent) out << (int)f.kind << " " << f.at << "\n";
    out << "end\n";
  }
  bool load(std::istream& in) {
    std::string tag; if (!(in>>tag)) return false;
    if (tag!="STORYTELLER") return false;
    std::string v; in>>v; // v1
    in >> tag >> time;
    in >> tag >> pace.threatBudget;
    in >> tag >> cdRaid >> cdDisease >> cdWeather >> cdGood;
    size_t n=0; in >> tag >> n;
    recent.clear(); recent.resize(n);
    for (size_t i=0;i<n;i++){ int k; in>>k>>recent[i].at; recent[i].kind=(IncidentKind)k; }
    in >> tag; // end
    return true;
  }

  ColonySnap snap() {
    ColonySnap s{};
    if (api.getColonistCount) s.colonists = api.getColonistCount();
    if (api.getWealth)        s.wealth    = api.getWealth();
    if (api.getHostileCount)  s.hostiles  = api.getHostileCount();
    if (api.getAverageMood)   s.mood      = api.getAverageMood();
    if (api.getDayIndex)      s.day       = api.getDayIndex();
    return s;
  }

  void gainThreatBudget(const ColonySnap& s, float dt) {
    // Accrue budget per second; scales with colony power.
    float perSec = 0.08f + s.colonists*0.02f + (s.wealth*0.0001f);
    perSec *= (0.85f + (1.0f - s.mood/100.f)*0.3f); // lower mood -> more negative pressure
    pace.threatBudget += perSec * dt;
  }

  void decayCooldowns(float dt) {
    cdRaid = std::max(0.f, cdRaid - dt);
    cdDisease = std::max(0.f, cdDisease - dt);
    cdWeather = std::max(0.f, cdWeather - dt);
    cdGood = std::max(0.f, cdGood - dt);
    pace.sinceIncident += dt;
  }

  void maybeSchedule(float dt) {
    tick += dt; time += dt;
    auto s = snap();
    gainThreatBudget(s, dt);
    decayCooldowns(dt);
    if (tick < checkEvery) return;
    tick = 0;

    // Decide severity target based on day & peace time.
    float sev = 0.35f + std::min(0.65f, s.day*0.015f) + std::min(0.25f, pace.sinceIncident/1200.f);

    // Build a candidate list with dynamic weights & cooldown checks.
    struct Candidate {
      Incident* inc;
      float weight;
      float cost;    // negative means consumes budget
      float* cdSlot; // pointer to matching cooldown for update
    };
    std::vector<Candidate> C;

    auto tryPush = [&](auto& vec, float& cdSlot){
      for (auto& x : vec){
        IncidentCtx c{ s, &rng, &api, sev };
        if (!x.canFire(c)) continue;
        // respect cooldowns & spacing
        if (cdSlot > 0) continue;
        if (pace.sinceIncident < x.minSpacing) continue;
        float cost = x.pointsCost(c);
        float w = x.weightBase;
        // Make some context-sensitive weight nudges
        if (x.kind==IncidentKind::Raid){
          w *= 0.8f + std::min(1.2f, (float)s.colonists*0.12f);
          // If we’re broke on budget, de-emphasize raids
          if (pace.threatBudget < 15) w *= 0.25f;
        }
        if (x.kind==IncidentKind::Trader || x.kind==IncidentKind::DropPod){
          // Good events more likely if mood is low
          w *= (1.0f + std::max(0.f, 0.8f - s.mood/100.f));
        }
        C.push_back(Candidate{ &x, std::max(0.0f, w), cost, &cdSlot });
      }
    };

    tryPush(raid, cdRaid);
    tryPush(disease, cdDisease);
    tryPush(heat, cdWeather);
    tryPush(cold, cdWeather);
    tryPush(mood, cdWeather);   // reusing slot; feel free to separate
    tryPush(trader, cdGood);
    tryPush(droppod, cdGood);

    if (C.empty()) return;

    // Filter out anything we can’t currently afford (if it has negative cost)
    std::vector<Candidate> affordable;
    affordable.reserve(C.size());
    for (auto& c : C) {
      if (c.cost < 0 && (-c.cost) > pace.threatBudget) continue; // too expensive
      affordable.push_back(c);
    }
    if (affordable.empty()) return;

    // Weighted pick & fire
    auto* pick = weightedPick(affordable, [](const Candidate& k){ return k.weight; }, rng);
    if (!pick) return;

    IncidentCtx ctx{ s, &rng, &api, sev };
    pick->inc->fire(ctx);
    // pay budget & set cooldowns
    if (pick->cost < 0) pace.threatBudget = std::max(0.f, pace.threatBudget + pick->cost);
    // simple CD assignment by kind
    *pick->cdSlot = pick->inc->cooldown;
    pace.sinceIncident = 0.f;
    recent.push_back(Fired{ pick->inc->kind, time });
    if (recent.size() > 32) recent.pop_front();
  }
};

// Singleton director (simple for single-player)
Director* G = nullptr;

// Default toast if game didn’t bind one
static void defaultToast(const std::string& s){ std::cout << "[Storyteller] " << s << "\n"; }

} // namespace (anon)

// ---------------- API -----------------------------------------------------------
void Storyteller_Init(const StorytellerBindings& b, uint64_t seed){
  if (!G) G = new Director();
  G->api = b;
  if (!G->api.toast) G->api.toast = defaultToast;
  G->rng = Rng(seed ? seed : 0xC01onyULL);
  G->pace = Pacing{};
  G->tick = 0.f; G->time = 0.f;
  G->cdRaid = G->cdDisease = G->cdWeather = G->cdGood = 0.f;
}

void Storyteller_Update(float dtSeconds){
  if (!G) return;
  // clamp dt to avoid spikes (pause/resume)
  float dt = std::min(dtSeconds, 0.25f);
  G->maybeSchedule(dt);
}

void Storyteller_Save(std::ostream& out){
  if (!G) return;
  G->save(out);
}

bool Storyteller_Load(std::istream& in){
  if (!G) G = new Director();
  return G->load(in);
}

} // namespace cg
