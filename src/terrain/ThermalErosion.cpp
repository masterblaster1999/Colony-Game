#include "ThermalErosion.h"
#include "../render/Textures.h"
#include "../render/Shaders.h"
#include "../render/HrCheck.h"
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace terrain
{
    void ThermalErosion::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                    uint32_t width, uint32_t height,
                                    const std::wstring& shadersDir)
    {
        m_dev = dev; m_ctx = ctx; m_w = width; m_h = height;

        // resources
        m_height = render::CreateRWTexture2D(m_dev, width, height, DXGI_FORMAT_R32_FLOAT);
        m_temp   = render::CreateRWTexture2D(m_dev, width, height, DXGI_FORMAT_R32_FLOAT);

        m_cbErode = render::CreateConstantBuffer(m_dev, sizeof(ErodeParams));

        // shaders
        std::wstring outflow = shadersDir + L"/ThermalOutflowCS.hlsl";
        std::wstring apply   = shadersDir + L"/ThermalApplyCS.hlsl";

        m_csOutflow = render::CreateCS(m_dev, outflow, "CSMain");
        m_csApply   = render::CreateCS(m_dev, apply, "CSMain");
    }

    void ThermalErosion::UnbindCS()
    {
        ID3D11UnorderedAccessView* uavnulls[8] = {};
        ID3D11ShaderResourceView*  srvnulls[8] = {};
        m_ctx->CSSetUnorderedAccessViews(0, 8, uavnulls, nullptr);
        m_ctx->CSSetShaderResources(0, 8, srvnulls);
        m_ctx->CSSetShader(nullptr, nullptr, 0);
    }

    void ThermalErosion::Step(const ErodeParams& p, int iterations)
    {
        const UINT tgx = (m_w + 15u) / 16u;
        const UINT tgy = (m_h + 15u) / 16u;

        for (int i = 0; i < iterations; ++i)
        {
            // Update constants (Width/Height must match current textures)
            ErodeParams c = p;
            c.Width = m_w; c.Height = m_h;
            m_ctx->UpdateSubresource(m_cbErode.Get(), 0, nullptr, &c, 0, 0);

            // Pass 1: Outflow (height -> temp)
            ID3D11UnorderedAccessView* uav0 = m_temp.uav.Get();
            ID3D11ShaderResourceView*  srv0 = m_height.srv.Get();

            m_ctx->CSSetShader(m_csOutflow.Get(), nullptr, 0);
            m_ctx->CSSetConstantBuffers(0, 1, m_cbErode.GetAddressOf());
            m_ctx->CSSetShaderResources(0, 1, &srv0);
            m_ctx->CSSetUnorderedAccessViews(0, 1, &uav0, nullptr);
            m_ctx->Dispatch(tgx, tgy, 1);
            UnbindCS();

            // Pass 2: Apply (temp -> height)
            uav0 = m_height.uav.Get();
            srv0 = m_temp.srv.Get();

            m_ctx->CSSetShader(m_csApply.Get(), nullptr, 0);
            m_ctx->CSSetConstantBuffers(0, 1, m_cbErode.GetAddressOf());
            m_ctx->CSSetShaderResources(0, 1, &srv0);
            m_ctx->CSSetUnorderedAccessViews(0, 1, &uav0, nullptr);
            m_ctx->Dispatch(tgx, tgy, 1);
            UnbindCS();
        }
    }
}
