// src/app/GpuHighPerfExports.cpp
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001; // NVIDIA
__declspec(dllexport) int           AmdPowerXpressRequestHighPerformance = 1; // AMD
}
