// renderer/ShaderCompiler.cpp
//
// Implementation of the DXC-based ShaderCompiler for Colony-Game.
// Wraps IDxcUtils + IDxcCompiler3 and returns DXIL/PDB/reflection blobs.

#include "ShaderCompiler.hpp"

#include <cassert>
#include <cstring>    // std::memcpy
#include <stdexcept>

namespace colony::render
{

using ComPtr = Microsoft::WRL::ComPtr;

// Helper: copy IDxcBlob contents into a std::vector<uint8_t>.
static std::vector<std::uint8_t> CopyBlob(IDxcBlob* blob)
{
    std::vector<std::uint8_t> result;
    if (!blob)
        return result;

    const SIZE_T size = blob->GetBufferSize();
    if (size == 0)
        return result;

    result.resize(size);
    std::memcpy(result.data(), blob->GetBufferPointer(), size);
    return result;
}

ShaderCompiler::ShaderCompiler()
{
    // Create IDxcUtils and IDxcCompiler3 instances via DxcCreateInstance. :contentReference[oaicite:2]{index=2}
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(m_utils.GetAddressOf()));
    if (FAILED(hr) || !m_utils) {
        throw std::runtime_error("ShaderCompiler: failed to create IDxcUtils (dxcompiler.dll missing?).");
    }

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(m_compiler.GetAddressOf()));
    if (FAILED(hr) || !m_compiler) {
        throw std::runtime_error("ShaderCompiler: failed to create IDxcCompiler3.");
    }

    hr = m_utils->CreateDefaultIncludeHandler(m_includeHandler.GetAddressOf());
    if (FAILED(hr) || !m_includeHandler) {
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

    ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->LoadFile(path.c_str(), nullptr, sourceBlob.GetAddressOf());
    if (FAILED(hr) || !sourceBlob) {
        result.success      = false;
        result.errorMessage = "DXC: failed to load shader file.";
        return result;
    }

    DxcBuffer buffer{};
    buffer.Ptr      = sourceBlob->GetBufferPointer();
    buffer.Size     = sourceBlob->GetBufferSize();
    buffer.Encoding = DXC_CP_ACP; // Let DXC/autodetect handle encoding; HLSL is usually ASCII/UTF-8. :contentReference[oaicite:3]{index=3}

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

    if (sourceUtf8.data() == nullptr || sourceUtf8.size() == 0) {
        result.success      = false;
        result.errorMessage = "DXC: empty shader source.";
        return result;
    }

    ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->CreateBlob(
        sourceUtf8.data(),
        static_cast<UINT32>(sourceUtf8.size()),
        DXC_CP_UTF8,
        sourceBlob.GetAddressOf()
    );
    if (FAILED(hr) || !sourceBlob) {
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
    for (const ShaderDefine& def : options.defines) {
        DxcDefine d{};
        d.Name  = def.name.c_str();
        d.Value = def.value.c_str();
        dxcDefines.push_back(d);
    }

    // -------------------------------------------------------------------------
    // Build argument list (similar to Simon Coenen's pattern, but via BuildArguments). :contentReference[oaicite:4]{index=4}
    // -------------------------------------------------------------------------
    std::vector<LPCWSTR> argPtrs;

    if (options.enableDebug) {
        // -Zi
        argPtrs.push_back(DXC_ARG_DEBUG);
    }
    if (options.warningsAreErrors) {
        // -WX
        argPtrs.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
    }

    // -O0..-O3
    switch (options.optimizationLevel) {
    case 0: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL0); break;
    case 1: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL1); break;
    case 2: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL2); break;
    case 3: argPtrs.push_back(DXC_ARG_OPTIMIZATION_LEVEL3); break;
    default: break; // use DXC default (O3)
    }

    if (options.disableValidation) {
        // -Vd
        argPtrs.push_back(DXC_ARG_SKIP_VALIDATION);
    }

    if (options.stripDebug) {
        // Separate PDB via DXC_OUT_PDB, keep object slim. :contentReference[oaicite:5]{index=5}
        argPtrs.push_back(L"-Qstrip_debug");
    }
    if (options.stripReflection) {
        argPtrs.push_back(L"-Qstrip_reflect");
    }

    // -I <dir> paths
    for (const std::wstring& dir : options.includeDirs) {
        argPtrs.push_back(L"-I");
        argPtrs.push_back(dir.c_str());
    }

    // Extra raw args
    for (const std::wstring& arg : options.extraArguments) {
        argPtrs.push_back(arg.c_str());
    }

    ComPtr<IDxcCompilerArgs> compilerArgs;
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
    if (FAILED(hr) || !compilerArgs) {
        result.success      = false;
        result.errorMessage = "DXC: BuildArguments() failed.";
        return result;
    }

    // -------------------------------------------------------------------------
    // Compile
    // -------------------------------------------------------------------------
    ComPtr<IDxcResult> dxResult;
    hr = m_compiler->Compile(
        &sourceBuffer,
        compilerArgs->GetArguments(),
        compilerArgs->GetCount(),
        m_includeHandler.Get(),
        IID_PPV_ARGS(dxResult.GetAddressOf())
    );

    if (FAILED(hr) || !dxResult) {
        result.success      = false;
        result.errorMessage = "DXC: Compile() call failed.";
        return result;
    }

    // Errors/warnings (DXC_OUT_ERRORS). This includes warnings unless disabled. :contentReference[oaicite:6]{index=6}
    ComPtr<IDxcBlobUtf8> errors;
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
    if (FAILED(hr) || FAILED(status)) {
        result.success = false;
        if (result.errorMessage.empty()) {
            result.errorMessage = "DXC: compilation failed (no error text).";
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Extract DXIL object, PDB, reflection
    // -------------------------------------------------------------------------
    ComPtr<IDxcBlob> object;
    hr = dxResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(object.GetAddressOf()), nullptr);
    if (FAILED(hr) || !object) {
        result.success      = false;
        if (result.errorMessage.empty()) {
            result.errorMessage = "DXC: failed to retrieve DXC_OUT_OBJECT.";
        }
        return result;
    }

    result.bytecode = CopyBlob(object.Get());

    // PDB (optional)
    ComPtr<IDxcBlob> pdbBlob;
    ComPtr<IDxcBlobWide> pdbName;
    if (SUCCEEDED(dxResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pdbBlob.GetAddressOf()), pdbName.GetAddressOf())) &&
        pdbBlob)
    {
        result.pdb = CopyBlob(pdbBlob.Get());
    }

    // Reflection (optional)
    ComPtr<IDxcBlob> reflectionBlob;
    if (SUCCEEDED(dxResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflectionBlob.GetAddressOf()), nullptr)) &&
        reflectionBlob)
    {
        result.reflection = CopyBlob(reflectionBlob.Get());
    }

    result.success = true;
    return result;
}

} // namespace colony::render
