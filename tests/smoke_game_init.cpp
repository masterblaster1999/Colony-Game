// tests/smoke_game_init.cpp
#include <doctest/doctest.h>
#include "game/core/Game.h"
#include "game/components/Agent.h"

TEST_CASE("game initializes an agent") {
    Game g; g.init();
    auto count = g.world().view<Agent>().size();
    CHECK(count >= 1);
}
