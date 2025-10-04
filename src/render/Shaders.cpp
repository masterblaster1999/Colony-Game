#include "Shaders.h"

// Stub for now; wire up DXC later. (See Microsoft DXC docs and vcpkg 'dxcompiler' if desired.)
namespace render {
bool CompileHLSL(const std::wstring&, const std::wstring&, const std::wstring&, CompiledShader&) {
    return false;
}
} // namespace render
