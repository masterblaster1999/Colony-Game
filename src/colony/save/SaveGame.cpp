// src/colony/save/SaveGame.cpp
#include "colony/save/SaveGame.hpp"

#include <fstream>
#include <sstream>
#include <unordered_set>
#include <chrono>
#include <iomanip>

#if defined(COLONY_USE_JSON_SCHEMA_VALIDATION)
  #include <json-schema.hpp> // pboettch/json-schema-validator
#endif

namespace colony::save {
using json = nlohmann::json;

// ---------- helpers ----------

static json collect_extras(const json& obj,
                           std::initializer_list<const char*> known)
{
    json extras = json::object();
    if (!obj.is_object()) return extras;

    std::unordered_set<std::string> known_set;
    known_set.reserve(known.size());
    for (auto* k : known) known_set.emplace(k);

    for (const auto& [k, v] : obj.items()) {
        if (!known_set.count(k)) extras[k] = v;
    }
    return extras;
}

static void merge_extras(json& dst, const json& extras)
{
    if (!dst.is_object() || !extras.is_object()) return;
    for (const auto& [k, v] : extras.items()) {
        // Do not overwrite known fields; only add missing ones.
        if (!dst.contains(k)) { dst[k] = v; }  // contains() is documented API. :contentReference[oaicite:5]{index=5}
    }
}

// ---------- Vec3f ----------
void to_json(json& j, const Vec3f& v) {
    j = json::array({ v.x, v.y, v.z });
}

void from_json(const json& j, Vec3f& v) {
    if (j.is_array() && j.size() == 3) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
        v.z = j.at(2).get<float>();
    } else if (j.is_object()) {
        v.x = j.value("x", 0.0f); // value(key, default) is the tolerant accessor. :contentReference[oaicite:6]{index=6}
        v.y = j.value("y", 0.0f);
        v.z = j.value("z", 0.0f);
    } else {
        throw nlohmann::json::type_error::create(302, "Vec3f expects array[3] or object");
    }
}

// ---------- ItemStack ----------
void to_json(json& j, const ItemStack& v) {
    j = json::object({
        {"item_id", v.item_id},
        {"count",   v.count}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, ItemStack& v) {
    v.item_id = j.value("item_id", std::string{});
    v.count   = j.value("count", 0);
    v.extras  = collect_extras(j, {"item_id", "count"});
}

// ---------- Colonist ----------
void to_json(json& j, const Colonist& v) {
    j = json::object({
        {"id",   v.id},
        {"name", v.name},
        {"pos",  v.pos},
        {"health", v.health},
        {"job", v.job},
        {"inventory", v.inventory}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, Colonist& v) {
    v.id     = j.value("id", std::string{});
    v.name   = j.value("name", std::string{});
    v.pos    = j.value("pos", Vec3f{});
    v.health = j.value("health", 100.0f);
    v.job    = j.value("job", std::string{});
    v.inventory = j.value("inventory", std::vector<ItemStack>{});
    v.extras = collect_extras(j, {"id","name","pos","health","job","inventory"});
}

// ---------- Building ----------
void to_json(json& j, const Building& v) {
    j = json::object({
        {"id",   v.id},
        {"type", v.type},
        {"pos",  v.pos},
        {"hp",   v.hp}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, Building& v) {
    v.id   = j.value("id", std::string{});
    v.type = j.value("type", std::string{});
    v.pos  = j.value("pos", Vec3f{});
    v.hp   = j.value("hp", 100.0f);
    v.extras = collect_extras(j, {"id","type","pos","hp"});
}

// ---------- GameMeta ----------
void to_json(json& j, const GameMeta& v) {
    j = json::object({
        {"tick",       v.tick},
        {"paused",     v.paused},
        {"time_scale", v.time_scale}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, GameMeta& v) {
    v.tick       = j.value("tick", uint64_t{0});
    v.paused     = j.value("paused", false);
    v.time_scale = j.value("time_scale", 1.0f);
    v.extras     = collect_extras(j, {"tick","paused","time_scale"});
}

// ---------- WorldState ----------
void to_json(json& j, const WorldState& v) {
    j = json::object({
        {"seed",      v.seed},
        {"rng_state", v.rng_state}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, WorldState& v) {
    v.seed      = j.value("seed", int64_t{0});
    v.rng_state = j.value("rng_state", std::string{});
    v.extras    = collect_extras(j, {"seed","rng_state"});
}

// ---------- PlayerState ----------
void to_json(json& j, const PlayerState& v) {
    j = json::object({
        {"camera_pos", v.camera_pos},
        {"camera_yaw_deg", v.camera_yaw_deg},
        {"camera_pitch_deg", v.camera_pitch_deg}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, PlayerState& v) {
    v.camera_pos       = j.value("camera_pos", Vec3f{});
    v.camera_yaw_deg   = j.value("camera_yaw_deg", 0.0f);
    v.camera_pitch_deg = j.value("camera_pitch_deg", 0.0f);
    v.extras = collect_extras(j, {"camera_pos","camera_yaw_deg","camera_pitch_deg"});
}

// ---------- SaveGame ----------
void to_json(json& j, const SaveGame& v) {
    j = json::object({
        {"schema_version", v.schema_version},
        {"engine_version", v.engine_version},
        {"created_utc",    v.created_utc},
        {"last_saved_utc", v.last_saved_utc},
        {"game",     v.game},
        {"world",    v.world},
        {"player",   v.player},
        {"resources", v.resources},
        {"research", json::object({{"unlocked", v.research_unlocked}})},
        {"colonists", v.colonists},
        {"buildings", v.buildings}
    });
    merge_extras(j, v.extras);
}
void from_json(const json& j, SaveGame& v) {
    v.schema_version = j.value("schema_version", 1);
    v.engine_version = j.value("engine_version", std::string{});
    v.created_utc    = j.value("created_utc", std::string{});
    v.last_saved_utc = j.value("last_saved_utc", std::string{});

    v.game   = j.value("game",   GameMeta{});
    v.world  = j.value("world",  WorldState{});
    v.player = j.value("player", PlayerState{});

    // resources: object<string,double>
    v.resources.clear();
    if (j.contains("resources") && j["resources"].is_object()) {
        for (const auto& [key, val] : j["resources"].items()) {
            v.resources[key] = val.get<double>();
        }
    }

    // research.unlocked
    if (j.contains("research") && j["research"].is_object()) {
        v.research_unlocked = j["research"].value("unlocked", std::vector<std::string>{});
    }

    v.colonists = j.value("colonists", std::vector<Colonist>{});
    v.buildings = j.value("buildings", std::vector<Building>{});

    v.extras = collect_extras(j, {
        "schema_version","engine_version","created_utc","last_saved_utc",
        "game","world","player","resources","research","colonists","buildings"
    });
}

// ---------- Migration (JSON-level) ----------
// Update raw JSON from older schema versions to the current one.
// Example migration: v0->v1 rename "hp"->"health" in colonists if ever existed.
bool MigrateJsonInPlace(json& j, int target_schema_version, std::string& outError)
{
    try {
        int file_ver = j.value("schema_version", 1);
        while (file_ver < target_schema_version) {
            if (file_ver == 0) {
                // Example: rename colonist.hp -> colonist.health
                if (j.contains("colonists") && j["colonists"].is_array()) {
                    for (auto& c : j["colonists"]) {
                        if (c.is_object() && c.contains("hp") && !c.contains("health")) {
                            c["health"] = c["hp"];
                            c.erase("hp");
                        }
                    }
                }
                file_ver = 1;
                j["schema_version"] = file_ver;
            } else {
                // No known migration path
                outError = "No migration path for schema_version=" + std::to_string(file_ver);
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        outError = e.what();
        return false;
    }
}

// ---------- I/O ----------

static std::string NowUtcIso8601()
{
    using clock = std::chrono::system_clock;
    auto t = clock::now();
    std::time_t tt = clock::to_time_t(t);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::expected<SaveGame, SaveError>
LoadSaveGame(const std::filesystem::path& file,
             const std::filesystem::path& schemaPath)
{
    try {
        std::ifstream ifs(file, std::ios::binary);
        if (!ifs) {
            return std::unexpected(SaveError{ SaveError::Code::IoOpenFail, "Cannot open file: " + file.string() });
        }

        json doc = json::parse(ifs); // throws on malformed JSON. Basic usage per docs. :contentReference[oaicite:7]{index=7}

#if defined(COLONY_USE_JSON_SCHEMA_VALIDATION)
        if (!schemaPath.empty()) {
            std::ifstream sch(schemaPath);
            if (!sch) {
                return std::unexpected(SaveError{ SaveError::Code::JsonSchemaInvalid, "Cannot open schema: " + schemaPath.string() });
            }
            json schema = json::parse(sch);

            // pboettch/json-schema-validator usage pattern:
            // json_validator val; val.set_root_schema(schema); val.validate(doc);
            // (Supports draft-07.) :contentReference[oaicite:8]{index=8}
            nlohmann::json_schema::json_validator validator;
            try {
                validator.set_root_schema(schema);
                validator.validate(doc);
            } catch (const std::exception& e) {
                return std::unexpected(SaveError{ SaveError::Code::JsonSchemaInvalid, std::string("Schema validation failed: ") + e.what() });
            }
        }
#endif

        // Migrate old schema -> current
        std::string migErr;
        if (!MigrateJsonInPlace(doc, /*target*/1, migErr)) {
            return std::unexpected(SaveError{ SaveError::Code::MigrationFailed, migErr });
        }

        SaveGame sg = doc.get<SaveGame>(); // uses from_json() for each type. :contentReference[oaicite:9]{index=9}
        return sg;
    }
    catch (const nlohmann::json::type_error& e) {
        return std::unexpected(SaveError{ SaveError::Code::JsonTypeError, e.what() });
    }
    catch (const nlohmann::json::parse_error& e) {
        return std::unexpected(SaveError{ SaveError::Code::JsonParseError, e.what() });
    }
}

std::expected<void, SaveError>
SaveSaveGame(const SaveGame& save, const std::filesystem::path& file)
{
    try {
        SaveGame tmp = save;
        // Refresh timestamps (optional)
        if (tmp.created_utc.empty())
            tmp.created_utc = NowUtcIso8601();
        tmp.last_saved_utc = NowUtcIso8601();

        json j = tmp; // to_json()

        std::filesystem::create_directories(file.parent_path());
        std::ofstream ofs(file, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            return std::unexpected(SaveError{ SaveError::Code::IoWriteFail, "Cannot open for write: " + file.string() });
        }
        ofs << j.dump(2); // pretty-print; change to 0 for compact
        return {};
    }
    catch (const std::exception& e) {
        return std::unexpected(SaveError{ SaveError::Code::IoWriteFail, e.what() });
    }
}

} // namespace colony::save
