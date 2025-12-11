// HighPerfGPU.cpp
// -----------------------------------------------------------------------------
// Windows-only: Hint NVIDIA Optimus & AMD PowerXpress to prefer the discrete,
// high-performance GPU for this process by exporting well-known symbols.
//
// Build notes:
//  * Add this .cpp to your EXE target(s) ONLY (do not compile into a static lib
//    or into a separate launcher EXE that spawns the game).
//  * Keep exactly one definition of each symbol in the process to avoid ODR/link
//    issues.
//
// References:
//  - NVIDIA Optimus Rendering Policies (TB-05942):
//      extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }
//  - AMD PowerXpress guidance (GPUOpen / AMD docs):
//      extern "C" { __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001; }
//
// Optional build switches:
//  * Define CG_ENABLE_GPU_PREFERENCE_EXPORTS=0 to compile this file as a no-op.
//  * Define CG_FORCE_LINK_GPU_EXPORTS to force the linker to keep the symbols
//    if you ever compile this into a static lib by mistake.
// -----------------------------------------------------------------------------

#if !defined(_WIN32)
#  error "HighPerfGPU.cpp is Windows-only. Exclude this file on non-Windows builds."
#endif

#ifndef CG_ENABLE_GPU_PREFERENCE_EXPORTS
#  define CG_ENABLE_GPU_PREFERENCE_EXPORTS 1
#endif

#if CG_ENABLE_GPU_PREFERENCE_EXPORTS

// Use a 32-bit type; drivers only check the presence & low bit of the value.
// We avoid including <Windows.h> to keep this TU lightweight.
extern "C" {

// NVIDIA Optimus: 0x00000001 => prefer High Performance Graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;

// AMD PowerXpress: 0x00000001 => prefer high-performance GPU
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 0x00000001;

} // extern "C"

// Sanity: on Windows/MSVC, unsigned long is 32-bit.
static_assert(sizeof(unsigned long) == 4, "Expected 32-bit unsigned long on Windows");

#if defined(_MSC_VER) && defined(CG_FORCE_LINK_GPU_EXPORTS)
// If this TU were ever compiled into a static library, these pragmas help
// force the linker to include the object so the exports are present.
// Not required when compiling directly into an EXE.
#  if defined(_M_IX86)
#    pragma comment(linker, "/include:_NvOptimusEnablement")
#    pragma comment(linker, "/include:_AmdPowerXpressRequestHighPerformance")
#  else
#    pragma comment(linker, "/include:NvOptimusEnablement")
#    pragma comment(linker, "/include:AmdPowerXpressRequestHighPerformance")
#  endif
#endif

#endif // CG_ENABLE_GPU_PREFERENCE_EXPORTS
