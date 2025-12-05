// src/platform/win/GpuPreference.cpp
// Hint hybrid‑GPU drivers to prefer the high‑performance (discrete) GPU.
//
// IMPORTANT:
//  * Compile this file into the FINAL GAME EXE ONLY (not a static lib, not the launcher)
//    to avoid duplicate definitions at link time.
//  * These exports are best‑effort hints; users and OEM/global settings can still override.
//
// References:
//  - NVIDIA Optimus: "Global Variable NvOptimusEnablement" (Release 302+). 
//  - AMD PowerXpress: AmdPowerXpressRequestHighPerformance must be defined in the GAME PROCESS.
//    (See vendor docs cited in the PR/commit message.)

#if defined(_WIN32)

  #include <windows.h> // for DWORD
  static_assert(sizeof(DWORD) == 4, "DWORD must be 32-bit");

  extern "C" {

  // NVIDIA Optimus: prefer High Performance Graphics GPU.
  // Doc: "Enabling High Performance Graphics Rendering on Optimus Systems", TB-05942-003 (Release 302+).
  // Value semantics: only the LSB is considered; 1 = prefer discrete GPU.
  __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;  // :contentReference[oaicite:1]{index=1}

  // AMD PowerXpress: prefer High Performance GPU.
  // Doc: AMD CrossFire guide (D3D11) — MUST be exported by the game process (not a launcher).
  __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;  // :contentReference[oaicite:2]{index=2}

  } // extern "C"

#endif // _WIN32
