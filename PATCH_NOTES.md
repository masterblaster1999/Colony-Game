# Patch 5: InputMapper multi-binds + action events + Shift speed boost

## What this patch adds

- **Multiple bindings per action** in `src/input/InputMapper.*`
  - Default bindings now include **Arrow keys** as alternatives to WASD.
- **ActionEvent stream (Pressed/Released)**
  - The mapper emits action transition events each frame, so gameplay can react to actions.
  - Gameplay never needs to inspect raw key codes.
- **Shift speed boost**
  - `Action::SpeedBoost` bound to Shift.
  - `PrototypeGame` multiplies pan/zoom speed while boost is held.

## What changed

- `src/AppWindow.cpp`
  - Forwards **all non-system** key down/up messages into the input queue (instead of whitelisting only WASD/QE).
  - System keys remain handled in the window layer: **Esc**, **V**, **F11**, **Alt+Enter**.

- `src/input/InputMapper.h/.cpp`
  - Adds `Action::SpeedBoost`.
  - Supports multiple bindings per action.
  - Produces an `ActionEvent` stream (`Pressed` / `Released`).

- `src/game/PrototypeGame.cpp`
  - Uses `Action::SpeedBoost` to apply a movement speed multiplier.

## New/updated controls

- Movement:
  - **WASD** or **Arrow keys** for pan
  - **Q/E** for zoom
  - **Shift** = speed boost

