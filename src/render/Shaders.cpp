#include "Shaders.h"
#include "HrCheck.h"

#include <d3dcompiler.h>
#include <filesystem>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>

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

    // Case-insensitive wide "ends with".
    static bool EndsWithI(const std::wstring& s, const std::wstring& suffix)
    {
        if (s.size() < suffix.size())
            return false;

        const size_t off = s.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i)
        {
            if (towlower(s[off + i]) != towlower(suffix[i]))
                return false;
        }
        return true;
    }

    static bool IsPrecompiledBlobPath(const std::wstring& path)
    {
        return EndsWithI(path, L".cso") || EndsWithI(path, L".dxbc");
    }

    static bool TryLoadBlobFromFile(const std::wstring& path, ComPtr<ID3DBlob>& outBlob)
    {
        ComPtr<ID3DBlob> blob;
        const HRESULT hr = D3DReadFileToBlob(path.c_str(), blob.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            return false;

        outBlob = blob;
        return true;
    }

    // Tiny helper to avoid pulling in Windows conversion APIs just for error strings.
    // (Shader paths here are typically ASCII; non-ASCII will become '?'.)
    static std::string WStringToNarrowLossy(const std::wstring& ws)
    {
        std::string out;
        out.reserve(ws.size());
        for (wchar_t c : ws)
            out.push_back((c <= 0x7f) ? static_cast<char>(c) : '?');
        return out;
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

    // Prefer precompiled blobs (.cso/.dxbc) when available; compile only if needed.
    static ComPtr<ID3DBlob> LoadPrecompiledOrCompile(const std::wstring& path,
                                                     const char* entry,
                                                     const char* target,
                                                     const std::vector<Define>& defines,
                                                     UINT compileFlags)
    {
        // If the caller explicitly passed a blob path, load it directly.
        if (IsPrecompiledBlobPath(path))
        {
            ComPtr<ID3DBlob> blob;
            if (!TryLoadBlobFromFile(path, blob))
            {
                throw std::runtime_error(
                    std::string("Failed to load shader blob: ") + WStringToNarrowLossy(path)
                );
            }
            return blob;
        }

        // If there are defines, we cannot safely auto-pick a single sibling .cso because
        // different define sets typically require different compiled outputs.
        // In that case, compile from source unless the caller explicitly passed a .cso/.dxbc.
        if (!defines.empty())
            return CompileFromFile(path, entry, target, defines, compileFlags);

        // Otherwise, try a sibling compiled blob first: Foo.hlsl -> Foo.cso (then Foo.dxbc).
        const std::filesystem::path srcPath(path);

        {
            std::filesystem::path csoPath = srcPath;
            csoPath.replace_extension(L".cso");

            ComPtr<ID3DBlob> blob;
            if (TryLoadBlobFromFile(csoPath.wstring(), blob))
                return blob;
        }

        {
            std::filesystem::path dxbcPath = srcPath;
            dxbcPath.replace_extension(L".dxbc");

            ComPtr<ID3DBlob> blob;
            if (TryLoadBlobFromFile(dxbcPath.wstring(), blob))
                return blob;
        }

        // No precompiled blob available -> compile from source.
        return CompileFromFile(path, entry, target, defines, compileFlags);
    }

    ComPtr<ID3D11ComputeShader> CreateCS(ID3D11Device* device,
                                         const std::wstring& path,
                                         const char* entry,
                                         const std::vector<Define>& defines,
                                         UINT compileFlags)
    {
        auto bc = LoadPrecompiledOrCompile(path, entry, "cs_5_0", defines, compileFlags);
        ComPtr<ID3D11ComputeShader> cs;
        HR_CHECK(device->CreateComputeShader(bc->GetBufferPointer(), bc->GetBufferSize(),
                                             nullptr, cs.ReleaseAndGetAddressOf()));
        return cs;
    }
}
