# Colony-Game Patch (Round 2)

This patch fixes the remaining MSVC build errors:

- C4150: deletion of pointer to incomplete type 'colony::game::AsyncSaveManager'
- C2338: static assertion failed: "can't delete an incomplete type"
- C2027: use of undefined type 'colony::game::AsyncSaveManager'

## What changed

### 1) Use a custom deleter for AsyncSaveManager
MSVC's std::default_delete<T> has a static_assert that fails if T is incomplete at the point the unique_ptr destructor is instantiated.
To make this robust, we:

- Add `AsyncSaveManagerDeleter` + `AsyncSaveManagerPtr` in `src/game/PrototypeGame_Impl.h`
- Change `PrototypeGame::Impl::saveMgr` to use `AsyncSaveManagerPtr` instead of `std::unique_ptr<AsyncSaveManager>`

### 2) Define the deleter where AsyncSaveManager is complete
`AsyncSaveManager` is defined in `src/game/PrototypeGame_SaveLoad.cpp`, so we define:

`void AsyncSaveManagerDeleter::operator()(AsyncSaveManager*) const noexcept`

in that file, after the `AsyncSaveManager` definition.

### 3) Replace make_unique calls
Because `std::make_unique` returns a `std::unique_ptr<T>` with the default deleter, we replace:

`std::make_unique<AsyncSaveManager>()`

with:

`AsyncSaveManagerPtr{ new AsyncSaveManager() }`

## Files in this zip

- src/game/PrototypeGame_Impl.h
- src/game/PrototypeGame_SaveLoad.cpp
- PATCH_NOTES.md
