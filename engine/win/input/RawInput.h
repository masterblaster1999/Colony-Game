#pragma once
#include <cstdint>
struct RawMouseDelta { long dx{}, dy{}; bool wheel=false; long wheelDelta=0; };

namespace wininput {
bool InitializeRawMouse(void* hwnd); // pass HWND
bool HandleRawInputMessage(void* lparam, RawMouseDelta& out); // call on WM_INPUT
}
