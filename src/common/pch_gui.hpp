#pragma once
#include "common/pch.hpp"

// Only include ImGui when available (compile-time macro from CMake)
#if defined(COLONY_WITH_IMGUI)
  #include <imgui.h>
#endif
