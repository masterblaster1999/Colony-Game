#pragma once

#include <string>

// Robust Windows-style argument quoting (CommandLineToArgvW-compatible)
std::wstring QuoteArgWindows(const std::wstring& arg);

// Build child arguments from our own command line (skips argv[0])
std::wstring BuildChildArguments();

// CLI helpers for flags like:
//   --exe | --exe=...
//   --skip-preflight
//   --no-singleton
bool TryGetArgValue(const wchar_t* name, std::wstring& out);
bool HasFlag(const wchar_t* name);
