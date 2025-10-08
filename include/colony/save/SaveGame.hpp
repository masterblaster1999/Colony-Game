#pragma once
// include/colony/save/SaveGame.hpp
//
// Versioned, forward-compatible save-game model + I/O.
// Uses nlohmann::json for (de)serialization; optional runtime schema validation.

#include <nlohmann/json.hpp>               // vcpkg: nlohmann-json
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <expected>                         // C++23
#include <cstdint>

namespace colony::save {

using json = nlohmann::json;

struct SaveError {
    enum class Code {
        IoOpenFail,
        IoWriteFail,
        JsonParseError,
        JsonTypeError,
        JsonSchemaInvalid,
        MigrationFailed
    } code{};
    std::string message;
};

struct Vec3f {
    float x{0}, y{0}, z{0};
};

struct ItemStack {
    std::string item_id;
    int32_t     count{0};

    // Unknown, future fields preserved and round-tripped.
    json extras = json::object();
};

struct Colonist {
    std::string id;
    std::string name;
    Vec3f       pos{};
    float       health{100.0f};
    std::string job;

    std::vector<ItemStack> inventory;

    json extras = json::object();
};

struct Building {
    std::string id;
    std::string type;
    Vec3f       pos{};
    float       hp{100.0f};

    json extras = json::object();
};

struct GameMeta {
    uint64_t tick{0};
    bool     paused{false};
    float    time_scale{1.0f};
    json     extras = json::object();
};

struct WorldState {
    int64_t seed{0};
    std::string rng_state;  // opaque
    json extras = json::object();
};

struct PlayerState {
    Vec3f camera_pos{};
    float camera_yaw_deg{0};
    float camera_pitch_deg{0};
    json extras = json::object();
};

struct SaveGame {
    // Bump this when schema changes (and write a migration step).
    int32_t schema_version{1};
    std::string engine_version; // semver if you want

    std::string created_utc;
    std::string last_saved_utc;

    GameMeta game;
    WorldState world;
    PlayerState player;

    std::unordered_map<std::string, double> resources; // id -> quantity
    std::vector<std::string> research_unlocked;
    std::vector<Colonist> colonists;
    std::vector<Building> buildings;

    // Unknown top-level fields preserved and round-tripped.
    json extras = json::object();
};

// ---------- JSON (de)serialization ----------
void to_json(json& j, const Vec3f& v);
void from_json(const json& j, Vec3f& v);

void to_json(json& j, const ItemStack& v);
void from_json(const json& j, ItemStack& v);

void to_json(json& j, const Colonist& v);
void from_json(const json& j, Colonist& v);

void to_json(json& j, const Building& v);
void from_json(const json& j, Building& v);

void to_json(json& j, const GameMeta& v);
void from_json(const json& j, GameMeta& v);

void to_json(json& j, const WorldState& v);
void from_json(const json& j, WorldState& v);

void to_json(json& j, const PlayerState& v);
void from_json(const json& j, PlayerState& v);

void to_json(json& j, const SaveGame& v);
void from_json(const json& j, SaveGame& v);

// ---------- I/O API ----------
std::expected<SaveGame, SaveError>
LoadSaveGame(const std::filesystem::path& file,
             const std::filesystem::path& schemaPath = {});

std::expected<void, SaveError>
SaveSaveGame(const SaveGame& save, const std::filesystem::path& file);

// Migration hook: updates raw JSON in-place from older versions to current.
bool MigrateJsonInPlace(nlohmann::json& j, int target_schema_version, std::string& outError);

} // namespace colony::save
