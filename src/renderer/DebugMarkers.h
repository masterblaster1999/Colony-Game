// renderer/DebugMarkers.h
#if defined(USE_PIX)
  #include <pix3.h>
  #define GPU_EVENT_BEGIN(cmd, col, name) PIXBeginEvent(cmd, col, name)
  #define GPU_EVENT_END(cmd)              PIXEndEvent(cmd)
  #define CPU_SCOPED_EVENT(col, name)     PIXScopedEvent(col, name)
#else
  #define GPU_EVENT_BEGIN(cmd, col, name) ((void)0)
  #define GPU_EVENT_END(cmd)              ((void)0)
  struct Noop { Noop(...){} }; #define CPU_SCOPED_EVENT(...) Noop _pix_scope__
#endif
