// src/prof/TracyIntegration.h
#pragma once

#if defined(_WIN32)
  #define CG_WINDOWS 1
#endif

#if defined(TRACY_ENABLE)
  #include <tracy/Tracy.hpp>     // ZoneScoped, FrameMark*, TracyMessage, etc.
  // If you later add fibers/async zones, also include <tracy/TracyC.h>.
#else
  // Minimal no-op fallbacks (subset)
  #define ZoneScoped          do{}while(0)
  #define ZoneScopedN(x)      do{}while(0)
  #define FrameMark           do{}while(0)
  #define FrameMarkStart(x)   do{}while(0)
  #define FrameMarkEnd(x)     do{}while(0)
  #define TracyMessage(x,y)   do{}while(0)
  namespace tracy { inline void SetThreadName(const char*){} }
#endif

namespace cg::prof {

// Call once very early (WinMain / wWinMain)
void InitTracy(const char* programName);

// Optional: attach a short “build / runtime” blurb to the trace
void AppInfo(const char* text);

} // namespace cg::prof

// --------- short local macros you can drop anywhere ----------
#define CG_ZONE()                  ZoneScoped
#define CG_ZONE_N(name)            ZoneScopedN(name)     // name must be a literal
#define CG_FRAME()                 FrameMark
#define CG_STARTUP_BEGIN()         FrameMarkStart("Startup")
#define CG_STARTUP_END()           FrameMarkEnd("Startup")
#define CG_THREAD(name)            ::tracy::SetThreadName(name)

#if defined(TRACY_ENABLE)
// attach short text to the *current* zone; length must fit uint16_t
#define CG_ZONE_TEXT(txt, len)     ZoneText(txt, (uint16_t)(len))
#define CG_MSG_L(text)             TracyMessage(text, (uint16_t)strlen(text))
#else
#define CG_ZONE_TEXT(txt, len)     do{}while(0)
#define CG_MSG_L(text)             do{}while(0)
#endif
