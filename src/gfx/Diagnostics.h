// Diagnostics.h (D3D12 debug + GPU-based validation)
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace gfx
{
    inline void EnableD3D12DebugLayer(bool enableGpuValidation)
    {
    #if defined(_DEBUG)
        using Microsoft::WRL::ComPtr;

        // Enable standard D3D12 debug layer.
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();

            // Optional: GPU‑based validation – more expensive but great when debugging.
            if (enableGpuValidation)
            {
                ComPtr<ID3D12Debug1> debug1;
                if (SUCCEEDED(debug.As(&debug1)))
                {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                }
            }
        }
    #else
        // In release builds we don't actually use the parameter.
        (void)enableGpuValidation; // prevent C4100
    #endif
    }
}
