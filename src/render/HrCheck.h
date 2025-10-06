#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <comdef.h>
#include <stdexcept>
#include <sstream>
#include <string>

namespace render
{
    inline std::string WideToUtf8(const wchar_t* w)
    {
        if (!w) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
        return s;
    }

    inline void ThrowIfFailed(HRESULT hr, const char* expr, const char* file, int line)
    {
        if (FAILED(hr))
        {
            _com_error err(hr);
            std::ostringstream oss;
            oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr)
                << " at " << file << ":" << std::dec << line
                << " for " << expr << " — " << WideToUtf8(err.ErrorMessage());
            throw std::runtime_error(oss.str());
        }
    }
} // namespace render

#define HR_CHECK(EXPR) ::render::ThrowIfFailed((EXPR), #EXPR, __FILE__, __LINE__)
