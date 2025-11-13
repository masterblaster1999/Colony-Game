// platform/win/GamepadsXInput.cpp

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Xinput.h>

#pragma comment(lib, "xinput9_1_0.lib")

bool PollPad(int idx, XINPUT_STATE& s)
{
    ZeroMemory(&s, sizeof(s));
    return XInputGetState(idx, &s) == ERROR_SUCCESS;
}

void SetRumble(int idx, float low, float high)
{
    XINPUT_VIBRATION v{};
    v.wLeftMotorSpeed  = static_cast<WORD>(std::clamp(low,  0.0f, 1.0f) * 65535.0f);
    v.wRightMotorSpeed = static_cast<WORD>(std::clamp(high, 0.0f, 1.0f) * 65535.0f);
    XInputSetState(idx, &v);
}

#endif // _WIN32
