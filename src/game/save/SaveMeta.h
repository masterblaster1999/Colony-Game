#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace colony::game::save {

namespace fs = std::filesystem;

enum class SaveKind : std::uint8_t {
    Unknown = 0,
    Manual,
    Autosave,
};

struct SaveSummary {
    SaveKind kind = SaveKind::Unknown;

    // When the save was created (Unix seconds, UTC). 0 if unknown.
    std::int64_t savedUnixSecondsUtc = 0;

    // Real-time seconds since launch when the save was created. 0 if unknown.
    double playtimeSeconds = 0.0;

    // World size
    int worldW = 0;
    int worldH = 0;

    // Summary counts
    int population   = 0;
    int plannedCount = 0;

    // Inventory snapshot
    int   wood = 0;
    float food = 0.0f;

    // Built counts (subset)
    int builtFloors      = 0;
    int builtWalls       = 0;
    int builtFarms       = 0;
    int builtStockpiles  = 0;

    // Optional tiny thumbnail for the save browser.
    // Each byte packs: low nibble = built TileType, high nibble = planned TileType.
    int thumbW = 0;
    int thumbH = 0;
    std::vector<std::uint8_t> thumbPacked;
};

// Returns a sidecar metadata path for a given world save path.
// Example: "proto_world_slot_1.json" -> "proto_world_slot_1.meta.json"
[[nodiscard]] fs::path MetaPathFor(const fs::path& worldPath) noexcept;

// Reads a small sidecar meta file (written by the game's save system).
[[nodiscard]] bool ReadMetaFile(const fs::path& metaPath,
                                SaveSummary& outSummary,
                                std::string* outError = nullptr) noexcept;

// Formats a Unix timestamp (seconds, UTC) as a local-time string.
// Returns an empty string if unixSecondsUtc <= 0.
[[nodiscard]] std::string FormatLocalTime(std::int64_t unixSecondsUtc) noexcept;

// Formats a duration in seconds as "H:MM:SS".
[[nodiscard]] std::string FormatDurationHMS(double seconds) noexcept;

} // namespace colony::game::save
