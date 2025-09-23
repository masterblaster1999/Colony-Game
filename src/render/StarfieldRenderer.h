#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>

namespace colony::render {

class StarfieldRenderer {
public:
    StarfieldRenderer() = default;
    ~StarfieldRenderer() = default;

    // Returns false on failure
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // Update viewport size (inv size goes to the shader)
    void OnResize(uint32_t width, uint32_t height);

    // Render the starfield. Call with an immediate context.
    // 'timeSeconds' should be a monotonic time; 'density' around 1.0..3.0.
    void Render(ID3D11DeviceContext* ctx, float timeSeconds, float density = 1.0f);

private:
    struct StarCB {
        float invRes[2]; // 1/width, 1/height
        float time;
        float density;
        // pad to 16B multiple automatically
    };

    bool loadShaderBlob(const wchar_t* path, ID3DBlob** blobOut);

    Microsoft::WRL::ComPtr<ID3D11VertexShader>   m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer>         m_cb;           // b0
    Microsoft::WRL::ComPtr<ID3D11BlendState>     m_blendAdd;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_dssDisabled;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_rsCullNone;

    uint32_t m_width  = 1;
    uint32_t m_height = 1;
};

} // namespace colony::render
