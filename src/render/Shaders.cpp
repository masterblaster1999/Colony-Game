#include "Shaders.h"
#include "HrCheck.h"

using Microsoft::WRL::ComPtr;

namespace render
{
    static std::vector<D3D_SHADER_MACRO> BuildMacros(const std::vector<Define>& defs)
    {
        std::vector<D3D_SHADER_MACRO> macros;
        macros.reserve(defs.size() + 1);
        for (const auto& d : defs)
            macros.push_back(D3D_SHADER_MACRO{ d.Name.c_str(), d.Value.c_str() });
        macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });
        return macros;
    }

    ComPtr<ID3DBlob> CompileFromFile(const std::wstring& path,
                                     const char* entry,
                                     const char* target,
                                     const std::vector<Define>& defines,
                                     UINT compileFlags)
    {
#if defined(_DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        ComPtr<ID3DBlob> bytecode, errors;
        auto macros = BuildMacros(defines);
        HRESULT hr = D3DCompileFromFile(path.c_str(), macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                        entry, target, compileFlags, 0,
                                        bytecode.ReleaseAndGetAddressOf(),
                                        errors.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            const char* msg = errors ? (const char*)errors->GetBufferPointer() : "Unknown shader compile error";
            throw std::runtime_error(std::string("Shader compile failed: ") + msg);
        }
        return bytecode;
    }

    ComPtr<ID3D11ComputeShader> CreateCS(ID3D11Device* device,
                                         const std::wstring& path,
                                         const char* entry,
                                         const std::vector<Define>& defines,
                                         UINT compileFlags)
    {
        auto bc = CompileFromFile(path, entry, "cs_5_0", defines, compileFlags);
        ComPtr<ID3D11ComputeShader> cs;
        HR_CHECK(device->CreateComputeShader(bc->GetBufferPointer(), bc->GetBufferSize(),
                                             nullptr, cs.ReleaseAndGetAddressOf()));
        return cs;
    }
}
