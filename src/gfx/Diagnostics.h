// src/gfx/Diagnostics.h
#pragma once

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <wrl.h>
#include <d3d12.h>
#include <dxgidebug.h>
#include <d3d12sdklayers.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

inline void EnableD3D12DebugLayer(bool enableGpuValidation)
{
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer(); // Must be BEFORE device creation
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
        if (enableGpuValidation && SUCCEEDED(debug.As(&debug1)))
        {
            // Optional, dev-only: slows down a lot but catches more issues
            debug1->SetEnableGPUBasedValidation(TRUE);
        }
    }
#endif
}
