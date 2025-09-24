#include "TracyIntegration.h"

namespace cg::prof
{
#ifdef TRACY_ENABLE
    // TracyD3D11Ctx is an opaque type from the header; keep it TU-local.
    static TracyD3D11Ctx s_gpuCtx;

    void InitD3D11(ID3D11Device* dev, ID3D11DeviceContext* ctx)
    {
        static bool inited = false;
        if (inited) return;
        s_gpuCtx = TracyD3D11Context(dev, ctx);
        TracyGpuContextName("D3D11", 5);
        inited = true;
    }

    void CollectD3D11()
    {
        TracyD3D11Collect(s_gpuCtx);
    }

    void ShutdownD3D11()
    {
        TracyD3D11Destroy(s_gpuCtx);
    }
#else
    void InitD3D11(ID3D11Device*, ID3D11DeviceContext*) {}
    void CollectD3D11() {}
    void ShutdownD3D11() {}
#endif
}
