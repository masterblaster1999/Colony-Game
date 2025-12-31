#pragma once

#include "game/editor/Blueprint.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace colony::game::editor {

namespace fs = std::filesystem;

// Metadata for a blueprint file on disk.
struct BlueprintFileInfo
{
    std::string name;   // sanitized, user-facing base name (no extension)
    fs::path    path;   // full path
    std::uintmax_t sizeBytes = 0;
    std::int64_t   modifiedUtcSeconds = 0; // Unix seconds (UTC), 0 if unknown
};

// Sanitizes a user-provided name into a filename-safe base (no extension).
// The result is never empty (falls back to "blueprint").
[[nodiscard]] std::string SanitizeBlueprintName(std::string_view name) noexcept;

// Builds a canonical blueprint path under dir for a given name.
// Uses the extension ".blueprint.json".
[[nodiscard]] fs::path BlueprintPathForName(const fs::path& dir, std::string_view name) noexcept;

// Ensures the blueprint directory exists (creates it if missing).
// Returns false on failure.
[[nodiscard]] bool EnsureBlueprintDir(const fs::path& dir, std::string* outError = nullptr) noexcept;

// Lists blueprint files in dir (sorted newest-first). Returns an empty list on errors.
[[nodiscard]] std::vector<BlueprintFileInfo> ListBlueprintFiles(const fs::path& dir) noexcept;

// Saves a blueprint to disk as JSON (same schema as clipboard exchange).
// Uses an atomic write to avoid partial files.
bool SaveBlueprintToFile(const PlanBlueprint& bp,
                         const fs::path& path,
                         std::string* outError = nullptr) noexcept;

// Loads a blueprint from disk.
bool LoadBlueprintFromFile(const fs::path& path,
                           PlanBlueprint& out,
                           std::string* outError = nullptr) noexcept;

// Deletes a blueprint file from disk.
bool DeleteBlueprintFile(const fs::path& path,
                         std::string* outError = nullptr) noexcept;

} // namespace colony::game::editor
