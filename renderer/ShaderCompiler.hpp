// renderer/ShaderCompiler.hpp
//
// Small DXC wrapper around IDxcUtils + IDxcCompiler3 for compiling HLSL to DXIL.
// Windows-only; intended for use from the Colony-Game renderer.
//
// Usage example (runtime compilation / hot-reload):
//
//   using namespace colony::render;
//
//   ShaderCompiler compiler;
//   CompileOptions opts;
//   opts.enableDebug      = true;
//   opts.warningsAreErrors = true;
//   opts.optimizationLevel = 3;
//   opts.stripDebug        = false;       // keep debug in DXC_OUT_OBJECT or separate PDB
//   opts.stripReflection   = false;
//
//   ShaderCompileResult res = compiler.compileFromFile(
//       L"renderer/Shaders/erosion_thermal_flow_cs.hlsl",
//       L"CSMain",
//       L"cs_6_0",
//       opts
//   );
//
//   if (!res.success) {
//       // Log res.errorMessage (UTF-8) somewhere.
//   } else {
//       // Use res.bytecode.data() + res.bytecode.size() with D3D12 PSO creation.
//   }

#pragma once

#ifdef _WIN32

// -----------------------------------------------------------------------------
// Windows + DXC includes
// -----------------------------------------------------------------------------
#ifdef __has_include
#  if __has_include("platform/win/WinHeaders.h")
#    include "platform/win/WinHeaders.h"
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <Windows.h>
#  endif
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <wrl/client.h>
#include <dxcapi.h>

namespace colony::render
{

// Single HLSL define (e.g. -DNAME=VALUE).
struct ShaderDefine
{
    std::wstring name;
    std::wstring value;
};

// High-level compile options; translated into DXC arguments.
struct CompileOptions
{
    // -Zi / no -Zi
    bool enableDebug        = true;

    // -WX
    bool warningsAreErrors  = true;

    // 0..3 -> -O0..-O3, or negative to use DXC default (O3).
    int  optimizationLevel  = 3;

    // -Qstrip_debug, -Qstrip_reflect
    bool stripDebug         = false;
    bool stripReflection    = false;

    // -Vd (skip validator)
    bool disableValidation  = false;

    // "-I <dir>" per element.
    std::vector<std::wstring> includeDirs;

    // -D NAME=VALUE per element.
    std::vector<ShaderDefine> defines;

    // Extra raw arguments, passed through as-is (e.g. L"-Zpc", L"-HV", L"2021").
    std::vector<std::wstring> extraArguments;
};

// Result of a shader compile.
struct ShaderCompileResult
{
    bool success = false;

    // DXC_OUT_OBJECT: compiled DXIL container.
    std::vector<std::uint8_t> bytecode;

    // DXC_OUT_PDB: optional PDB blob (if generated).
    std::vector<std::uint8_t> pdb;

    // DXC_OUT_REFLECTION: optional reflection blob (if generated).
    std::vector<std::uint8_t> reflection;

    // DXC_OUT_ERRORS: UTF-8 errors/warnings (if any).
    std::string errorMessage;
};

// Wraps IDxcUtils + IDxcCompiler3 for basic HLSL compilation.
// Not thread-safe by itself; either create one instance per thread or
// ensure external synchronization around Compile() calls.
// (DXC docs explicitly recommend one compiler/utils instance per thread.) :contentReference[oaicite:1]{index=1}
class ShaderCompiler
{
public:
    ShaderCompiler();
    ~ShaderCompiler() = default;

    ShaderCompiler(const ShaderCompiler&)            = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;
    ShaderCompiler(ShaderCompiler&&) noexcept        = default;
    ShaderCompiler& operator=(ShaderCompiler&&) noexcept = default;

    // Compile a shader from a file on disk.
    // - path: absolute or relative path (UTF-16).
    // - entryPoint: e.g. L"CSMain", L"VSMain", L"PSMain".
    // - profile: e.g. L"cs_6_0", L"ps_6_0", L"vs_6_0".
    ShaderCompileResult compileFromFile(
        const std::wstring& path,
        const std::wstring& entryPoint,
        const std::wstring& profile,
        const CompileOptions& options = {}
    ) const;

    // Compile a shader from an in-memory UTF-8 string.
    // sourceName is used only for diagnostics and include resolution.
    ShaderCompileResult compileFromSource(
        const std::wstring& sourceName,
        std::string_view    sourceUtf8,
        const std::wstring& entryPoint,
        const std::wstring& profile,
        const CompileOptions& options = {}
    ) const;

private:
    using ComPtr = Microsoft::WRL::ComPtr;

    ShaderCompileResult compileInternal(
        const std::wstring& sourceName,
        const DxcBuffer&    sourceBuffer,
        const std::wstring& entryPoint,
        const std::wstring& profile,
        const CompileOptions& options
    ) const;

    ComPtr<IDxcUtils>          m_utils;
    ComPtr<IDxcCompiler3>      m_compiler;
    ComPtr<IDxcIncludeHandler> m_includeHandler;
};

} // namespace colony::render

#else
#  error "ShaderCompiler is only supported on Windows (_WIN32)."
#endif // _WIN32
