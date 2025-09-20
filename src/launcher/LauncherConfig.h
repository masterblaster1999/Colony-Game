#pragma once
#include <string>

namespace launcher {
// Returns the target EXE name to launch (e.g., L"ColonyGame.exe").
// Reads res/launcher.cfg if present; falls back to a sensible default.
// Never returns an empty string.
std::wstring read_target_exe();
}
