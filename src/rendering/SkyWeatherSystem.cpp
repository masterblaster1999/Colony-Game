#include "rendering/SkyWeatherSystem.h"
#include <cassert>
#include <random>
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace cg {

struct AtmosphereCB {
    XMFLOAT3 sunDir; float sunIntensity;
    XMFLOAT3 cameraPos; float mieG;
    XMFLOAT3 betaRayleigh; float _pad0;
    XMFLOAT3 betaMie; float _pad1;
    float planetRadius; float atmosphereRadius; XMFLOAT2 pad2;
};

struct CameraCB { XMMATRIX invViewProj; };

struct CloudGenCB {
    XMFLOAT3 volumeSize; float densityScale;
    XMFLOAT3 noiseScale; float coverage;
    float warpFreq1, warpAmp1, warpFreq2, warpAmp2;
    float perlinWeight, worleyWeight, heightSharp, heightBase;
};

struct CloudRaymarchCB {
    XMFLOAT3 cloudMin; float cloudMaxHeight;
    XMFLOAT3 cloudMax; float stepCount;
    float sigmaExt, sigmaScat, shadowStep, shadowSigma;
};

struct PrecipUpdateCB {
    XMFLOAT3 cameraPos; float dt;
    float topY, groundY;
    float spawnRadiusXZ, gravity, windStrength, time;
    float snow, pad0, pad1, pad2;
};

struct PrecipDrawCB {
    XMMATRIX viewProj;
    XMFLOAT3 camRight; float size;
    XMFLOAT3 camUp;    float opacity;
};

static void SafeReleaseUAV(ID3D11DeviceContext* ctx) {
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}

bool SkyWeatherSystem::compileShader(const std::wstring& path, const char* entry, const char* profile, ComPtr<ID3DBlob>& blobOut) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, profile, flags, 0, &blobOut, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        return false;
    }
    return true;
}

bool SkyWeatherSystem::createStates() {
    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_dev->CreateSamplerState(&s, &m_linearClamp);
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    float border[4] = {0,0,0,0};
    memcpy(s.BorderColor, border, sizeof(border));
    m_dev->CreateSamplerState(&s, &m_linearBorder);

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_dev->CreateBlendState(&bd, &m_alphaBlend);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    m_dev->CreateDepthStencilState(&dd, &m_depthDisabled);
    return true;
}

bool SkyWeatherSystem::createShaders() {
    ComPtr<ID3DBlob> vs, ps, cs;

    // Fullscreen VS
    if (!compileShader(L"renderer/Shaders/Common/FullScreenTriangleVS.hlsl","main","vs_5_0",vs)) return false;
    m_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_fullscreenVS);

    // Sky PS
    if (!compileShader(L"renderer/Shaders/Atmosphere/SkyPS.hlsl","main","ps_5_0",ps)) return false;
    m_dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_skyPS);

    // Cloud raymarch PS
    if (!compileShader(L"renderer/Shaders/Clouds/CloudRaymarchPS.hlsl","main","ps_5_0",ps)) return false;
    m_dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_cloudPS);

    // Cloud gen CS
    if (!compileShader(L"renderer/Shaders/Clouds/CloudNoiseCS.hlsl","main","cs_5_0",cs)) return false;
    m_dev->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &m_cloudGenCS);

    // Precip update CS
    if (!compileShader(L"renderer/Shaders/Weather/PrecipitationCS.hlsl","main","cs_5_0",cs)) return false;
    m_dev->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &m_precipCS);

    // Precip draw VS/PS
    if (!compileShader(L"renderer/Shaders/Weather/PrecipitationVS.hlsl","main","vs_5_0",vs)) return false;
    m_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_precipVS);
    if (!compileShader(L"renderer/Shaders/Weather/PrecipitationPS.hlsl","main","ps_5_0",ps)) return false;
    m_dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_precipPS);

    // Constant buffers
    auto makeCB = [&](UINT size, ComPtr<ID3D11Buffer>& cb) {
        D3D11_BUFFER_DESC bd{}; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.ByteWidth = (size + 15) & ~15; bd.Usage = D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(m_dev->CreateBuffer(&bd, nullptr, &cb));
    };
    makeCB(sizeof(AtmosphereCB), m_cbAtmosphere);
    makeCB(sizeof(CameraCB),     m_cbCamera);
    makeCB(sizeof(CloudGenCB),   m_cbCloudGen);
    makeCB(sizeof(CloudRaymarchCB), m_cbCloudRaymarch);
    makeCB(sizeof(PrecipUpdateCB), m_cbPrecipUpdate);
    makeCB(sizeof(PrecipDrawCB),   m_cbPrecipDraw);

    return true;
}

bool SkyWeatherSystem::createCloudVolume(const CloudParams& p) {
    D3D11_TEXTURE3D_DESC td{};
    td.Width  = p.volumeSize.x;
    td.Height = p.volumeSize.y;
    td.Depth  = p.volumeSize.z;
    td.Format = DXGI_FORMAT_R16_FLOAT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    td.MipLevels = 1;
    HRESULT hr = m_dev->CreateTexture3D(&td, nullptr, &m_cloudTex3D);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.Format = td.Format;
    uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    uavd.Texture3D.WSize = td.Depth;
    m_dev->CreateUnorderedAccessView(m_cloudTex3D.Get(), &uavd, &m_cloudUAV);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = td.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srvd.Texture3D.MipLevels = 1;
    m_dev->CreateShaderResourceView(m_cloudTex3D.Get(), &srvd, &m_cloudSRV);
    return true;
}

bool SkyWeatherSystem::createParticles(uint32_t count) {
    // Structured buffer with UAV+SRV
    struct Particle { XMFLOAT3 pos; float life; XMFLOAT3 vel; float seed; };

    std::vector<Particle> initial(count);
    std::mt19937 rng(42); std::uniform_real_distribution<float> U(0,1);
    for (auto& p : initial) {
        p.pos = XMFLOAT3(0, 50, 0);
        p.vel = XMFLOAT3(0, -12, 0);
        p.life = 6.0f * U(rng);
        p.seed = 100.0f * U(rng);
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(sizeof(Particle) * count);
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Particle);
    D3D11_SUBRESOURCE_DATA init{ initial.data(), 0, 0 };
    HRESULT hr = m_dev->CreateBuffer(&bd, &init, &m_particles);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{}; 
    uavd.Format = DXGI_FORMAT_UNKNOWN;
    uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavd.Buffer.NumElements = count;
    m_dev->CreateUnorderedAccessView(m_particles.Get(), &uavd, &m_particlesUAV);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = DXGI_FORMAT_UNKNOWN;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    srvd.BufferEx.NumElements = count;
    m_dev->CreateShaderResourceView(m_particles.Get(), &srvd, &m_particlesSRV);

    return true;
}

bool SkyWeatherSystem::init(ID3D11Device* dev, ID3D11DeviceContext* ctx, int w, int h) {
    m_dev = dev; m_ctx = ctx; m_width = w; m_height = h;
    if (!createStates()) return false;
    if (!createShaders()) return false;
    if (!createCloudVolume(m_clouds)) return false;
    if (!createParticles(12000)) return false;
    return true;
}

void SkyWeatherSystem::resize(int w, int h) { m_width = w; m_height = h; }

void SkyWeatherSystem::shutdown() {
    // ComPtrs auto-release
}

void SkyWeatherSystem::update(double timeSec, float dt,
    const XMFLOAT3& cameraPos,
    const XMMATRIX& viewProj,
    const XMMATRIX& invViewProj,
    const AtmosphereParams& atm,
    const CloudParams& clouds,
    const PrecipParams& precip)
{
    m_cameraPos = cameraPos;
    m_invViewProj = invViewProj;
    m_viewProj = viewProj;
    m_atm = atm; m_clouds = clouds; m_precip = precip;

    // 1) Generate/refresh cloud volume (can skip every N frames if needed)
    {
        D3D11_MAPPED_SUBRESOURCE ms{};
        m_ctx->Map(m_cbCloudGen.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* cb = (CloudGenCB*)ms.pData;
        cb->volumeSize = XMFLOAT3((float)clouds.volumeSize.x, (float)clouds.volumeSize.y, (float)clouds.volumeSize.z);
        cb->densityScale = clouds.densityScale;
        cb->noiseScale = clouds.noiseScale;
        cb->coverage = clouds.coverage;
        cb->warpFreq1 = clouds.warpFreq1; cb->warpAmp1 = clouds.warpAmp1;
        cb->warpFreq2 = clouds.warpFreq2; cb->warpAmp2 = clouds.warpAmp2;
        cb->perlinWeight = clouds.perlinWeight; cb->worleyWeight = clouds.worleyWeight;
        cb->heightSharp = clouds.heightSharp; cb->heightBase = clouds.heightBase;
        m_ctx->Unmap(m_cbCloudGen.Get(), 0);

        m_ctx->CSSetShader(m_cloudGenCS.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* uav = m_cloudUAV.Get();
        m_ctx->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        ID3D11Buffer* cbs[] = { m_cbCloudGen.Get() };
        m_ctx->CSSetConstantBuffers(0, 1, cbs);

        UINT gx = (clouds.volumeSize.x + 7)/8;
        UINT gy = (clouds.volumeSize.y + 7)/8;
        UINT gz = (clouds.volumeSize.z + 7)/8;
        m_ctx->Dispatch(gx, gy, gz);
        SafeReleaseUAV(m_ctx); // unbind before SRV use
    }

    // 2) Update particles
    {
        D3D11_MAPPED_SUBRESOURCE ms{};
        m_ctx->Map(m_cbPrecipUpdate.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* cb = (PrecipUpdateCB*)ms.pData;
        cb->cameraPos = cameraPos;
        cb->dt = dt;
        cb->topY = precip.topY; cb->groundY = precip.groundY;
        cb->spawnRadiusXZ = precip.spawnRadiusXZ; cb->gravity = precip.gravity; cb->windStrength = precip.windStrength; cb->time = (float)timeSec;
        cb->snow = precip.snow ? 1.0f : 0.0f;
        m_ctx->Unmap(m_cbPrecipUpdate.Get(), 0);

        m_ctx->CSSetShader(m_precipCS.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* uav = m_particlesUAV.Get();
        m_ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ID3D11Buffer* cbs[] = { m_cbPrecipUpdate.Get() };
        m_ctx->CSSetConstantBuffers(0, 1, cbs);
        UINT groups = (m_precip.particleCount + 255) / 256;
        m_ctx->Dispatch(groups, 1, 1);
        SafeReleaseUAV(m_ctx);
    }

    // Update shared cbuffers
    {
        // Atmosphere
        D3D11_MAPPED_SUBRESOURCE ms{};
        m_ctx->Map(m_cbAtmosphere.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* cb = (AtmosphereCB*)ms.pData;
        cb->sunDir = atm.sunDir; cb->sunIntensity = atm.sunIntensity;
        cb->cameraPos = cameraPos; cb->mieG = atm.mieG;
        cb->betaRayleigh = atm.betaRayleigh; cb->betaMie = atm.betaMie;
        cb->planetRadius = atm.planetRadius; cb->atmosphereRadius = atm.atmosphereRadius;
        m_ctx->Unmap(m_cbAtmosphere.Get(), 0);

        // Camera
        m_ctx->Map(m_cbCamera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* cc = (CameraCB*)ms.pData;
        cc->invViewProj = XMMatrixTranspose(invViewProj);
        m_ctx->Unmap(m_cbCamera.Get(), 0);

        // Cloud raymarch
        m_ctx->Map(m_cbCloudRaymarch.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* cr = (CloudRaymarchCB*)ms.pData;
        cr->cloudMin = clouds.worldMin;  cr->cloudMaxHeight = clouds.worldMaxY;
        cr->cloudMax = clouds.worldMax;  cr->stepCount = clouds.stepCount;
        cr->sigmaExt = clouds.sigmaExt;  cr->sigmaScat = clouds.sigmaScat;
        cr->shadowStep = clouds.shadowStep; cr->shadowSigma = clouds.shadowSigma;
        m_ctx->Unmap(m_cbCloudRaymarch.Get(), 0);
    }
}

void SkyWeatherSystem::renderSky(ID3D11RenderTargetView* rtv) {
    float blendFactor[4] = {0,0,0,0};
    m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    m_ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    m_ctx->OMSetDepthStencilState(m_depthDisabled.Get(), 0);

    m_ctx->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
    m_ctx->PSSetShader(m_skyPS.Get(), nullptr, 0);

    ID3D11Buffer* cbsPS[] = { m_cbAtmosphere.Get(), m_cbCamera.Get() };
    m_ctx->PSSetConstantBuffers(0, 2, cbsPS);

    ID3D11SamplerState* samps[] = { m_linearClamp.Get() };
    m_ctx->PSSetSamplers(0, 1, samps);

    m_ctx->IASetInputLayout(nullptr);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->Draw(3, 0);
}

void SkyWeatherSystem::renderClouds(ID3D11RenderTargetView* rtv) {
    float blendFactor[4] = {0,0,0,0};
    m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    m_ctx->OMSetBlendState(m_alphaBlend.Get(), blendFactor, 0xffffffff);
    m_ctx->OMSetDepthStencilState(m_depthDisabled.Get(), 0);

    m_ctx->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
    m_ctx->PSSetShader(m_cloudPS.Get(), nullptr, 0);

    ID3D11Buffer* cbsPS[] = { m_cbAtmosphere.Get(), m_cbCamera.Get(), m_cbCloudRaymarch.Get() };
    m_ctx->PSSetConstantBuffers(0, 3, cbsPS);

    ID3D11SamplerState* samps[] = { m_linearBorder.Get() };
    m_ctx->PSSetSamplers(0, 1, samps);

    ID3D11ShaderResourceView* srvs[] = { m_cloudSRV.Get() };
    m_ctx->PSSetShaderResources(0, 1, srvs);

    m_ctx->IASetInputLayout(nullptr);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    m_ctx->PSSetShaderResources(0, 1, nullSRV);
}

void SkyWeatherSystem::renderPrecipitation(ID3D11RenderTargetView* rtv,
    const XMFLOAT3& camRight, const XMFLOAT3& camUp, const XMMATRIX& viewProj)
{
    float blendFactor[4] = {0,0,0,0};
    m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    m_ctx->OMSetBlendState(m_alphaBlend.Get(), blendFactor, 0xffffffff);

    // Draw constants
    D3D11_MAPPED_SUBRESOURCE ms{};
    m_ctx->Map(m_cbPrecipDraw.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    auto* cd = (PrecipDrawCB*)ms.pData;
    cd->viewProj = XMMatrixTranspose(viewProj);
    cd->camRight = camRight; cd->camUp = camUp;
    cd->size = m_precip.size; cd->opacity = m_precip.opacity;
    m_ctx->Unmap(m_cbPrecipDraw.Get(), 0);

    m_ctx->VSSetShader(m_precipVS.Get(), nullptr, 0);
    m_ctx->PSSetShader(m_precipPS.Get(), nullptr, 0);

    ID3D11Buffer* cbVS[] = { m_cbPrecipDraw.Get() };
    m_ctx->VSSetConstantBuffers(0, 1, cbVS);

    ID3D11ShaderResourceView* srvs[] = { m_particlesSRV.Get() };
    m_ctx->VSSetShaderResources(0, 1, srvs);

    ID3D11SamplerState* samps[] = { m_linearClamp.Get() };
    m_ctx->PSSetSamplers(0, 1, samps);

    m_ctx->IASetInputLayout(nullptr);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_ctx->DrawInstanced(4, m_precip.particleCount, 0, 0);

    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    m_ctx->VSSetShaderResources(0, 1, nullSRV);
}

} // namespace cg
