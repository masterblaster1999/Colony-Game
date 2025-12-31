#pragma once

#include "game/proto/ProtoWorld.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace colony::game::editor {

// A small, clipboard-shareable blueprint for the PrototypeGame plan system.
//
// Each cell is stored as a packed byte:
//   bits 0-3 : proto::TileType (0..15)
//   bits 4-5 : plan priority   (0..3)
//   bits 6-7 : reserved
struct PlanBlueprint {
    int w = 0;
    int h = 0;

    // Row-major, size == w*h.
    std::vector<std::uint8_t> packed;

    void Clear() noexcept;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t CellCount() const noexcept { return packed.size(); }
};

// Pack/unpack helpers.
[[nodiscard]] std::uint8_t BlueprintPack(colony::proto::TileType t, std::uint8_t priority) noexcept;
[[nodiscard]] colony::proto::TileType BlueprintUnpackTile(std::uint8_t packed) noexcept;
[[nodiscard]] std::uint8_t BlueprintUnpackPriority(std::uint8_t packed) noexcept;

// Blueprint transforms (operate on the packed cell data).
//
// Rotation is around the blueprint's origin (top-left).
// All transforms preserve the packed byte (tile + priority).
[[nodiscard]] PlanBlueprint BlueprintRotateCW(const PlanBlueprint& bp) noexcept;
[[nodiscard]] PlanBlueprint BlueprintRotateCCW(const PlanBlueprint& bp) noexcept;
[[nodiscard]] PlanBlueprint BlueprintRotate180(const PlanBlueprint& bp) noexcept;
[[nodiscard]] PlanBlueprint BlueprintFlipHorizontal(const PlanBlueprint& bp) noexcept;
[[nodiscard]] PlanBlueprint BlueprintFlipVertical(const PlanBlueprint& bp) noexcept;


// Blueprint utility helpers.
//
// These are used by tooling (UI/library) to keep blueprints compact and cache-friendly.
struct BlueprintBounds {
    int x0 = 0;
    int y0 = 0;
    int x1 = -1;
    int y1 = -1;

    [[nodiscard]] bool Empty() const noexcept { return x1 < x0 || y1 < y0; }
    [[nodiscard]] int Width() const noexcept { return Empty() ? 0 : (x1 - x0 + 1); }
    [[nodiscard]] int Height() const noexcept { return Empty() ? 0 : (y1 - y0 + 1); }
};

// Returns the minimal bounds that contain all non-empty cells (tile != Empty).
// If the blueprint has no non-empty cells, returns an empty bounds (x1 < x0).
[[nodiscard]] BlueprintBounds BlueprintNonEmptyBounds(const PlanBlueprint& bp) noexcept;

// Extracts a sub-rectangle from the blueprint.
// Out-of-range source cells are treated as Empty.
[[nodiscard]] PlanBlueprint BlueprintCrop(const PlanBlueprint& bp, int x0, int y0, int w, int h) noexcept;

// Removes empty rows/columns on the outside of a blueprint.
// If all cells are empty, returns an empty blueprint.
[[nodiscard]] PlanBlueprint BlueprintTrimEmptyBorders(const PlanBlueprint& bp) noexcept;

// Stable hash for caching / de-duplication (FNV-1a 64-bit over dims + packed bytes).
[[nodiscard]] std::uint64_t BlueprintHash64(const PlanBlueprint& bp) noexcept;


// JSON serialization used for clipboard exchange.
//
// The string is a single JSON object with a lightweight RLE payload.
[[nodiscard]] std::string PlanBlueprintToJson(const PlanBlueprint& bp);

// Returns false on parse/validation failure.
bool PlanBlueprintFromJson(std::string_view text,
                           PlanBlueprint& out,
                           std::string* outError = nullptr) noexcept;

} // namespace colony::game::editor
