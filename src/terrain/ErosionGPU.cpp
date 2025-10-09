#include "ErosionGPU.hpp"
#include <d3dcompiler.h>
#include <vector>
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace colony::terrain {

struct CBFlow {
    float talus;
    float carry;
    int   width;
    int   height;
};
struct CBApply {
    int width;
    int height;
    float pad[2];
};

static HRESULT compileCS(const std::wstring& path, const char* entry, ID3DBlob** bytecode)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    Microsoft::WRL::ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, "cs_5_0", flags, 0, bytecode, err.GetAddressOf());
    if (FAILED(hr) && err) OutputDebugStringA((const char*)err->GetBufferPointer());
    return hr;
}

bool ErosionGPU::initialize(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::wstring& shaderDir)
{
    if (!device || !ctx) return false;
    m_device = device; m_ctx = ctx;
    return compileShaders(shaderDir);
}

bool ErosionGPU::compileShaders(const std::wstring& dir)
{
    Microsoft::WRL::ComPtr<ID3DBlob> bc;

    // Flow compute
    if (FAILED(compileCS(dir + L"\\erosion_thermal_flow_cs.hlsl", "CSMain", bc.GetAddressOf())))
        return false;
    if (FAILED(m_device->CreateComputeShader(bc->GetBufferPointer(), bc->GetBufferSize(), nullptr, m_csFlow.GetAddressOf())))
        return false;
    bc.Reset();

    // Apply compute
    if (FAILED(compileCS(dir + L"\\erosion_thermal_apply_cs.hlsl", "CSMain", bc.GetAddressOf())))
        return false;
    if (FAILED(m_device->CreateComputeShader(bc->GetBufferPointer(), bc->GetBufferSize(), nullptr, m_csApply.GetAddressOf())))
        return false;

    // Constant buffers
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(CBFlow);
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_cbFlowParams.GetAddressOf()))) return false;
    bd.ByteWidth = sizeof(CBApply);
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_cbApplyParams.GetAddressOf()))) return false;
    return true;
}

bool ErosionGPU::createResources(int w, int h, const float* heightData)
{
    destroyResources();

    D3D11_TEXTURE2D_DESC td{};
    td.Width  = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = heightData;
    sd.SysMemPitch = UINT(sizeof(float) * w);

    if (FAILED(m_device->CreateTexture2D(&td, &sd, m_heightA.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateTexture2D(&td, &sd, m_heightB.GetAddressOf()))) return false;

    // Flow texture RGBA32F
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    sd.pSysMem = nullptr; sd.SysMemPitch = 0;
    if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_flow.GetAddressOf()))) return false;

    // SRVs/UAVs
    if (FAILED(m_device->CreateShaderResourceView(m_heightA.Get(), nullptr, m_srvHeightA.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateShaderResourceView(m_heightB.Get(), nullptr, m_srvHeightB.GetAddressOf()))) return false;

    if (FAILED(m_device->CreateUnorderedAccessView(m_heightA.Get(), nullptr, m_uavHeightA.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateUnorderedAccessView(m_heightB.Get(), nullptr, m_uavHeightB.GetAddressOf()))) return false;

    if (FAILED(m_device->CreateUnorderedAccessView(m_flow.Get(), nullptr, m_uavFlow.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateShaderResourceView(m_flow.Get(), nullptr, m_srvFlow.GetAddressOf()))) return false;

    return true;
}

void ErosionGPU::destroyResources()
{
    m_srvHeightA.Reset(); m_srvHeightB.Reset();
    m_uavHeightA.Reset(); m_uavHeightB.Reset();
    m_uavFlow.Reset();    m_srvFlow.Reset();
    m_heightA.Reset();    m_heightB.Reset(); m_flow.Reset();
}

bool ErosionGPU::dispatchThermal(int w, int h, const ThermalParams& p, int iterations)
{
    const UINT gx = (UINT)((w + 15) / 16);
    const UINT gy = (UINT)((h + 15) / 16);

    for (int it=0; it<iterations; ++it)
    {
        // --- Pass 1: compute flows from current height (srvHeightA) -> flow (uavFlow)
        {
            D3D11_MAPPED_SUBRESOURCE ms{};
            if (SUCCEEDED(m_ctx->Map(m_cbFlowParams.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                auto* cb = reinterpret_cast<CBFlow*>(ms.pData);
                cb->talus  = p.talus;
                cb->carry  = p.carry;
                cb->width  = w;
                cb->height = h;
                m_ctx->Unmap(m_cbFlowParams.Get(), 0);
            }
            ID3D11UnorderedAccessView* uavs[] = { m_uavFlow.Get() };
            ID3D11ShaderResourceView*  srvs[] = { m_srvHeightA.Get() };

            m_ctx->CSSetShader(m_csFlow.Get(), nullptr, 0);
            m_ctx->CSSetConstantBuffers(0, 1, m_cbFlowParams.GetAddressOf());
            m_ctx->CSSetShaderResources(0, 1, srvs);
            m_ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            m_ctx->Dispatch(gx, gy, 1);

            // Unbind UAV/SRV
            ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
            ID3D11ShaderResourceView*  nullSRV[1] = { nullptr };
            m_ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
            m_ctx->CSSetShaderResources(0, 1, nullSRV);
        }

        // --- Pass 2: apply flows: heightA + flow -> heightB
        {
            D3D11_MAPPED_SUBRESOURCE ms{};
            if (SUCCEEDED(m_ctx->Map(m_cbApplyParams.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                auto* cb = reinterpret_cast<CBApply*>(ms.pData);
                cb->width  = w;
                cb->height = h;
                m_ctx->Unmap(m_cbApplyParams.Get(), 0);
            }
            ID3D11UnorderedAccessView* uavs[] = { m_uavHeightB.Get() };
            ID3D11ShaderResourceView*  srvs[] = { m_srvHeightA.Get(), m_srvFlow.Get() };

            m_ctx->CSSetShader(m_csApply.Get(), nullptr, 0);
            m_ctx->CSSetConstantBuffers(0, 1, m_cbApplyParams.GetAddressOf());
            m_ctx->CSSetShaderResources(0, 2, srvs);
            m_ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            m_ctx->Dispatch((UINT)((w + 15)/16), (UINT)((h + 15)/16), 1);

            ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
            ID3D11ShaderResourceView*  nullSRV[2] = { nullptr, nullptr };
            m_ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
            m_ctx->CSSetShaderResources(0, 2, nullSRV);
        }

        // ping-pong height
        std::swap(m_heightA, m_heightB);
        std::swap(m_srvHeightA, m_srvHeightB);
        std::swap(m_uavHeightA, m_uavHeightB);
    }
    return true;
}

bool ErosionGPU::thermalErode(Heightfield& height, const ThermalParams& p)
{
    const int w = height.width();
    const int h = height.height();
    if (!createResources(w, h, height.data())) return false;

    if (!dispatchThermal(w, h, p, p.iterations)) { destroyResources(); return false; }

    // Readback result from current "A".
    D3D11_TEXTURE2D_DESC td{};
    m_heightA->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&td, nullptr, staging.GetAddressOf()))) { destroyResources(); return false; }

    m_ctx->CopyResource(staging.Get(), m_heightA.Get());

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(m_ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) { destroyResources(); return false; }

    // copy rows (pitch can differ)
    for (int y=0;y<h;++y) {
        const float* src = reinterpret_cast<const float*>((const uint8_t*)ms.pData + y*ms.RowPitch);
        std::memcpy(height.data() + y*w, src, sizeof(float)*w);
    }
    m_ctx->Unmap(staging.Get(), 0);

    destroyResources();
    return true;
}

} // namespace colony::terrain
