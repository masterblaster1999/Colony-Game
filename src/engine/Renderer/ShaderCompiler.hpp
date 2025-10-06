#pragma once
// Minimal DXC wrapper (Windows-only).
// Requires: dxcompiler.lib + dxcompiler.dll at runtime.
// Prefer getting DXC via vcpkg 'directx-dxc' (CMake target: Microsoft::DirectXShaderCompiler).

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>
#include <mutex>

#include <wrl/client.h>       // Microsoft::WRL::ComPtr
#include <dxcapi.h>           // IDxcCompiler3 / IDxcUtils / DxcCreateInstance

// Optional convenience to link if you're not using CMake target:
// #if defined(_MSC_VER)
// #pragma comment(lib, "dxcompiler.lib")
// #endif

namespace colony::gfx {

struct ShaderDefine {
    std::wstring name;   // e.g. L"USE_FOG"
    std::wstring value;  // e.g. L"1" (may be empty)
};

struct ShaderCompileOptions {
    std::wstring entryPoint;           // e.g. L"main"
    std::wstring targetProfile;        // e.g. L"ps_6_7", L"vs_6_7", L"cs_6_7"
    std::vector<ShaderDefine> defines; // optional
    std::vector<std::filesystem::path> includeDirs; // passed as -I
    bool debug = false;                // -Zi + embed debug / PDB
    bool warningsAsErrors = false;     // -WX
    std::vector<std::wstring> extraArgs; // any extra dxc args (e.g. L"-enable-16bit-types")
    std::optional<std::filesystem::path> pdbOutputDir; // if set and debug=true, write PDB next to output
};

struct ShaderBinary {
    Microsoft::WRL::ComPtr<IDxcBlob>       dxil;      // compiled object (DXIL container)
    Microsoft::WRL::ComPtr<IDxcBlobUtf8>   errors;    // UTF8 compiler output (warnings/errors)
    Microsoft::WRL::ComPtr<IDxcBlob>       pdb;       // optional (when -Zi)
    std::wstring                           pdbName;   // from compiler (may include path)
    bool                                    succeeded = false;
};

class ShaderCompiler {
public:
    ShaderCompiler();

    // Compile from a source file on disk (UTF-8 or UTF-16 paths).
    ShaderBinary CompileFromFile(const std::filesystem::path& hlslPath,
                                 const ShaderCompileOptions& opts) const;

    // Compile from in-memory UTF-8 source (e.g., generated code). 'virtualFileName' is used in messages/#line.
    ShaderBinary CompileFromSource(std::wstring_view virtualFileName,
                                   std::string_view  sourceUtf8,
                                   const ShaderCompileOptions& opts) const;

private:
    Microsoft::WRL::ComPtr<IDxcUtils>           m_utils;
    Microsoft::WRL::ComPtr<IDxcCompiler3>       m_compiler;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler>  m_defaultInclude;
    // IDxcCompiler3 is generally safe to call from multiple threads, but we serialize to be conservative.
    mutable std::mutex                          m_mutex;

    ShaderBinary CompileInternal(const DxcBuffer& srcBuffer,
                                 std::wstring_view sourceName,
                                 const ShaderCompileOptions& opts) const;

    static std::vector<DxcDefine> BuildDxcDefines(const std::vector<ShaderDefine>& in,
                                                  std::vector<std::wstring>& backingStorage);
    static std::vector<const wchar_t*> BuildArgPointers(const std::vector<std::wstring>& args);
};

} // namespace colony::gfx
