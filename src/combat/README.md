# Combat module (src/combat)

This folder is a **self-contained, deterministic combat simulation** that doesn't
depend on the rest of the engine (only the C++ standard library). It is designed
to be easy to integrate into your existing game loop.

## Quick usage

```cpp
#include "combat/CGCombatSimulation.hpp"

colony::combat::CombatSimulation sim;
sim.seed(1234);

// Create entities (use your game's entity IDs)
auto& a = sim.world().create_or_get(1);
a.faction = 1;
a.position = {0,0};
a.max_health = a.health = 25;
a.weapon.name = "Rifle";
a.weapon.range = 10;
a.weapon.damage = colony::combat::DamagePacket::single(colony::combat::DamageType::Kinetic, 6);

auto& b = sim.world().create_or_get(2);
b.faction = 2;
b.position = {5,0};
b.max_health = b.health = 20;

// Issue an attack and step the sim
sim.request_attack(1, 2);
sim.update(1.0f/60.0f);

// Read events for UI / logs / sound
for (auto e : sim.world().events()) { ... }
