#include "TracyIntegration.h"
#include "TracyInclude.h"  // use the shim to safely include Tracy or stubs

namespace prof {

void Init(const char* appName)
{
#if defined(TRACY_ENABLE)
    // Tracy client doesn’t require init. Optionally tag main thread.
    if (appName && *appName) tracy::SetThreadName(appName);
#else
    (void)appName;
#endif
}

void Shutdown()
{
    // Tracy client shuts down automatically on process exit.
}

void MarkFrame()
{
#if defined(TRACY_ENABLE)
    FrameMark; // expands to tracy::Profiler::SendFrameMark(nullptr)
#endif
}

void MarkFrameNamed(const char* name)
{
#if defined(TRACY_ENABLE)
    // Frame tick with a label
    FrameMarkNamed(name);
#else
    (void)name;
#endif
}

void MarkFrameStart(const char* name)
{
#if defined(TRACY_ENABLE)
    // Start a discontinuous frame range
    FrameMarkStart(name);
#else
    (void)name;
#endif
}

void MarkFrameEnd(const char* name)
{
#if defined(TRACY_ENABLE)
    // End a discontinuous frame range
    FrameMarkEnd(name);
#else
    (void)name;
#endif
}

void SetThreadName(const char* name)
{
#if defined(TRACY_ENABLE)
    tracy::SetThreadName(name ? name : "thread");
#else
    (void)name;
#endif
}

void Message(const char* text, std::size_t len)
{
#if defined(TRACY_ENABLE)
    if (!text) return;
    tracy::Message(text, (uint32_t)len);
#else
    (void)text; (void)len;
#endif
}

void MessageColor(const char* text, std::size_t len, std::uint32_t rgb)
{
#if defined(TRACY_ENABLE)
    if (!text) return;
    tracy::MessageC(text, (uint32_t)len, rgb);
#else
    (void)text; (void)len; (void)rgb;
#endif
}

} // namespace prof
