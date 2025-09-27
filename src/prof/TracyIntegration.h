// src/prof/TracyIntegration.h
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
  // ---- No-op fallbacks when Tracy is disabled -----------------
  // CPU zones
  #define ZoneScoped                 do {} while(0)
  #define ZoneScopedN(x)             do { (void)sizeof(x); } while(0)
  #define FrameMark                  do {} while(0)

  // Optional helpers often used in codebases that include Tracy
  // (defined as no-ops here so you can use them freely).
  #define TracyPlot(name, value)     do { (void)sizeof(name); (void)sizeof(value); } while(0)
  #define TracyMessage(msg, len)     do { (void)sizeof(msg); (void)sizeof(len); } while(0)
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
