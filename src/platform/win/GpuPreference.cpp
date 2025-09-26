// Prefer high-performance GPU on dual-GPU laptops (NVIDIA/AMD)
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // NVIDIA
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1; // AMD
}
