#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include "../render/Textures.h"

namespace terrain
{
    struct ErodeParams
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
        float Talus     = 0.02f;
        float Strength  = 0.5f;
        float _pad[2]   = {}; // 16-byte alignment
    };

    class ThermalErosion
    {
    public:
        ThermalErosion() = default;

        // shadersDir: e.g. L"shaders" (default). Files expected:
        //   ThermalOutflowCS.hlsl (entry: CSMain)
        //   ThermalApplyCS.hlsl   (entry: CSMain)
        void Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                        uint32_t width, uint32_t height,
                        const std::wstring& shadersDir = L"shaders");

        void Step(const ErodeParams& p, int iterations = 1);

        ID3D11ShaderResourceView* HeightSRV() const { return m_height.srv.Get(); }

    private:
        ID3D11Device*         m_dev  = nullptr;
        ID3D11DeviceContext*  m_ctx  = nullptr;
        uint32_t              m_w    = 0;
        uint32_t              m_h    = 0;

        render::Texture2D m_height;
        render::Texture2D m_temp;

        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbErode;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csOutflow;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csApply;

        void UnbindCS();
    };
}
