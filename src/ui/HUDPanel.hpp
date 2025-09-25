#include "ui/HUDPanel.hpp"

// ... wherever you build your frame UI:
{
    // Fill the view‑model from your game state
    colony::ui::HUDViewModel vm;
    vm.colonistCount = (int)world.colonists.size();          // <-- replace with your accessor
    vm.paused        = sim.paused;                           // <-- replace with your accessor
    vm.timeScale     = sim.timeScale;                        // <-- replace with your accessor

    // Provide any resources you track (example: Wood, Stone, Food)
    static colony::ui::HUDResource res[3];
    res[0] = {"Wood",  resources.wood};                      // <-- replace with your accessor
    res[1] = {"Stone", resources.stone};
    res[2] = {"Food",  resources.food};

    vm.resources     = res;
    vm.resourceCount = 3; // adjust to your array size

    // Draw HUD and react to user actions
    colony::ui::HUDActions act{};
    colony::ui::DrawHUD(vm, act);

    if (act.togglePause)         sim.paused    = !sim.paused;
    if (act.setTimeScale) { sim.timeScale = act.newTimeScale; sim.paused = false; }
}
