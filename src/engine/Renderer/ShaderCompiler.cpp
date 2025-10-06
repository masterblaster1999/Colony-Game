#include "ShaderCompiler.hpp"

#include <cassert>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace colony::gfx {

static std::wstring ToWide(const std::filesystem::path& p) {
    return p.wstring();
}

ShaderCompiler::ShaderCompiler() {
    // Create Utils + Compiler + Default Include Handler.
    // DxcCreateInstance is the documented way to obtain these interfaces. 
    // See: IDxcCompiler3 and IDxcUtils on Microsoft Learn. 
    // (Do not use the deprecated IDxcCompiler APIs.)  [docs cited in README]
    // 
    // Note: DXC doesn't require CoInitializeEx for DxcCreateInstance, but your app
    // likely initializes COM elsewhere for other components.

    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils)))) {
        throw std::runtime_error("Failed to create IDxcUtils (dxcompiler.dll not found?)");
    }
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler)))) {
        throw std::runtime_error("Failed to create IDxcCompiler3");
    }
    if (FAILED(m_utils->CreateDefaultIncludeHandler(&m_defaultInclude))) {
        throw std::runtime_error("Failed to create default include handler");
    }
}

std::vector<DxcDefine>
ShaderCompiler::BuildDxcDefines(const std::vector<ShaderDefine>& in, std::vector<std::wstring>& backing) {
    std::vector<DxcDefine> out;
    out.reserve(in.size());
    // Keep owned wide strings alive while DXC parses.
    backing.clear();
    backing.reserve(in.size() * 2);
    for (auto& d : in) {
        backing.push_back(d.name);
        backing.push_back(d.value);
        out.push_back(DxcDefine{ backing[backing.size()-2].c_str(), backing.back().c_str() });
    }
    return out;
}

std::vector<const wchar_t*>
ShaderCompiler::BuildArgPointers(const std::vector<std::wstring>& args) {
    std::vector<const wchar_t*> ptrs;
    ptrs.reserve(args.size());
    for (auto& a : args) ptrs.push_back(a.c_str());
    return ptrs;
}

ShaderBinary ShaderCompiler::CompileFromFile(const std::filesystem::path& hlslPath,
                                             const ShaderCompileOptions& opts) const
{
    ShaderBinary out{};
    const std::wstring wpath = ToWide(hlslPath);

    ComPtr<IDxcBlobEncoding> srcBlob;
    UINT32 codePage = 0; // autodetect
    HRESULT hr = m_utils->LoadFile(wpath.c_str(), &codePage, &srcBlob);
    if (FAILED(hr) || !srcBlob) {
        out.succeeded = false;
        return out;
    }

    DxcBuffer dxcBuf{};
    dxcBuf.Encoding = codePage; // often DXC_CP_UTF8
    dxcBuf.Ptr      = srcBlob->GetBufferPointer();
    dxcBuf.Size     = srcBlob->GetBufferSize();

    return CompileInternal(dxcBuf, wpath, opts);
}

ShaderBinary ShaderCompiler::CompileFromSource(std::wstring_view virtualFileName,
                                               std::string_view  sourceUtf8,
                                               const ShaderCompileOptions& opts) const
{
    ShaderBinary out{};

    ComPtr<IDxcBlobEncoding> srcBlob;
    HRESULT hr = m_utils->CreateBlob(sourceUtf8.data(),
                                     static_cast<UINT32>(sourceUtf8.size()),
                                     DXC_CP_UTF8,
                                     &srcBlob);
    if (FAILED(hr) || !srcBlob) {
        out.succeeded = false;
        return out;
    }

    DxcBuffer dxcBuf{};
    dxcBuf.Encoding = DXC_CP_UTF8;
    dxcBuf.Ptr      = srcBlob->GetBufferPointer();
    dxcBuf.Size     = srcBlob->GetBufferSize();

    return CompileInternal(dxcBuf, std::wstring(virtualFileName), opts);
}

ShaderBinary ShaderCompiler::CompileInternal(const DxcBuffer& srcBuffer,
                                             std::wstring_view sourceName,
                                             const ShaderCompileOptions& opts) const
{
    std::scoped_lock lock(m_mutex);

    // Build argument list.
    std::vector<std::wstring> args;
    args.reserve(16 + opts.includeDirs.size() * 2 + opts.extraArgs.size());

    // Required: entry, target
    args.push_back(L"-E"); args.push_back(opts.entryPoint);
    args.push_back(L"-T"); args.push_back(opts.targetProfile);

    // Optimization / debug
    if (opts.debug) {
        args.push_back(L"-Zi");             // generate debug info
        args.push_back(L"-Qembed_debug");   // embed PDB in DXIL container as well
        // Optional: separate PDB file on disk
        if (opts.pdbOutputDir && !opts.pdbOutputDir->empty()) {
            args.push_back(L"-Fd");
            // DXC wants a *file* path; provide "Dir/SourceName.pdb".
            std::filesystem::path pdbPath = *opts.pdbOutputDir;
            pdbPath /= std::filesystem::path(sourceName).filename();
            pdbPath.replace_extension(L".pdb");
            args.push_back(pdbPath.wstring());
        }
        // Also disable optimizations if you prefer extremely debuggable code:
        args.push_back(L"-Od");
    } else {
        args.push_back(L"-O3");
    }
    if (opts.warningsAsErrors) {
        args.push_back(L"-WX");
    }

    // Include directories
    for (auto& inc : opts.includeDirs) {
        args.push_back(L"-I");
        args.push_back(inc.wstring());
    }

    // Extra args
    for (auto& a : opts.extraArgs) {
        args.push_back(a);
    }

    // Defines
    std::vector<std::wstring> definesBacking;
    auto dxcDefines = BuildDxcDefines(opts.defines, definesBacking);

    // BuildArgs helps pack PCWSTR[] and DxcDefine[]
    ComPtr<IDxcCompilerArgs> compiledArgs;
    if (FAILED(m_utils->BuildArguments(
        sourceName.data(),
        opts.entryPoint.c_str(),
        opts.targetProfile.c_str(),
        BuildArgPointers(args).data(),
        static_cast<UINT32>(args.size()),
        dxcDefines.data(),
        static_cast<UINT32>(dxcDefines.size()),
        &compiledArgs))) 
    {
        ShaderBinary o{}; o.succeeded = false; return o;
    }

    // Compile
    ComPtr<IDxcResult> result;
    HRESULT hr = m_compiler->Compile(
        &srcBuffer,
        compiledArgs->GetArguments(),
        compiledArgs->GetCount(),
        m_defaultInclude.Get(),
        IID_PPV_ARGS(&result));
    if (FAILED(hr) || !result) {
        ShaderBinary o{}; o.succeeded = false; return o;
    }

    // Pull status + errors
    ShaderBinary out{};
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    out.errors = errors;

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        out.succeeded = false;
        return out; // caller can inspect 'errors'
    }

    // Object (DXIL)
    ComPtr<IDxcBlob> obj;
    ComPtr<IDxcBlobUtf16> objName;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&obj), &objName))) {
        out.succeeded = false; return out;
    }
    out.dxil = obj;

    // Optional PDB
    ComPtr<IDxcBlob> pdb;
    ComPtr<IDxcBlobUtf16> pdbName;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdb), &pdbName)) && pdb) {
        out.pdb = pdb;
        if (pdbName) out.pdbName = pdbName->GetStringPointer();
    }

    out.succeeded = true;
    return out;
}

} // namespace colony::gfx
