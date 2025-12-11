#include <windows.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // NVIDIA Optimus: prefer dGPU
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1; // AMD PowerXpress: prefer dGPU
}

// Optional: runtime log or checks if you like
namespace cg {
void SelectHighPerformanceGPU() { /* nothing required at runtime */ }
}
