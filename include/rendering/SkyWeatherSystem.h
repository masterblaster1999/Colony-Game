#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace cg {

// -----------------------------------------------------------------------------
// Parameter structures
// -----------------------------------------------------------------------------
struct AtmosphereParams {
    DirectX::XMFLOAT3 sunDir = { 0.3f, 0.7f, 0.6f };
    float             sunIntensity = 20.0f;
    DirectX::XMFLOAT3 betaRayleigh = { 5.5e-6f, 13.0e-6f, 22.4e-6f };
    float             mieG = 0.8f;
    DirectX::XMFLOAT3 betaMie = { 2.0e-5f, 2.0e-5f, 2.0e-5f };
    float             planetRadius = 6371000.0f;
    float             atmosphereRadius = 6471000.0f;
};

struct CloudParams {
    DirectX::XMUINT3 volumeSize = {128, 64, 128};
    float densityScale = 1.0f;
    DirectX::XMFLOAT3 noiseScale = {0.006f, 0.012f, 0.006f};
    float coverage = 0.45f;
    float warpFreq1 = 2.0f, warpAmp1 = 0.75f, warpFreq2 = 6.0f, warpAmp2 = 0.25f;
    float perlinWeight = 0.65f, worleyWeight = 0.35f, heightSharp = 4.5f, heightBase = 0.55f;
    DirectX::XMFLOAT3 worldMin = {-1000.f, 1000.f, -1000.f};
    float worldMaxY = 2500.f;
    DirectX::XMFLOAT3 worldMax = { 1000.f, 0.f, 1000.f };
    float stepCount = 64.f;
    float sigmaExt = 2.0f, sigmaScat = 1.5f, shadowStep = 50.f, shadowSigma = 4.0f;
};

struct PrecipParams {
    bool  snow = false;
    float topY = 120.0f;
    float groundY = 0.0f;
    float spawnRadiusXZ = 45.0f;
    float gravity = 9.8f;
    float windStrength = 6.0f;
    float size = 0.08f;  // raindrop billboard half-size
    float opacity = 0.7f;
    uint32_t particleCount = 12000;
};

// -----------------------------------------------------------------------------
// SkyWeatherSystem
// -----------------------------------------------------------------------------
class SkyWeatherSystem {
public:
    bool init(ID3D11Device* dev, ID3D11DeviceContext* ctx, int backbufferW, int backbufferH);
    void resize(int w, int h);
    void shutdown();

    void update(double timeSec, float dt,
                const DirectX::XMFLOAT3& cameraPos,
                const DirectX::XMMATRIX& viewProj,
                const DirectX::XMMATRIX& invViewProj,
                const AtmosphereParams& atm,
                const CloudParams& clouds,
                const PrecipParams& precip);

    // Render order: sky (behind everything), then clouds (alpha), then precipitation (alpha)
    void renderSky(ID3D11RenderTargetView* rtv);
    void renderClouds(ID3D11RenderTargetView* rtv);
    void renderPrecipitation(ID3D11RenderTargetView* rtv,
                             const DirectX::XMFLOAT3& camRight,
                             const DirectX::XMFLOAT3& camUp,
                             const DirectX::XMMATRIX& viewProj);

private:
    ID3D11Device*        m_dev  = nullptr;
    ID3D11DeviceContext* m_ctx  = nullptr;
    int m_width  = 0;
    int m_height = 0;

    // Shaders
    Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_fullscreenVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_skyPS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_cloudPS;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_cloudGenCS;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_precipCS;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_precipVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_precipPS;

    // States
    Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_linearClamp;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_linearBorder;
    Microsoft::WRL::ComPtr<ID3D11BlendState>        m_alphaBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthDisabled;

    // Constant buffers
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbAtmosphere;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbCamera;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbCloudGen;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbCloudRaymarch;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbPrecipUpdate;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbPrecipDraw;

    // Cloud volume
    Microsoft::WRL::ComPtr<ID3D11Texture3D>           m_cloudTex3D;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_cloudUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_cloudSRV;

    // Particle buffer
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_particles; // structured buffer
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_particlesUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_particlesSRV;

    // Cached parameters
    AtmosphereParams m_atm{};
    CloudParams      m_clouds{};
    PrecipParams     m_precip{};
    DirectX::XMFLOAT3 m_cameraPos{};
    DirectX::XMMATRIX m_invViewProj{};
    DirectX::XMMATRIX m_viewProj{};

    // Helpers
    bool compileShader(const std::wstring& path,
                       const char* entry,
                       const char* profile,
                       Microsoft::WRL::ComPtr<ID3DBlob>& blobOut);
    bool createShaders();
    bool createStates();
    bool createCloudVolume(const CloudParams& params);
    bool createParticles(uint32_t count);
};

} // namespace cg
