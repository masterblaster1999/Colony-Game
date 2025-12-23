# Patch 4: InputMapper + WASD/QE Camera Movement

This patch builds on the previous refactors (AppWindow split + InputQueue + PrototypeGame).

## What it adds

- `src/input/InputMapper.h/.cpp`
  - Action enum + simple key binds
  - Tracks key state from `InputEvent` (`KeyDown/KeyUp`)
  - Exposes movement axes (WASD + QE)

## What it changes

- AppWindow now forwards **W/A/S/D/Q/E** as `InputEventType::KeyDown/KeyUp` into the queue.
- AppWindow emits `InputEventType::FocusLost` on focus loss to clear held keys.
- PrototypeGame now uses InputMapper to apply **continuous** keyboard movement to the debug camera.

## Apply order

Apply after Patch 3 (InputQueue + PrototypeGame decoupling).

