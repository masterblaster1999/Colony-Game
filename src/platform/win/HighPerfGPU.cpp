// src/platform/win/HighPerfGPU.cpp
#include <windows.h>

// Exported variables to nudge discrete GPUs.
// NVIDIA: Only LSB is currently used (0x1 => High Performance).
// AMD: Must be exported by the EXE (not a DLL).
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // NVIDIA Optimus
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1; // AMD Hybrid Graphics
}

// Optional helper, if you want a call-site:
namespace cg {
    inline void SelectHighPerformanceGPU() noexcept {
        // No runtime work required; export exists in image.
    }
}
