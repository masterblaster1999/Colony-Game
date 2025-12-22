// renderer/ShaderCompiler.cpp
//
// Implementation of the DXC-based ShaderCompiler for Colony-Game.
// Wraps IDxcUtils + IDxcCompiler3 and returns DXIL/PDB/reflection blobs.

#include "ShaderCompiler.hpp"

#include <cstring>    // std::memcpy
#include <stdexcept>

namespace colony::render
{
namespace
{
    // NOTE:
    // We *intentionally* load dxcompiler.dll dynamically instead of linking dxcompiler.lib.
    // This avoids link errors if the import lib isn't wired into CMake yet, and also means
    // the game only requires dxcompiler.dll *if* runtime compilation is used.
    using ShaderCompiler_DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

    struct ShaderCompiler_DxcModule
    {
        HMODULE                          dll    = nullptr;
        ShaderCompiler_DxcCreateInstanceProc create = nullptr;
    };

    // Keep dxcompiler.dll loaded for the lifetime of the process (avoid unload order issues).
    ShaderCompiler_DxcModule& ShaderCompiler_GetDxcModule()
    {
        static ShaderCompiler_DxcModule* mod = []() -> ShaderCompiler_DxcModule* {
            auto* m = new ShaderCompiler_DxcModule{};

            m->dll = ::LoadLibraryW(L"dxcompiler.dll");
            if (!m->dll)
            {
                throw std::runtime_error(
                    "ShaderCompiler: dxcompiler.dll not found. "
                    "Runtime shader compilation requires the DirectX Shader Compiler (DXC)."
                );
            }

            m->create = reinterpret_cast<ShaderCompiler_DxcCreateInstanceProc>(
                ::GetProcAddress(m->dll, "DxcCreateInstance")
            );
            if (!m->create)
            {
                throw std::runtime_error(
                    "ShaderCompiler: dxcompiler.dll does not export DxcCreateInstance."
                );
            }

            return m;
        }();

        return *mod;
    }

    // Helper: copy IDxcBlob contents into a std::vector<uint8_t>.
    static std::vector<std::uint8_t> ShaderCompiler_CopyBlob(IDxcBlob* blob)
    {
        std::vector<std::uint8_t> result;
        if (!blob)
            return result;

        const SIZE_T size = blob->GetBufferSize();
        if (size == 0)
            return result;

        result.resize(static_cast<size_t>(size));
        std::memcpy(result.data(), blob->GetBufferPointer(), size);
        return result;
    }
} // namespace

ShaderCompiler::ShaderCompiler()
{
    // Create IDxcUtils and IDxcCompiler3 instances via dxcompiler.dll/DxcCreateInstance.
    auto& dxc = ShaderCompiler_GetDxcModule();

    HRESULT hr = dxc.create(CLSID_DxcUtils, IID_PPV_ARGS(m_utils.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || !m_utils)
    {
        throw std::runtime_error("ShaderCompiler: failed to create IDxcUtils.");
    }

    hr = dxc.create(CLSID_DxcCompiler, IID_PPV_ARGS(m_compiler.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || !m_compiler)
    {
        throw std::runtime_error("ShaderCompiler: failed to create IDxcCompiler3.");
    }

    hr = m_utils->CreateDefaultIncludeHandler(m_includeHandler.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !m_includeHandler)
    {
        throw std::runtime_error("ShaderCompiler: failed to create DXC default include handler.");
    }
}

ShaderCompileResult ShaderCompiler::compileFromFile(
    const std::wstring& path,
    const std::wstring& entryPoint,
    const std::wstring& profile,
    const CompileOptions& options
) const
{
    ShaderCompileResult result;

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->LoadFile(path.c_str(), nullptr, sourceBlob.GetAddressOf());
    if (FAILED(hr) || !sourceBlob)
    {
        result.success      = false;
        result.errorMessage = "DXC: failed to load shader file.";
        return result;
    }

    // Respect encoding when known (otherwise let DXC treat it as ACP).
    UINT32 codePage = DXC_CP_ACP;
    BOOL   known    = FALSE;
    if (SUCCEEDED(sourceBlob->GetEncoding(&known, &codePage)) && known)
    {
        // codePage already set
    }
    else
    {
        codePage = DXC_CP_ACP;
    }

    DxcBuffer buffer{};
    buffer.Ptr      = sourceBlob->GetBufferPointer();
    buffer.Size     = sourceBlob->GetBufferSize();
    buffer.Encoding = codePage;

    return compileInternal(path, buffer, entryPoint, profile, options);
}

ShaderCompileResult ShaderCompiler::compileFromSource(
    const std::wstring& sourceName,
    std::string_view    sourceUtf8,
    const std::wstring& entryPoint,
    const std::wstring& profile,
    const CompileOptions& options
) const
{
    ShaderCompileResult result;

    if (sourceUtf8.data() == nullptr || sourceUtf8.size() == 0)
    {
        result.success      = false;
        result.errorMessage = "DXC: empty shader source.";
        return result;
    }

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->CreateBlob(
        sourceUtf8.data(),
        static_cast<UINT32>(sourceUtf8.size()),
        DXC_CP_UTF8,
        sourceBlob.GetAddressOf()
    );
    if (FAILED(hr) || !sourceBlob)
    {
        result.success      = false;
        result.errorMessage = "DXC: failed to create source blob.";
        return result;
    }

    DxcBuffer buffer{};
    buffer.Ptr      = sourceBlob->GetBufferPointer();
    buffer.Size     = sourceBlob->GetBufferSize();
    buffer.Encoding = DXC_CP_UTF8;

    return compileInternal(sourceName, buffer, entryPoint, profile, options);
}

ShaderCompileResult ShaderCompiler::compileInternal(
    const std::wstring& sourceName,
    const DxcBuffer&    sourceBuffer,
    const std::wstring& entryPoint,
    const std::wstring& profile,
    const CompileOptions& options
) const
{
    ShaderCompileResult result;

    // -------------------------------------------------------------------------
    // Build defines array
    // -------------------------------------------------------------------------
    std::vector<DxcDefine> dxcDefines;
    dxcDefines.reserve(options.defines.size());
    for (const ShaderDefine& def : options.defines)
    {
        DxcDefine d{};
        d.Name  = def.name.c_str();
        d.Value = def.value.c_str();
        dxcDefines.push_back(d);
    }

    // -------------------------------------------------------------------------
    // Build argument list
    // -------------------------------------------------------------------------
    std::vector<LPCWSTR> argPtrs;

    if (options.enableDebug)
        argPtrs.push_back(DXC_ARG_DEBUG); // -Zi

    if (options.warningsAreErrors)
        argPtrs.push_back(DXC_ARG_WARNINGS_ARE_ERRORS); // -WX

    switch (options.optimizationLevel)
    {
        case 0: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL0); break;
        case 1: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL1); break;
        case 2: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL2); break;
        case 3: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL3); break;
        default: break; // DXC default
    }

    if (options.disableValidation)
        argPtrs.push_back(DXC_ARG_SKIP_VALIDATION); // -Vd

    if (options.stripDebug)
        argPtrs.push_back(L"-Qstrip_debug");

    if (options.stripReflection)
        argPtrs.push_back(L"-Qstrip_reflect");

    for (const std::wstring& dir : options.includeDirs)
    {
        argPtrs.push_back(L"-I");
        argPtrs.push_back(dir.c_str());
    }

    for (const std::wstring& arg : options.extraArguments)
        argPtrs.push_back(arg.c_str());

    Microsoft::WRL::ComPtr<IDxcCompilerArgs> compilerArgs;
    HRESULT hr = m_utils->BuildArguments(
        sourceName.c_str(),
        entryPoint.c_str(),
        profile.c_str(),
        argPtrs.empty() ? nullptr : argPtrs.data(),
        static_cast<UINT>(argPtrs.size()),
        dxcDefines.empty() ? nullptr : dxcDefines.data(),
        static_cast<UINT>(dxcDefines.size()),
        compilerArgs.GetAddressOf()
    );
    if (FAILED(hr) || !compilerArgs)
    {
        result.success      = false;
        result.errorMessage = "DXC: BuildArguments() failed.";
        return result;
    }

    // -------------------------------------------------------------------------
    // Compile
    // -------------------------------------------------------------------------
    Microsoft::WRL::ComPtr<IDxcResult> dxResult;
    hr = m_compiler->Compile(
        &sourceBuffer,
        compilerArgs->GetArguments(),
        compilerArgs->GetCount(),
        m_includeHandler.Get(),
        IID_PPV_ARGS(dxResult.GetAddressOf())
    );
    if (FAILED(hr) || !dxResult)
    {
        result.success      = false;
        result.errorMessage = "DXC: Compile() call failed.";
        return result;
    }

    // Errors/warnings (DXC_OUT_ERRORS).
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
    if (SUCCEEDED(dxResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr)) &&
        errors && errors->GetStringLength() > 0)
    {
        result.errorMessage.assign(
            errors->GetStringPointer(),
            errors->GetStringPointer() + errors->GetStringLength()
        );
    }

    // Check compilation status.
    HRESULT status = S_OK;
    hr = dxResult->GetStatus(&status);
    if (FAILED(hr) || FAILED(status))
    {
        result.success = false;
        if (result.errorMessage.empty())
            result.errorMessage = "DXC: compilation failed (no error text).";
        return result;
    }

    // -------------------------------------------------------------------------
    // Extract DXIL object, PDB, reflection
    // -------------------------------------------------------------------------
    Microsoft::WRL::ComPtr<IDxcBlob> object;
    hr = dxResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(object.GetAddressOf()), nullptr);
    if (FAILED(hr) || !object)
    {
        result.success = false;
        if (result.errorMessage.empty())
            result.errorMessage = "DXC: failed to retrieve DXC_OUT_OBJECT.";
        return result;
    }

    result.bytecode = ShaderCompiler_CopyBlob(object.Get());

    // PDB (optional)
    Microsoft::WRL::ComPtr<IDxcBlob>     pdbBlob;
    Microsoft::WRL::ComPtr<IDxcBlobWide> pdbName;
    if (SUCCEEDED(dxResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pdbBlob.GetAddressOf()), pdbName.GetAddressOf())) &&
        pdbBlob)
    {
        result.pdb = ShaderCompiler_CopyBlob(pdbBlob.Get());
    }

    // Reflection (optional)
    Microsoft::WRL::ComPtr<IDxcBlob> reflectionBlob;
    if (SUCCEEDED(dxResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflectionBlob.GetAddressOf()), nullptr)) &&
        reflectionBlob)
    {
        result.reflection = ShaderCompiler_CopyBlob(reflectionBlob.Get());
    }

    result.success = true;
    return result;
}

} // namespace colony::render
