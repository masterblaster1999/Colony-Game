#pragma once
#include <fstream>
#include <string>
#include <cstdarg>

namespace colony::win {

class Log {
    std::wofstream out_;
public:
    explicit Log(const std::wstring& path) : out_(path, std::ios::app) {}
    void line(const wchar_t* fmt, ...) {
        wchar_t buf[1024];
        va_list ap; va_start(ap, fmt);
        _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
        va_end(ap);
        out_ << buf << L"\r\n";
        out_.flush();
    }
};

} // namespace colony::win
