#include <windows.h>

// Exported globals recognized by NVIDIA Optimus & AMD PowerXpress drivers.
// Must exist exactly once in the process.
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001UL; // prefer dGPU
    __declspec(dllexport) int           AmdPowerXpressRequestHighPerformance = 1; // prefer dGPU
}
