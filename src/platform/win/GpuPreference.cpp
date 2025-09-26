// src/platform/win/GpuPreference.cpp
// Prefer high-performance GPU on hybrid (iGPU + dGPU) Windows laptops.
// Must be compiled into the EXE (not a DLL) so the symbols appear in the
// executable's export table for the driver to detect.

#if defined(_WIN32)
  #include <windows.h>

  // NVIDIA Optimus (checks for presence of this exported symbol):
  //   NvOptimusEnablement = 0x00000001
  //
  // AMD PowerXpress / Switchable Graphics (checks for presence of this symbol):
  //   AmdPowerXpressRequestHighPerformance = 1
  //
  // These variables are read by the GPU drivers at process start and hint that
  // the discrete/high‑performance GPU should be used for this application.

  extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // NVIDIA
    __declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1; // AMD
  }
#endif
