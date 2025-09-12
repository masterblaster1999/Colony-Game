#pragma once
#ifndef COLONY_CONTRACT_BOARD_HPP_INCLUDED
#define COLONY_CONTRACT_BOARD_HPP_INCLUDED

/*
    ContractBoard.hpp  (single-file, header-only)

    Drop-in "Contracts/Missions" system to strengthen gameplay loops by
    providing rotating, time-limited goals with rewards.

    ──────────────────────────────────────────────────────────────────────────
    HOW TO INTEGRATE (typical minimal wiring)
    ──────────────────────────────────────────────────────────────────────────
    1) Include in a central game module (e.g., Game.cpp or Simulation.cpp)
         #include "gameplay/ContractBoard.hpp"

    2) Create the board and keep it somewhere globally reachable (game state).
         colony::ContractBoard board;
         board.SetMaxActive(3);
         board.SetRngSeed(0xC01ony42); // optional, for reproducibility

    3) Call into the board as your sim runs:
       - Each frame or tick:
           board.Update(ticksElapsed);
           board.TickAndAutoGenerate(snapshot); // snapshot = current colony state (see struct below)
       - When resources change:
           board.OnResourceDelta("wood", +15);
       - When a building is constructed:
           board.OnBuildingConstructed("sawmill");
       - When population changes:
           board.OnPopulationChanged(newPopulation);
       - When your day/cycle ends (optional):
           board.OnCycleCompleted();

    4) When a contract is completed, call Claim() to grant rewards:
           board.Claim(contractId, [&](const colony::Reward& r){
               // Apply rewards in your economy/inventory systems.
               for (auto& kv : r.resourceBundle) giveResource(kv.first, kv.second);
               reputation += r.reputation;
           });

    5) (Optional) Persist contracts between sessions:
           board.Save("saves/contracts.cb");    // before shutdown
           board.Load("saves/contracts.cb");    // on startup

    Notes:
      - The system is engine-agnostic. No STL exceptions are required.
      - Time is expressed in "ticks" (your sim’s update unit). Use any cadence.
      - All identifiers (resources/buildings) are plain strings to decouple
        from your internal enums. Map them however you like.

    Build: C++17 or newer. Pure STL; no external deps.

    Author: You (feel free to modify). License: match the repo’s license.
*/

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <utility>
#include <random>
#include <algorithm>
#include <functional>
#include <limits>

namespace colony {

// ─────────────────────────────────────────────────────────────────────────────
// Basic data types
// ─────────────────────────────────────────────────────────────────────────────

enum class TaskType : std::int32_t {
    ProduceResource = 0,  // Produce/collect amount of a resource (subjectKey = resource name)
    BuildCount      = 1,  // Construct N of a building type (subjectKey = building name)
    ReachPopulation = 2,  // Reach total population (subjectKey ignored)
    SurviveCycles   = 3   // Survive N cycles/days (subjectKey ignored)
};

struct Reward {
    // Generic bundle: resource name -> amount (can be negative for a "cost" style)
    std::vector<std::pair<std::string, std::int64_t>> resourceBundle;
    // Optional meta reward (e.g., reputation / influence points)
    std::int64_t reputation = 0;
};

// Snapshot of current game state the board can see for auto-generation.
// Provide what you have; leave maps empty if not applicable.
struct ColonySnapshot {
    std::int64_t population = 0;
    std::unordered_map<std::string, std::int64_t> resourceCounts;  // e.g., {"wood": 120, "stone": 50}
    std::unordered_map<std::string, std::int64_t> buildingCounts;  // e.g., {"sawmill": 1, "farm": 2}
    std::int64_t cycleIndex = 0;                                   // e.g., day count
};

struct Contract {
    // Identity
    std::int32_t id = -1;

    // Presentation
    std::string title;
    std::string description;

    // Logic
    TaskType type = TaskType::ProduceResource;
    std::string subjectKey;               // resource/building type for applicable tasks
    std::int64_t target = 0;              // target amount to reach
    std::int64_t progress = 0;            // current progress
    std::int32_t expiryTicks = 0;         // time remaining until failure (<= 0 => failed if not completed)
    bool completed = false;
    bool failed = false;

    // Reward
    Reward reward;

    // Helper: how much remains
    std::int64_t Remaining() const {
        if (completed) return 0;
        return target > progress ? (target - progress) : 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ContractBoard
// ─────────────────────────────────────────────────────────────────────────────

class ContractBoard {
public:
    // Public configuration knobs
    void SetMaxActive(std::size_t n)            { maxActive_ = (n == 0 ? 1 : n); }
    void SetGenerationCooldown(std::int32_t t)  { genCooldownTicks_ = std::max<std::int32_t>(t, 0); }
    void SetDefaultExpiry(std::int32_t t)       { defaultExpiryTicks_ = std::max<std::int32_t>(t, 1); }
    void SetRngSeed(std::uint32_t seed)         { rng_.seed(seed); }

    std::size_t MaxActive() const               { return maxActive_; }
    std::int32_t DefaultExpiry() const          { return defaultExpiryTicks_; }

    // Lifecycle
    ContractBoard()
    : rng_(std::random_device{}())
    {
        SetMaxActive(3);
        SetGenerationCooldown(0);
        SetDefaultExpiry(60 * 60); // e.g., 1 real-time hour if 1 tick = 1 second. Adjust as needed.
    }

    // Advance time and handle expiry
    void Update(std::int32_t ticksElapsed) {
        if (ticksElapsed <= 0) return;

        // Update generation timer
        if (pendingGenCooldown_ > 0) {
            pendingGenCooldown_ = std::max<std::int32_t>(0, pendingGenCooldown_ - ticksElapsed);
        }

        // Tick contracts
        for (auto& c : active_) {
            if (!c.completed && !c.failed) {
                c.expiryTicks -= ticksElapsed;
                if (c.expiryTicks <= 0) {
                    c.failed = true;
                    c.expiryTicks = 0;
                }
            }
        }

        // Garbage collect obviously stale ones if you like (optional).
        // Keep failed ones visible until replaced, to let UI show "failed" state.
    }

    // Auto-generate new contracts when below capacity, respecting a cooldown.
    // Call this *after* Update() each tick with current snapshot.
    void TickAndAutoGenerate(const ColonySnapshot& snap) {
        // Refill up to MaxActive()
        while (active_.size() < maxActive_) {
            if (pendingGenCooldown_ > 0) break;
            Contract c;
            if (!GenerateContract(snap, c)) break; // couldn't generate (e.g., no data)
            c.id = NextId_();
            c.expiryTicks = defaultExpiryTicks_;
            active_.push_back(std::move(c));
            // set cooldown between generations to avoid bursty churn
            pendingGenCooldown_ = std::max<std::int32_t>(genCooldownTicks_, 60);
        }
    }

    // Event hooks: wire these into your sim
    void OnResourceDelta(const std::string& resourceId, std::int64_t delta) {
        if (delta <= 0) return; // Only count gains by default; tweak if needed
        for (auto& c : active_) {
            if (c.failed || c.completed) continue;
            if (c.type == TaskType::ProduceResource && c.subjectKey == resourceId) {
                // Guard against overflow
                if (c.progress > (std::numeric_limits<std::int64_t>::max)() - delta) {
                    c.progress = (std::numeric_limits<std::int64_t>::max)();
                } else {
                    c.progress += delta;
                }
                CheckComplete_(c);
            }
        }
    }

    void OnBuildingConstructed(const std::string& buildingId, std::int64_t countDelta = 1) {
        if (countDelta <= 0) return;
        for (auto& c : active_) {
            if (c.failed || c.completed) continue;
            if (c.type == TaskType::BuildCount && c.subjectKey == buildingId) {
                c.progress = std::min<std::int64_t>(c.target, c.progress + countDelta);
                CheckComplete_(c);
            }
        }
    }

    void OnPopulationChanged(std::int64_t newPopulation) {
        for (auto& c : active_) {
            if (c.failed || c.completed) continue;
            if (c.type == TaskType::ReachPopulation) {
                c.progress = std::max<std::int64_t>(c.progress, newPopulation);
                CheckComplete_(c);
            }
        }
    }

    void OnCycleCompleted(std::int64_t cyclesDelta = 1) {
        if (cyclesDelta <= 0) return;
        for (auto& c : active_) {
            if (c.failed || c.completed) continue;
            if (c.type == TaskType::SurviveCycles) {
                c.progress = std::min<std::int64_t>(c.target, c.progress + cyclesDelta);
                CheckComplete_(c);
            }
        }
    }

    // Claim reward for a completed contract. Returns true if reward granted and contract removed.
    // grantFn: callback to apply rewards into your game's inventory/economy.
    bool Claim(std::int32_t contractId, const std::function<void(const Reward&)>& grantFn) {
        for (std::size_t i = 0; i < active_.size(); ++i) {
            auto& c = active_[i];
            if (c.id != contractId) continue;
            if (!c.completed || c.failed) return false;
            if (grantFn) grantFn(c.reward);
            // Remove the contract and free a slot
            active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
        return false;
    }

    // Accessors
    const std::vector<Contract>& Active() const { return active_; }

    // Remove all failed contracts immediately (optional utility)
    void CullFailed() {
        active_.erase(std::remove_if(active_.begin(), active_.end(),
            [](const Contract& c){ return c.failed; }), active_.end());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Persistence (simple binary format; robust to strings of any content)
    // ─────────────────────────────────────────────────────────────────────────

    bool Save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::out);
        if (!out) return false;
        Write_u32_(out, Magic_);
        Write_u32_(out, Version_);
        Write_u32_(out, static_cast<std::uint32_t>(active_.size()));
        for (const auto& c : active_) {
            Write_i32_(out, c.id);
            Write_i32_(out, static_cast<std::int32_t>(c.type));
            Write_str_(out, c.title);
            Write_str_(out, c.description);
            Write_str_(out, c.subjectKey);
            Write_i64_(out, c.target);
            Write_i64_(out, c.progress);
            Write_i32_(out, c.expiryTicks);
            Write_u8_(out, c.completed ? 1 : 0);
            Write_u8_(out, c.failed ? 1 : 0);
            // Reward
            Write_u32_(out, static_cast<std::uint32_t>(c.reward.resourceBundle.size()));
            for (auto& kv : c.reward.resourceBundle) {
                Write_str_(out, kv.first);
                Write_i64_(out, kv.second);
            }
            Write_i64_(out, c.reward.reputation);
        }
        return static_cast<bool>(out);
    }

    bool Load(const std::string& path) {
        std::ifstream in(path, std::ios::binary | std::ios::in);
        if (!in) return false;
        std::uint32_t magic = 0, version = 0;
        if (!Read_u32_(in, magic) || magic != Magic_) return false;
        if (!Read_u32_(in, version) || version != Version_) return false;

        std::uint32_t count = 0;
        if (!Read_u32_(in, count)) return false;
        std::vector<Contract> tmp;
        tmp.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Contract c;
            std::int32_t typeInt = 0;
            if (!Read_i32_(in, c.id)) return false;
            if (!Read_i32_(in, typeInt)) return false;
            c.type = static_cast<TaskType>(typeInt);
            if (!Read_str_(in, c.title)) return false;
            if (!Read_str_(in, c.description)) return false;
            if (!Read_str_(in, c.subjectKey)) return false;
            if (!Read_i64_(in, c.target)) return false;
            if (!Read_i64_(in, c.progress)) return false;
            if (!Read_i32_(in, c.expiryTicks)) return false;

            std::uint8_t b = 0;
            if (!Read_u8_(in, b)) return false; c.completed = (b != 0);
            if (!Read_u8_(in, b)) return false; c.failed    = (b != 0);

            std::uint32_t rcount = 0;
            if (!Read_u32_(in, rcount)) return false;
            c.reward.resourceBundle.clear();
            c.reward.resourceBundle.reserve(rcount);
            for (std::uint32_t r = 0; r < rcount; ++r) {
                std::string key; std::int64_t amt = 0;
                if (!Read_str_(in, key)) return false;
                if (!Read_i64_(in, amt)) return false;
                c.reward.resourceBundle.emplace_back(std::move(key), amt);
            }
            if (!Read_i64_(in, c.reward.reputation)) return false;

            tmp.push_back(std::move(c));
        }
        active_ = std::move(tmp);
        // After load, allow immediate generation again
        pendingGenCooldown_ = 0;
        return true;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Generation logic
    // ─────────────────────────────────────────────────────────────────────────

    bool GenerateContract(const ColonySnapshot& snap, Contract& out) {
        // If we have no snapshot signal at all, create a very generic starter
        const bool hasAnyRes = !snap.resourceCounts.empty();
        const bool hasAnyBld = !snap.buildingCounts.empty();
        const bool earlyGame = (snap.population < 12) && (TotalCount_(snap.buildingCounts) < 4);

        std::uniform_int_distribution<int> pick(0, 99);

        // Candidate buckets: Short / Medium / Long
        const int roll = pick(rng_);
        if (roll < 50) {
            // SHORT: simple & quick
            if (hasAnyRes) {
                auto key = WeightedPickKey_(snap.resourceCounts);
                auto base = std::max<std::int64_t>(5, snap.resourceCounts.at(key) / 5); // ~20% of current stock
                BuildProduceContract_("Top up " + key,
                                      "Accumulate more " + key + " to stabilize early supply.",
                                      key, ClampTarget_(base, 5, 250),
                                      MakeReward_({{key, base}}, 1),
                                      out);
                return true;
            } else if (hasAnyBld) {
                auto key = WeightedPickKey_(snap.buildingCounts);
                BuildConstructContract_("Expand " + key,
                                        "Construct one additional " + key + " to boost throughput.",
                                        key, 1,
                                        MakeReward_({{"tools", 5}}, 1),
                                        out);
                return true;
            } else {
                BuildPopulationContract_("Recruit settlers",
                                         "Reach a population of 5 to unlock momentum.",
                                         5, MakeReward_({{"food", 20}}, 1), out);
                return true;
            }
        } else if (roll < 85) {
            // MEDIUM: stretch goals
            if (hasAnyRes) {
                auto key = WeightedPickKey_(snap.resourceCounts);
                auto base = std::max<std::int64_t>(15, snap.resourceCounts.at(key) / 3); // ~33% of stock
                BuildProduceContract_("Secure " + key + " supply",
                                      "Stockpile " + std::to_string(base) + " " + key + " to weather shortfalls.",
                                      key, ClampTarget_(base, 20, 800),
                                      MakeReward_({{key, base/3}, {"coin", base/5}}, 2),
                                      out);
                return true;
            } else if (hasAnyBld) {
                auto key = WeightedPickKey_(snap.buildingCounts);
                BuildConstructContract_("Scale infrastructure: " + key,
                                        "Construct 2 " + key + " to multiply production.",
                                        key, 2, MakeReward_({{"coin", 30}}, 2), out);
                return true;
            } else {
                BuildPopulationContract_("Grow the colony",
                                         "Reach population 10 for specialization options.",
                                         10, MakeReward_({{"coin", 50}}, 2), out);
                return true;
            }
        } else {
            // LONG: timed narrative-ish objectives
            if (earlyGame) {
                BuildSurviveContract_("Hold out", "Survive 3 cycles to prove viability.",
                                      3, MakeReward_({{"coin", 75}, {"food", 40}}, 3), out);
                return true;
            } else {
                BuildPopulationContract_("Town charter",
                                         "Reach population " + std::to_string(std::max<std::int64_t>(15, snap.population + 5)) + " to qualify for a charter.",
                                         std::max<std::int64_t>(15, snap.population + 5),
                                         MakeReward_({{"coin", 100}}, 4), out);
                return true;
            }
        }
    }

    void BuildProduceContract_(const std::string& title,
                               const std::string& desc,
                               const std::string& resourceKey,
                               std::int64_t target,
                               Reward reward,
                               Contract& out)
    {
        out = {};
        out.title = title;
        out.description = desc;
        out.type = TaskType::ProduceResource;
        out.subjectKey = resourceKey;
        out.target = std::max<std::int64_t>(1, target);
        out.reward = std::move(reward);
    }

    void BuildConstructContract_(const std::string& title,
                                 const std::string& desc,
                                 const std::string& buildingKey,
                                 std::int64_t count,
                                 Reward reward,
                                 Contract& out)
    {
        out = {};
        out.title = title;
        out.description = desc;
        out.type = TaskType::BuildCount;
        out.subjectKey = buildingKey;
        out.target = std::max<std::int64_t>(1, count);
        out.reward = std::move(reward);
    }

    void BuildPopulationContract_(const std::string& title,
                                  const std::string& desc,
                                  std::int64_t popTarget,
                                  Reward reward,
                                  Contract& out)
    {
        out = {};
        out.title = title;
        out.description = desc;
        out.type = TaskType::ReachPopulation;
        out.target = std::max<std::int64_t>(1, popTarget);
        out.reward = std::move(reward);
    }

    void BuildSurviveContract_(const std::string& title,
                               const std::string& desc,
                               std::int64_t cycles,
                               Reward reward,
                               Contract& out)
    {
        out = {};
        out.title = title;
        out.description = desc;
        out.type = TaskType::SurviveCycles;
        out.target = std::max<std::int64_t>(1, cycles);
        out.reward = std::move(reward);
    }

    // Completion helper
    static void CheckComplete_(Contract& c) {
        if (!c.completed && !c.failed && c.progress >= c.target) {
            c.completed = true;
        }
    }

    // Reward helpers
    static Reward MakeReward_(std::initializer_list<std::pair<const char*, std::int64_t>> items,
                              std::int64_t reputation)
    {
        Reward r;
        r.reputation = reputation;
        r.resourceBundle.reserve(items.size());
        for (auto& it : items) r.resourceBundle.emplace_back(std::string(it.first), it.second);
        return r;
    }

    // Weighted pick by value (heavier items more likely)
    template <class Map>
    std::string WeightedPickKey_(const Map& m) {
        std::uint64_t total = 0;
        for (auto& kv : m) {
            // clamp each weight to avoid overflow and ensure non-zero
            std::uint64_t w = static_cast<std::uint64_t>(std::max<std::int64_t>(1, kv.second));
            if (w > (std::numeric_limits<std::uint64_t>::max)() - total) w = 1;
            total += w;
        }
        if (total == 0) return m.begin()->first;

        std::uniform_int_distribution<std::uint64_t> dist(0, total - 1);
        std::uint64_t roll = dist(rng_);
        for (auto& kv : m) {
            std::uint64_t w = static_cast<std::uint64_t>(std::max<std::int64_t>(1, kv.second));
            if (roll < w) return kv.first;
            roll -= w;
        }
        return m.begin()->first; // fallback
    }

    template <class Map>
    static std::int64_t TotalCount_(const Map& m) {
        std::int64_t s = 0;
        for (auto& kv : m) {
            if (kv.second > (std::numeric_limits<std::int64_t>::max)() - s) return (std::numeric_limits<std::int64_t>::max)();
            s += kv.second;
        }
        return s;
    }

    static std::int64_t ClampTarget_(std::int64_t v, std::int64_t mn, std::int64_t mx) {
        return std::max<std::int64_t>(mn, std::min<std::int64_t>(mx, v));
    }

    std::int32_t NextId_() {
        if (nextId_ == (std::numeric_limits<std::int32_t>::max)()) nextId_ = 1;
        return nextId_++;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Binary IO helpers (little-endian write/read of POD + length-prefixed strings)
    // ─────────────────────────────────────────────────────────────────────────

    static void Write_u32_(std::ofstream& out, std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    static void Write_i32_(std::ofstream& out, std::int32_t v)  { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    static void Write_u8_(std::ofstream& out, std::uint8_t v)   { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    static void Write_i64_(std::ofstream& out, std::int64_t v)  { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); }

    static void Write_str_(std::ofstream& out, const std::string& s) {
        std::uint32_t n = static_cast<std::uint32_t>(s.size());
        Write_u32_(out, n);
        if (n) out.write(s.data(), static_cast<std::streamsize>(n));
    }

    static bool Read_u32_(std::ifstream& in, std::uint32_t& v) { in.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<bool>(in); }
    static bool Read_i32_(std::ifstream& in, std::int32_t& v)  { in.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<bool>(in); }
    static bool Read_u8_(std::ifstream& in, std::uint8_t& v)   { in.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<bool>(in); }
    static bool Read_i64_(std::ifstream& in, std::int64_t& v)  { in.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<bool>(in); }

    static bool Read_str_(std::ifstream& in, std::string& s) {
        std::uint32_t n = 0;
        if (!Read_u32_(in, n)) return false;
        s.resize(n);
        if (n) in.read(&s[0], static_cast<std::streamsize>(n));
        return static_cast<bool>(in);
    }

private:
    // State
    std::vector<Contract> active_;
    std::size_t maxActive_            = 3;
    std::int32_t defaultExpiryTicks_  = 3600;
    std::int32_t genCooldownTicks_    = 120;  // baseline gap between auto-generations
    std::int32_t pendingGenCooldown_  = 0;

    std::mt19937 rng_;
    std::int32_t nextId_ = 1;

    // Persistence identifiers
    static constexpr std::uint32_t Magic_   = 0xCB0A4D21u; // "CB" + random bits
    static constexpr std::uint32_t Version_ = 1u;
};

} // namespace colony

// ─────────────────────────────────────────────────────────────────────────────
// Optional standalone micro-demo (compile this header as a TU with the macro)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef CONTRACT_BOARD_DEMO
#include <iostream>
int main() {
    colony::ContractBoard board;
    board.SetMaxActive(3);
    board.SetRngSeed(12345);

    colony::ColonySnapshot snap;
    snap.population = 7;
    snap.resourceCounts = { {"wood", 60}, {"stone", 25}, {"food", 40} };
    snap.buildingCounts = { {"sawmill", 1}, {"farm", 1} };
    snap.cycleIndex = 3;

    // Simulate a few ticks
    for (int t = 0; t < 5; ++t) {
        board.Update(60);
        board.TickAndAutoGenerate(snap);
    }

    for (auto& c : board.Active()) {
        std::cout << "Contract " << c.id << " | " << c.title << " [" << (int)c.type << "]"
                  << " target=" << c.target << " progress=" << c.progress
                  << " expires in " << c.expiryTicks << " ticks\n";
    }

    // Pretend we produced 50 wood
    board.OnResourceDelta("wood", 50);

    // Claim any completed
    for (auto& c : board.Active()) {
        if (c.completed) {
            board.Claim(c.id, [&](const colony::Reward& r){
                std::cout << "Claimed reward: +" << r.reputation << " rep and items:";
                for (auto& kv : r.resourceBundle) std::cout << " {" << kv.first << ":" << kv.second << "}";
                std::cout << "\n";
            });
        }
    }

    // Save/load roundtrip
    board.Save("contracts.cb");
    colony::ContractBoard loaded;
    loaded.Load("contracts.cb");
    std::cout << "Loaded " << loaded.Active().size() << " active contracts.\n";
    return 0;
}
#endif

#endif // COLONY_CONTRACT_BOARD_HPP_INCLUDED
