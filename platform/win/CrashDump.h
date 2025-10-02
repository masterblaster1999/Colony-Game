#pragma once
#include <windows.h>

namespace crashdump {
    // call early in wWinMain; returns true if handler installed
    bool install();
}
