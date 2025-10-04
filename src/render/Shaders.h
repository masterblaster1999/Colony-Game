#pragma once
#include <string>
#include <vector>

namespace render {

struct CompiledShader {
    std::vector<uint8_t> bytecode;
};

bool CompileHLSL(const std::wstring& file, const std::wstring& entry,
                 const std::wstring& profile, CompiledShader& out);

} // namespace render
