#pragma once

#if defined(USE_PIX) && USE_PIX
  #include <WinPixEventRuntime/pix3.h>
  struct PixScopedEvent {
    PixScopedEvent(ID3D12GraphicsCommandList* cl, UINT64 color, const char* name)
      : cmd(cl) { PIXBeginEvent(cmd, color, name); }
    ~PixScopedEvent() { PIXEndEvent(cmd); }
    ID3D12GraphicsCommandList* cmd;
  };
  #define PIX_SCOPE(cmd, name) PixScopedEvent CONCAT(_pix,__LINE__){cmd, PIX_COLOR_DEFAULT, name}
#else
  #define PIX_SCOPE(cmd, name) ((void)0)
#endif
