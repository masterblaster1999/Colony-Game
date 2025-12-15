#pragma once

#include "CGCombatSimulation.hpp"

namespace colony::combat {

// Optional ImGui debug overlay to inspect combat state. If ImGui headers are not
// available in the build, this compiles to a no-op.
void draw_combat_debug_ui(CombatSimulation& sim);

} // namespace colony::combat
