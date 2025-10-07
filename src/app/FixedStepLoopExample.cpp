// src/app/FixedStepLoopExample.cpp
// You can either compile this file or copy its contents into your app loop.
// Build guards keep it harmless if ImGui/Tracy/Taskflow aren't present.

#include <chrono>
#include <functional>

#include "core/FixedTimestep.h"
#include "core/Profile.h"
#include "engine/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"
#include "ui/DebugHud.h"

namespace {

inline double NowSeconds() {
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  auto now = clock::now();
  std::chrono::duration<double> diff = now - t0;
  return diff.count();
}

} // namespace

// Call this from your app after window/device init.
// Returns when you break the loop (e.g., when a quit flag is set).
int RunFixedStepLoop_Example(bool (*poll_os_and_should_quit)()) {
  colony::World world;
  colony::DebugHud hud(/*history*/240);

  // Example: create a test entity to prove the pipeline works.
  {
    auto e = world.registry.create();
    world.registry.emplace<colony::ecs::Name>(e, colony::ecs::Name{"Spinner"});
    world.registry.emplace<colony::ecs::Transform>(e);
    world.registry.emplace<colony::ecs::Growth>(e, colony::ecs::Growth{ 2.0f, 0.0f });
    world.registry.emplace<colony::ecs::Tickable>(e,
      colony::ecs::Tickable{
        [](entt::registry& r, entt::entity me, double dt){
          auto& t = r.get<colony::ecs::Transform>(me);
          t.rot += static_cast<float>(dt) * 1.0f; // spin slowly
        },
        true
      }
    );
    world.registry.emplace<colony::ecs::Renderable>(e,
      colony::ecs::Renderable{
        [](entt::registry& r, entt::entity me, float alpha){
          (void)r; (void)me; (void)alpha;
          // TODO: bind your renderer here; draw a thing for 'me'.
          // For now, this is just a stub.
        },
        true
      }
    );
  }

  // Fixed stepper at 60 Hz, clamp catch-up to 5 ticks/frame
  colony::FixedSettings fs;
  fs.tick_hz = 60.0;
  fs.max_catchup_ticks = 5;
  fs.max_frame_dt = 0.25;
  colony::FixedStepper stepper(fs);
  stepper.reset(NowSeconds());

  bool running = true;
  while (running) {
    if (poll_os_and_should_quit && poll_os_and_should_quit()) {
      break;
    }

    const double now = NowSeconds();
    auto stats = stepper.step(
      now,
      // update(dt)
      [&](double dt){
        CG_ZONE("Simulation");
        world.sim_time_seconds += dt;

        // 1) Tickable systems
        const auto ticked = colony::ecs::UpdateTickables(world.registry, dt);

        // 2) "Jobs" example; parallel growth updates (Taskflow if available)
#if CG_WITH_TASKFLOW
        const auto grown = colony::ecs::UpdateGrowthParallel(world.registry, dt, world.jobs);
#else
        const auto grown = colony::ecs::UpdateGrowthParallel(world.registry, dt, nullptr);
#endif
        (void)ticked; (void)grown;
      },
      // render(alpha)
      [&](float alpha){
        CG_ZONE("RenderFramePrep");

        // 3) Render pass via ECS renderables
        (void)colony::ecs::RenderPass(world.registry, alpha);

        // 4) Draw the debug HUD
        colony::DebugHudMetrics m{};
        m.sim_time_seconds   = world.sim_time_seconds;
        m.tick_hz            = fs.tick_hz;
        m.ticks_this_frame   = stats.ticks_this_frame;
        m.frame_dt_seconds   = stats.frame_dt;
        m.clamped_dt_seconds = stats.clamped_dt;
        m.alpha              = stats.alpha;
        hud.update(m);
        hud.draw();

        // 5) Present (TODO: call your swap/present here)
        // present();
      }
    );

    // Optional: throttle or yield; your renderer/vsync usually does this.
    // std::this_thread::sleep_for(std::chrono::milliseconds(0));
  }

  return 0;
}
