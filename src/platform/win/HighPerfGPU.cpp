#include <windows.h>
extern "C" {
// Request discrete GPU on hybrid systems (NVIDIA / AMD)
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;
}
