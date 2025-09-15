#pragma once
#include <string>
#include <filesystem>

namespace cg {

class Log {
public:
    static void Init(const std::filesystem::path& logDir);
    static void Shutdown();

    static void Info(const std::string& msg);
    static void Warn(const std::string& msg);
    static void Error(const std::string& msg);

private:
    static void Write(const char* level, const std::string& msg);
};

} // namespace cg
