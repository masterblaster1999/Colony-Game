#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include "Heightfield.hpp"
#include "ErosionCommon.hpp"

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do { if(p){ (p)->Release(); (p)=nullptr; } } while(0)
#endif

namespace colony::terrain {

class ErosionGPU {
public:
    ErosionGPU() = default;

    // You supply the D3D11 device/context (engine owns them).
    bool initialize(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::wstring& shaderDir);

    // Thermal erosion on GPU: ping-pong height & flow textures for 'iterations' passes.
    // After completion, height data is read back into the CPU heightfield.
    bool thermalErode(Heightfield& height, const ThermalParams& p);

private:
    bool compileShaders(const std::wstring& shaderDir);
    bool createResources(int w, int h, const float* heightData);
    void destroyResources();
    bool dispatchThermal(int w, int h, const ThermalParams& p, int iterations);

    Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_ctx;

    // shaders
    Microsoft::WRL::ComPtr<ID3D11ComputeShader>    m_csFlow;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader>    m_csApply;

    // resources
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_heightA;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_heightB;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_flow;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srvHeightA;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srvHeightB;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_uavHeightA;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_uavHeightB;

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_uavFlow;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_srvFlow;

    Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbFlowParams;
    Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbApplyParams;
};

} // namespace colony::terrain
