#include "TracyIntegration.h"
#include "TracyInclude.h"   // shim: includes <tracy/Tracy.hpp> if present, else safe no‑ops
#include <cstddef>          // std::size_t
#include <cstdint>          // std::uint32_t

namespace prof {

void Init(const char* appName)
{
#if defined(TRACY_ENABLE)
    // Tracy client doesn’t require init. Optionally tag the current thread.
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
    // Use the macro (is a no-op when TRACY_ENABLE is not defined)
    FrameMark;
}

void MarkFrameNamed(const char* name)
{
#if defined(TRACY_ENABLE)
    FrameMarkNamed(name);
#else
    (void)name;
#endif
}

void MarkFrameStart(const char* name)
{
#if defined(TRACY_ENABLE)
    FrameMarkStart(name);
#else
    (void)name;
#endif
}

void MarkFrameEnd(const char* name)
{
#if defined(TRACY_ENABLE)
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
    // Use Tracy macro, not tracy::Message(...)
    TracyMessage(text, len);
#else
    (void)text; (void)len;
#endif
}

void MessageColor(const char* text, std::size_t len, std::uint32_t rgb)
{
#if defined(TRACY_ENABLE)
    if (!text) return;
    // Colored message macro
    TracyMessageC(text, len, rgb);
#else
    (void)text; (void)len; (void)rgb;
#endif
}

} // namespace prof
