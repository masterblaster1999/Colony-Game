#pragma once

#include "game/proto/ProtoWorld.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace colony::game::editor {

// Small undo/redo history for plan placement.
//
// Design goals:
//  - group drag strokes / rectangle placements into a single command
//  - be robust even if the same tile is touched multiple times within a command
//  - avoid deep coupling: history uses World::placePlan + cell() access
//
// Notes:
//  - Undo/redo restores *plan + workRemaining* and the wood count.
//  - Reservations/jobs are intentionally cleared after applying a command.
class PlanHistory {
public:
    struct TileSnapshot {
        colony::proto::TileType planned = colony::proto::TileType::Empty;
        std::uint8_t planPriority = 0;
        float workRemaining = 0.0f;
    };

    struct TileEdit {
        int x = 0;
        int y = 0;
        TileSnapshot before{};
        TileSnapshot after{};
    };

    struct Command {
        int woodBefore = 0;
        int woodAfter  = 0;
        std::vector<TileEdit> edits;
    };

    PlanHistory() = default;

    void Clear() noexcept;

    void SetMaxCommands(std::size_t max) noexcept { m_maxCommands = max; }
    [[nodiscard]] std::size_t MaxCommands() const noexcept { return m_maxCommands; }

    [[nodiscard]] bool HasActiveCommand() const noexcept { return m_active.has_value(); }

    // Begin a new command. Does not clear redo until you commit.
    void BeginCommand(int woodBefore);

    // Records a tile change.
    // If the tile already exists within the active command, this updates the "after" snapshot.
    void RecordChange(int x, int y, TileSnapshot before, TileSnapshot after);

    // Commits the active command.
    // Returns false if the command contained no edits.
    bool CommitCommand(int woodAfter);

    // Discards the active command without affecting undo/redo.
    void CancelCommand() noexcept;

    [[nodiscard]] std::size_t UndoCount() const noexcept { return m_undo.size(); }
    [[nodiscard]] std::size_t RedoCount() const noexcept { return m_redo.size(); }

    [[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }

    bool Undo(colony::proto::World& world);
    bool Redo(colony::proto::World& world);

private:
    bool apply(colony::proto::World& world, const Command& cmd, bool useAfter);

    std::size_t m_maxCommands = 128;
    std::optional<Command> m_active;
    std::vector<Command> m_undo;
    std::vector<Command> m_redo;
};

} // namespace colony::game::editor
