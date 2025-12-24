// src/platform/win/GpuPreference.cpp
// -------------------------------------------------------------------------------------------------
// Hybrid GPU hint exports (Optimus / PowerXpress)
//
// Why this exists:
//   On systems with an iGPU + dGPU, some drivers decide which GPU to run the process on.
//   Exporting these well-known variables provides a best-effort hint to prefer the high-performance
//   (usually discrete) GPU.
//
// IMPORTANT:
//   * Compile this file into the FINAL GAME EXE ONLY (not a static library, not the launcher),
//     otherwise you'll get duplicate symbol definitions at link time.
//   * These are only hints; OS / driver / OEM settings can still override.
// -------------------------------------------------------------------------------------------------

#if defined(_WIN32)

  #include <windows.h> // DWORD
  static_assert(sizeof(DWORD) == 4, "DWORD must be 32-bit");

  extern "C" {

    // NVIDIA Optimus hint (Release 302+): 1 = prefer discrete GPU
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

    // AMD PowerXpress hint: 1 = prefer high-performance GPU
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;

  } // extern "C"

#endif // _WIN32
