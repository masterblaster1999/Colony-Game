// src/runtime/Diagnostics.h
#pragma once

#ifdef _DEBUG
  #define D3D12_ENABLE_DEBUG_LAYER 1
#else
  #define D3D12_ENABLE_DEBUG_LAYER 0
#endif

#if D3D12_ENABLE_DEBUG_LAYER
  #include <d3d12.h>
  #include <dxgidebug.h>
  #include <wrl/client.h>
  inline void EnableD3D12DebugLayer()
  {
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
      debug->EnableDebugLayer();
    // Optional: GPU-based validation (heavier)
    // Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
    // if (SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
  }
#else
  inline void EnableD3D12DebugLayer() {}
#endif
