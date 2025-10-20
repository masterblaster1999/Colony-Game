// src/prof/TracyIntegration.cpp
#include "TracyIntegration.h"

#if defined(TRACY_ENABLE)
  #include <cstring>
  #include <tracy/common/TracySystem.hpp>   // tracy::SetThreadName on some versions
  #include <tracy/Tracy.hpp>
#endif

namespace cg::prof {

void InitTracy(const char* programName)
{
#if defined(TRACY_ENABLE)
    // Program name in the UI (optional – available in recent versions)
    #if defined(TracySetProgramName)
      TracySetProgramName(programName);
    #endif

    // Name the main thread before any zones, so captured callstacks look nice
    ::tracy::SetThreadName("Main Thread");
    // Mark that we are entering the startup phase
    CG_STARTUP_BEGIN();

    // Minimal “build stamp” in the capture
    static const char kBuild[] = "Build: " __DATE__ " " __TIME__;
    TracyAppInfo(kBuild, (uint16_t)sizeof(kBuild)-1);
#endif
}

void AppInfo(const char* text)
{
#if defined(TRACY_ENABLE)
    if (!text) return;
    TracyAppInfo(text, (uint16_t)std::min<size_t>(std::strlen(text), 0xFFFF));
#endif
}

} // namespace cg::prof
