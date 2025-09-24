#pragma once
#include <d3d11.h>

// ---- CPU/GPU profiling integration for Tracy ------------------
// Build-time:
//   * Add TRACY_ENABLE to your compile definitions when you want profiling.
//   * Compile tracy/public/TracyClient.cpp into your target.
//   * Run Tracy.exe (the UI) and press "Connect".
//
// We provide no-op fallbacks so you can include this unconditionally.

#ifdef TRACY_ENABLE
  #include "Tracy.hpp"
  #include "TracyD3D11.hpp"
#else
  // CPU zones
  #define ZoneScoped           do {} while(0)
  #define ZoneScopedN(x)       do {} while(0)
  #define FrameMark            do {} while(0)
#endif

namespace cg::prof
{
    // Initialize GPU profiling (safe to call multiple times).
    void InitD3D11(ID3D11Device* dev, ID3D11DeviceContext* ctx);

    // Collect GPU events; call once per frame (best right after Present()).
    void CollectD3D11();

    // Shutdown on exit.
    void ShutdownD3D11();

    // Optional helpers for CPU zones in code without TRACY_ENABLE checks:
    inline void FrameMarkCPU() { FrameMark; }
}
