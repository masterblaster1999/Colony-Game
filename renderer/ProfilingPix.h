#if defined(_WIN32) && !defined(NDEBUG)
  #include <WinPixEventRuntime/pix3.h> // via NuGet or vendored
  #define PIX_SCOPED(eventName) PIXScopedEvent(PIX_COLOR(0xFF00A0FF), eventName)
  #define PIX_MARKER(eventName) PIXSetMarker(PIX_COLOR(0xFFFFA000), eventName)
#else
  #define PIX_SCOPED(eventName) ((void)0)
  #define PIX_MARKER(eventName) ((void)0)
#endif
