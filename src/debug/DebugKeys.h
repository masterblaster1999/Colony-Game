#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace dbg {

struct Callbacks {
    std::function<void(uint64_t)> regenerateWorld; // called with new seed
    std::function<void()> toggleHud;               // F5
    std::function<void()> toggleWireframe;         // F6
    std::function<void()> screenshot;              // Ctrl+PrtScr
};

struct SeedSource {
    uint64_t defaultSeed;                          // F2
    std::function<uint64_t()> randomSeed;          // F3 / Ctrl+R
};

struct State {
    std::unordered_map<int, bool> prev;
};

inline bool isDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

inline bool tap(State& s, int vk) {
    const bool d = isDown(vk);
    const bool was = s.prev[vk];
    s.prev[vk] = d;
    return d && !was; // rising edge
}

inline bool chord(const int mod, const int key) {
    return isDown(mod) && (GetAsyncKeyState(key) & 0x1) != 0; // async, low bit ~ toggles
}

// Call per-frame (anywhere in your update loop)
inline void HandleDebugKeys(State& st, const Callbacks& cb, const SeedSource& seeds) {
    if (tap(st, VK_F2) && cb.regenerateWorld) cb.regenerateWorld(seeds.defaultSeed);
    if (tap(st, VK_F3) && cb.regenerateWorld) cb.regenerateWorld(seeds.randomSeed ? seeds.randomSeed() : seeds.defaultSeed);

    if (tap(st, VK_F5) && cb.toggleHud) cb.toggleHud();
    if (tap(st, VK_F6) && cb.toggleWireframe) cb.toggleWireframe();

    if (isDown(VK_CONTROL) && tap(st, 'R') && cb.regenerateWorld) {
        cb.regenerateWorld(seeds.randomSeed ? seeds.randomSeed() : seeds.defaultSeed);
    }

    if (isDown(VK_CONTROL) && tap(st, VK_SNAPSHOT) && cb.screenshot) {
        cb.screenshot();
    }
}

} // namespace dbg
