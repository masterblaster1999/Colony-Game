// ──────────────────────────────────────────────────────────────────────────────
// ColonyDirectives.hpp — Mid‑term “Project/Directive” goals for Colony‑Game
// Place at: src/gameplay/ColonyDirectives.hpp
//
// Zero-dependency, header-only manager that:
//  • Defines chainable Directives -> each has Stages -> each has Conditions.
//  • Auto-posts Jobs to your existing job board via a callback.
//  • Tracks progress via world-query callbacks (resources, buildings, days, etc.).
//  • Emits small UI-friendly progress structs (strings + 0..1 progress).
//  • Optional rewards (grant resources, set flags, toast).
//  • Tiny text save/load (no external JSON needed).
//
// Hooks mirror your adapter style (cf. AtmosphereGameplayBridge.hpp) and tick()
// integrates cleanly with your fixed-step loop (see engine::run_loop). 
// ──────────────────────────────────────────────────────────────────────────────

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <limits>

// ---------- Job requests posted to your MultiJobBoard -------------------------
// Keep this generic; adapt inside your postJob() hookup.
struct JobRequest {
  // Examples: "Gather", "Build", "Craft", "Haul", "Research"
  std::string verb;
  // Target semantics are up to your mapping: resource id ("wood"), blueprint id ("bed"), recipe id, etc.
  std::string targetId;
  // Optional free-form payload (ex: "stockpile=Food", "bench=Smelter", "zone=Farm")
  std::string payload;
  int quantity{1};
  int priority{0};         // 0 = normal; higher = more urgent
};

// ---------- World query hooks -------------------------------------------------
struct DirectiveQueries {
  // Return count of a named resource in the colony (carried+stockpiled as you see fit).
  std::function<int(const std::string& resourceId)> resourceCount = nullptr;

  // Return the number of *built & operational* instances of a blueprint/structure.
  std::function<int(const std::string& blueprintId)> buildingCount = nullptr;

  // Current population (colonists available).
  std::function<int()> population = nullptr;

  // Days since game start (fractional allowed). Wire to your time subsystem.
  std::function<double()> daysPassed = nullptr;

  // Optional: any custom scalar by key (e.g., "morale", "breathableRooms")
  std::function<float(const std::string& key)> customScalar = nullptr;
};

// ---------- Outgoing hooks (effects/UI) --------------------------------------
struct DirectiveEffects {
  // Post a job (bridge this to your MultiJobBoard add/post API).
  std::function<void(const JobRequest&)> postJob = nullptr;

  // Reward: grant resources into the colony (optional).
  std::function<void(const std::string& resourceId, int amount)> grantResource = nullptr;

  // Reward/UX: set a global flag/unlock (optional).
  std::function<void(const std::string& flagKey, bool value)> setFlag = nullptr;

  // UX: show a toast or log line.
  std::function<void(const std::string& msg)> toast = nullptr;
};

// ---------- Conditions --------------------------------------------------------
enum class ConditionKind : std::uint8_t {
  ResourceAtLeast,     // need >= targetCount of resourceId
  BuildingAtLeast,     // need >= targetCount of building/blueprintId
  PopulationAtLeast,   // need >= targetCount colonists
  DaysPassedAtLeast,   // need >= targetDays since start
  FlagSet,             // world flag must be true
  CustomScalarAtLeast  // customScalar(key) >= targetValue
};

struct Condition {
  ConditionKind kind{};
  std::string key;     // resourceId / blueprintId / flag / custom key
  double target{0.0};  // count, days, scalar

  // Compute [0..1] progress and current value for HUD.
  std::pair<double,double> currentAndProgress(const DirectiveQueries& q,
                                              const std::unordered_map<std::string,bool>& flags) const {
    auto clamp01 = [](double v){ return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
    double cur = 0.0;
    switch (kind) {
      case ConditionKind::ResourceAtLeast:
        cur = q.resourceCount ? double(q.resourceCount(key)) : 0.0; break;
      case ConditionKind::BuildingAtLeast:
        cur = q.buildingCount ? double(q.buildingCount(key)) : 0.0; break;
      case ConditionKind::PopulationAtLeast:
        cur = q.population ? double(q.population()) : 0.0; break;
      case ConditionKind::DaysPassedAtLeast:
        cur = q.daysPassed ? q.daysPassed() : 0.0; break;
      case ConditionKind::FlagSet: {
        auto it = flags.find(key); cur = (it!=flags.end() && it->second) ? 1.0 : 0.0; 
        double prog = (target <= 0.5) ? cur : clamp01(cur/target);
        return {cur, prog};
      }
      case ConditionKind::CustomScalarAtLeast:
        cur = q.customScalar ? double(q.customScalar(key)) : 0.0; break;
    }
    double prog = (target <= 0.0) ? 1.0 : clamp01(cur / target);
    return {cur, prog};
  }

  bool satisfied(const DirectiveQueries& q,
                 const std::unordered_map<std::string,bool>& flags) const {
    auto cp = currentAndProgress(q, flags);
    if (kind == ConditionKind::FlagSet) return cp.first >= ((target <= 0.5) ? 1.0 : target);
    return cp.first + 1e-9 >= target;
  }
};

// Optional stage-time throttling: only post jobs once when a stage activates.
struct StageJobBundle {
  std::vector<JobRequest> jobs;
};

struct Reward {
  std::unordered_map<std::string,int> grantResources; // id->amount
  std::string setFlagKey;    // if non-empty, set to true on completion
  std::string toastMessage;  // optional banner/log
};

struct DirectiveStage {
  std::string title;
  std::string description;
  std::vector<Condition> conditions;
  StageJobBundle jobsToPost; // posted once on stage activation
  Reward reward;
};

enum class DirectiveState : std::uint8_t { Locked, Active, Completed };

struct Directive {
  std::string id;
  std::string name;
  std::string blurb;
  std::vector<DirectiveStage> stages;

  DirectiveState state{DirectiveState::Locked};
  int stageIndex{0};              // active stage pointer
  bool pinOnHUD{true};
  bool postedJobsForStage{false}; // internal: to avoid reposting every tick

  bool isDone() const { return state == DirectiveState::Completed; }
  bool hasActiveStage() const {
    return state == DirectiveState::Active && stageIndex >=0 && stageIndex < int(stages.size());
  }
};

// ---------- UI-friendly snapshot for HUD -------------------------------------
struct ConditionProgressUI {
  std::string label;     // "Stockpile Food"
  std::string value;     // "34 / 50"
  double progress01{0.0};
};
struct StageProgressUI {
  std::string directiveId;
  std::string directiveName;
  std::string stageTitle;
  std::string stageDesc;
  std::vector<ConditionProgressUI> conditions;
  double overall01{0.0}; // average of conditions
  bool completed{false};
};

// ---------- ColonyDirectives manager -----------------------------------------
class ColonyDirectives {
public:
  DirectiveQueries queries;
  DirectiveEffects effects;

  // World flags available to conditions & rewards (can also be set via effects.setFlag).
  std::unordered_map<std::string,bool> flags;

  // Add built-in starter directives (safe defaults, tweak freely).
  void addDefaultDirectives() {
    // 1) Bootstrap Shelter
    Directive d1;
    d1.id   = "bootstrap_shelter";
    d1.name = "Bootstrap Shelter";
    d1.blurb= "Secure basic sleep & supplies so colonists can stabilize.";

    {
      DirectiveStage s;
      s.title = "Stock Basic Supplies";
      s.description = "Gather wood and food to hold out the first days.";
      s.conditions = {
        {ConditionKind::ResourceAtLeast, "wood",  50},
        {ConditionKind::ResourceAtLeast, "food",  50},
        {ConditionKind::PopulationAtLeast, "",    3}
      };
      s.jobsToPost.jobs = {
        {"Gather","wood","stockpile=Any",50, 1},
        {"Gather","food","stockpile=Food",50, 1}
      };
      s.reward.grantResources = { {"medicine", 5} };
      s.reward.toastMessage   = "Supply stash secured (+5 medicine).";
      d1.stages.push_back(s);
    }
    {
      DirectiveStage s;
      s.title = "Beds & Roof";
      s.description = "Place beds under a roof for uninterrupted sleep.";
      s.conditions = {
        {ConditionKind::BuildingAtLeast, "bed",   3},
        {ConditionKind::BuildingAtLeast, "roof",  1}
      };
      s.jobsToPost.jobs = {
        {"Build","bed","room=Dormitory",3, 2},
        {"Build","roof","area=Shelter",1, 1}
      };
      s.reward.toastMessage   = "Shelter checked! Better rest improves work.";
      s.reward.setFlagKey     = "hasShelter";
      d1.stages.push_back(s);
    }
    d1.state = DirectiveState::Active;
    directives_.push_back(d1);

    // 2) Secure Food
    Directive d2;
    d2.id   = "secure_food";
    d2.name = "Secure Food";
    d2.blurb= "Sustainable food pipeline beats feast‑and‑famine.";

    {
      DirectiveStage s;
      s.title = "Farm Plot";
      s.description = "Till and sow a small farm plot.";
      s.conditions = {
        {ConditionKind::BuildingAtLeast, "farm_plot", 1},
      };
      s.jobsToPost.jobs = {
        {"Build","farm_plot","size=small",1, 1},
        {"Haul","seeds","to=farm_plot",20, 0}
      };
      d2.stages.push_back(s);
    }
    {
      DirectiveStage s;
      s.title = "Pantry Buffer";
      s.description = "Keep non‑perishable food on hand.";
      s.conditions = {
        {ConditionKind::ResourceAtLeast, "food", 120}
      };
      s.jobsToPost.jobs = { {"Gather","food","stockpile=Pantry",120, 0} };
      s.reward.toastMessage = "Food buffer established.";
      s.reward.setFlagKey   = "foodStable";
      d2.stages.push_back(s);
    }
    directives_.push_back(d2);

    // 3) Metalworking
    Directive d3;
    d3.id="metalworking";
    d3.name="Metalworking";
    d3.blurb="Smelt ore into bars and craft tools to unlock faster progress.";
    {
      DirectiveStage s;
      s.title = "Smelter Online";
      s.description = "Build a basic smelter and stock ore.";
      s.conditions = {
        {ConditionKind::BuildingAtLeast, "smelter", 1},
        {ConditionKind::ResourceAtLeast, "ore",     30}
      };
      s.jobsToPost.jobs = {
        {"Build","smelter","area=Workshop",1, 2},
        {"Haul","ore","to=smelter",30, 1}
      };
      d3.stages.push_back(s);
    }
    {
      DirectiveStage s;
      s.title = "Bars in Storage";
      s.description = "Produce metal bars and store them.";
      s.conditions = {
        {ConditionKind::ResourceAtLeast, "metal_bar", 20}
      };
      s.jobsToPost.jobs = {
        {"Craft","metal_bar","at=smelter",20, 1}
      };
      s.reward.toastMessage = "Metalworking ready: tool recipes unlocked!";
      s.reward.setFlagKey   = "metalUnlocked";
      d3.stages.push_back(s);
    }
    directives_.push_back(d3);
  }

  // Unlock a directive programmatically (e.g., from tutorial or biome checks).
  void unlock(const std::string& id) {
    if (auto* d = find(id)) {
      if (d->state == DirectiveState::Locked) {
        d->state = DirectiveState::Active;
        d->stageIndex = 0;
        d->postedJobsForStage = false;
      }
    }
  }

  // Fast tick: check stages, auto-post jobs on activation, apply rewards on completion.
  void tick(double /*dt*/) {
    for (auto& d : directives_) {
      if (d.state != DirectiveState::Active || d.stageIndex < 0 || d.stageIndex >= (int)d.stages.size())
        continue;

      auto& s = d.stages[d.stageIndex];

      // Post stage jobs once on activation.
      if (!d.postedJobsForStage) {
        if (effects.postJob) {
          for (const auto& jr : s.jobsToPost.jobs) effects.postJob(jr);
        }
        d.postedJobsForStage = true;
        if (effects.toast) effects.toast("Directive: " + d.name + " — Stage \"" + s.title + "\" begun");
      }

      // Evaluate completion.
      bool allOK = true;
      for (const auto& c : s.conditions) {
        if (!c.satisfied(queries, flags)) { allOK = false; break; }
      }
      if (!allOK) continue;

      // Stage complete → reward + advance
      if (!s.reward.toastMessage.empty() && effects.toast) effects.toast(s.reward.toastMessage);
      if (!s.reward.setFlagKey.empty()) {
        flags[s.reward.setFlagKey] = true;
        if (effects.setFlag) effects.setFlag(s.reward.setFlagKey, true);
      }
      if (effects.grantResource) {
        for (auto& kv : s.reward.grantResources) effects.grantResource(kv.first, kv.second);
      }

      d.stageIndex++;
      d.postedJobsForStage = false;
      if (d.stageIndex >= (int)d.stages.size()) {
        d.state = DirectiveState::Completed;
        if (effects.toast) effects.toast("Directive complete: " + d.name);
      } else {
        // Stage advanced, will auto-post next stage jobs on next tick.
      }
    }
  }

  // Snapshot for HUD (text + progress floats; no SDL deps).
  std::vector<StageProgressUI> snapshotForHUD() const {
    std::vector<StageProgressUI> out;
    for (const auto& d : directives_) {
      if (!d.pinOnHUD) continue;
      StageProgressUI ui;
      ui.directiveId = d.id;
      ui.directiveName = d.name;

      if (d.state == DirectiveState::Completed) {
        ui.stageTitle = "Completed";
        ui.stageDesc  = d.blurb;
        ui.completed  = true;
        ui.overall01  = 1.0;
        out.push_back(std::move(ui));
        continue;
      }
      if (!d.hasActiveStage()) continue;
      const auto& s = d.stages[d.stageIndex];
      ui.stageTitle = s.title;
      ui.stageDesc  = s.description;

      double sum = 0.0;
      for (const auto& c : s.conditions) {
        auto cp = c.currentAndProgress(queries, flags);
        ConditionProgressUI item;
        item.label = labelFor(c);
        item.value = valueFor(c, cp.first, c.target);
        item.progress01 = cp.second;
        sum += cp.second;
        ui.conditions.push_back(std::move(item));
      }
      ui.overall01 = (!ui.conditions.empty()) ? (sum / double(ui.conditions.size())) : 1.0;
      out.push_back(std::move(ui));
    }
    return out;
  }

  // Save minimal persistent state: "id:state:stageIndex|..."
  std::string serialize() const {
    std::ostringstream ss;
    bool first = true;
    for (const auto& d : directives_) {
      if (!first) ss << '|';
      first = false;
      ss << d.id << ':' << int(d.state) << ':' << d.stageIndex;
    }
    return ss.str();
  }

  // Load minimal state (unknown IDs are ignored).
  void deserialize(const std::string& s) {
    auto pos = 0u;
    while (pos < s.size()) {
      auto pipe = s.find('|', pos);
      auto part = s.substr(pos, (pipe==std::string::npos ? s.size() : pipe) - pos);
      auto c1 = part.find(':');
      auto c2 = part.find(':', c1==std::string::npos ? part.size() : c1+1);
      if (c1!=std::string::npos && c2!=std::string::npos) {
        auto id = part.substr(0, c1);
        int st   = std::stoi(part.substr(c1+1, c2-(c1+1)));
        int idx  = std::stoi(part.substr(c2+1));
        if (auto* d = find(id)) {
          d->state = (st < 0 || st > 2) ? DirectiveState::Locked : DirectiveState(st);
          d->stageIndex = idx;
          d->postedJobsForStage = false;
        }
      }
      if (pipe==std::string::npos) break;
      pos = pipe + 1;
    }
  }

  // Add your own data-driven directives from code.
  void addDirective(Directive d) {
    // If it comes in unlocked, ensure indices are sane.
    if (d.state == DirectiveState::Active && (d.stageIndex < 0 || d.stageIndex >= (int)d.stages.size())) {
      d.stageIndex = 0;
    }
    directives_.push_back(std::move(d));
  }

  // Get a pointer to a directive by id (or nullptr).
  Directive* find(const std::string& id) {
    for (auto& d : directives_) if (d.id == id) return &d;
    return nullptr;
  }
  const Directive* find(const std::string& id) const {
    for (auto& d : directives_) if (d.id == id) return &d;
    return nullptr;
  }

private:
  std::vector<Directive> directives_;

  static std::string labelFor(const Condition& c) {
    switch (c.kind) {
      case ConditionKind::ResourceAtLeast:    return "Stockpile " + c.key;
      case ConditionKind::BuildingAtLeast:    return "Build " + c.key;
      case ConditionKind::PopulationAtLeast:  return "Colonists";
      case ConditionKind::DaysPassedAtLeast:  return "Days Survived";
      case ConditionKind::FlagSet:            return "Flag: " + c.key;
      case ConditionKind::CustomScalarAtLeast:return c.key;
    }
    return "";
  }
  static std::string valueFor(const Condition& c, double cur, double target) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision((c.kind==ConditionKind::DaysPassedAtLeast || c.kind==ConditionKind::CustomScalarAtLeast) ? 1 : 0);
    if (c.kind == ConditionKind::FlagSet) {
      return (cur >= ((target <= 0.5) ? 1.0 : target)) ? "✓" : " ";
    }
    ss << cur << " / " << target;
    return ss.str();
  }
};
