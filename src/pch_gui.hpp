#pragma once
#include "pch_core.hpp"

// GUI-only precompiled header: Dear ImGui + backends
// These headers exist when imgui is installed (via vcpkg or vendored).
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
