#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace render
{
    struct Define { std::string Name; std::string Value; };

    Microsoft::WRL::ComPtr<ID3DBlob>
    CompileFromFile(const std::wstring& path,
                    const char* entry,
                    const char* target,
                    const std::vector<Define>& defines = {},
                    UINT compileFlags = 0);

    Microsoft::WRL::ComPtr<ID3D11ComputeShader>
    CreateCS(ID3D11Device* device,
             const std::wstring& path,
             const char* entry,
             const std::vector<Define>& defines = {},
             UINT compileFlags = 0);
}
