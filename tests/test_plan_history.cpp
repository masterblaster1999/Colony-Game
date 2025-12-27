// tests/test_plan_history.cpp
//
// Coverage for the prototype plan undo/redo system (game/editor/PlanHistory).
//
// Goals:
//  - Undo/redo restores per-tile plan state (planned type, priority, workRemaining)
//  - Undo/redo restores the saved wood counts for the command
//  - Duplicate edits to the same tile within a command are merged correctly
//  - Redo respects the configured max history cap even if it changes after undo

#include <doctest/doctest.h>

#include "game/editor/PlanHistory.h"
#include "game/proto/ProtoWorld.h"

namespace {

using colony::game::editor::PlanHistory;

[[nodiscard]] PlanHistory::TileSnapshot Snap(const colony::proto::Cell& c) noexcept
{
    PlanHistory::TileSnapshot s;
    s.planned = c.planned;
    s.planPriority = c.planPriority;
    s.workRemaining = c.workRemaining;
    return s;
}

} // namespace

TEST_CASE("PlanHistory undo/redo round-trips plan changes and wood")
{
    using colony::proto::TileType;
    using colony::proto::PlacePlanResult;

    colony::proto::World w(8, 8, /*seed*/ 1);
    w.inventory().wood = 50;

    PlanHistory h;

    const int woodBefore = w.inventory().wood;
    const auto before = Snap(w.cell(1, 1));

    REQUIRE(w.placePlan(1, 1, TileType::Floor, /*priority*/ 2) == PlacePlanResult::Ok);

    const int woodAfter = w.inventory().wood;
    const auto after = Snap(w.cell(1, 1));

    h.BeginCommand(woodBefore);
    h.RecordChange(1, 1, before, after);
    CHECK(h.CommitCommand(woodAfter));

    // Undo should restore tile + wood.
    CHECK(h.Undo(w));
    {
        const auto c = w.cell(1, 1);
        CHECK(c.planned == before.planned);
        CHECK(c.planPriority == before.planPriority);
        CHECK(c.workRemaining == doctest::Approx(before.workRemaining));
        CHECK(w.inventory().wood == woodBefore);
    }

    // Redo should restore tile + wood.
    CHECK(h.Redo(w));
    {
        const auto c = w.cell(1, 1);
        CHECK(c.planned == after.planned);
        CHECK(c.planPriority == after.planPriority);
        CHECK(c.workRemaining == doctest::Approx(after.workRemaining));
        CHECK(w.inventory().wood == woodAfter);
    }
}

TEST_CASE("PlanHistory merges duplicate edits to the same tile within a command")
{
    using colony::proto::TileType;
    using colony::proto::PlacePlanResult;

    colony::proto::World w(8, 8, /*seed*/ 1);
    w.inventory().wood = 100;

    PlanHistory h;

    const int wood0 = w.inventory().wood;
    h.BeginCommand(wood0);

    // First edit: Empty -> Floor
    const auto before0 = Snap(w.cell(2, 2));
    REQUIRE(w.placePlan(2, 2, TileType::Floor, /*priority*/ 0) == PlacePlanResult::Ok);
    const auto mid = Snap(w.cell(2, 2));
    h.RecordChange(2, 2, before0, mid);

    // Second edit on same tile within same command: Floor -> Wall
    const auto before1 = Snap(w.cell(2, 2));
    REQUIRE(w.placePlan(2, 2, TileType::Wall, /*priority*/ 3) == PlacePlanResult::Ok);
    const auto after = Snap(w.cell(2, 2));
    h.RecordChange(2, 2, before1, after);

    const int woodFinal = w.inventory().wood;
    CHECK(h.CommitCommand(woodFinal));

    // Undo should return to the original *before0* (not the mid-state).
    CHECK(h.Undo(w));
    {
        const auto c = w.cell(2, 2);
        CHECK(c.planned == before0.planned);
        CHECK(c.planPriority == before0.planPriority);
        CHECK(c.workRemaining == doctest::Approx(before0.workRemaining));
        CHECK(w.inventory().wood == wood0);
    }

    // Redo should return to the final *after* (Wall).
    CHECK(h.Redo(w));
    {
        const auto c = w.cell(2, 2);
        CHECK(c.planned == after.planned);
        CHECK(c.planPriority == after.planPriority);
        CHECK(c.workRemaining == doctest::Approx(after.workRemaining));
        CHECK(w.inventory().wood == woodFinal);
    }
}

TEST_CASE("PlanHistory redo respects max history cap even if cap changes after undo")
{
    using colony::proto::TileType;
    using colony::proto::PlacePlanResult;

    colony::proto::World w(8, 8, /*seed*/ 1);
    w.inventory().wood = 100;

    PlanHistory h;
    h.SetMaxCommands(8);

    // Command 1: place a Floor.
    {
        const int woodBefore = w.inventory().wood;
        const auto before = Snap(w.cell(1, 1));
        REQUIRE(w.placePlan(1, 1, TileType::Floor, /*priority*/ 1) == PlacePlanResult::Ok);
        const int woodAfter = w.inventory().wood;
        const auto after = Snap(w.cell(1, 1));

        h.BeginCommand(woodBefore);
        h.RecordChange(1, 1, before, after);
        CHECK(h.CommitCommand(woodAfter));
    }

    const auto floorAfter = Snap(w.cell(1, 1));
    const int woodAfterCmd1 = w.inventory().wood;

    // Command 2: place a Wall.
    {
        const int woodBefore = w.inventory().wood;
        const auto before = Snap(w.cell(2, 2));
        REQUIRE(w.placePlan(2, 2, TileType::Wall, /*priority*/ 0) == PlacePlanResult::Ok);
        const int woodAfter = w.inventory().wood;
        const auto after = Snap(w.cell(2, 2));

        h.BeginCommand(woodBefore);
        h.RecordChange(2, 2, before, after);
        CHECK(h.CommitCommand(woodAfter));
    }

    const auto wallAfter = Snap(w.cell(2, 2));
    const auto wallBefore = PlanHistory::TileSnapshot{}; // (empty)

    CHECK(h.UndoCount() == 2);

    // Undo command 2 so it's sitting in the redo stack.
    CHECK(h.Undo(w));
    CHECK(h.UndoCount() == 1);
    CHECK(h.RedoCount() == 1);

    // User lowers the max history cap at runtime.
    h.SetMaxCommands(1);

    // Redo command 2; undo stack should be trimmed to <= 1.
    CHECK(h.Redo(w));
    CHECK(h.UndoCount() == 1);
    CHECK(h.RedoCount() == 0);

    // Undo should remove the wall plan but keep the floor plan (cmd1 is no longer undoable).
    CHECK(h.Undo(w));

    {
        const auto cFloor = w.cell(1, 1);
        CHECK(cFloor.planned == floorAfter.planned);
        CHECK(cFloor.planPriority == floorAfter.planPriority);

        const auto cWall = w.cell(2, 2);
        CHECK(cWall.planned == wallBefore.planned);
        CHECK(cWall.planPriority == wallBefore.planPriority);
        CHECK(cWall.workRemaining == doctest::Approx(wallBefore.workRemaining));

        CHECK(w.inventory().wood == woodAfterCmd1);
    }
}
