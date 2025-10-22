// platform/win/GamepadsXInput.cpp
#include <Xinput.h>
#pragma comment(lib, "xinput9_1_0.lib")
bool PollPad(int idx, XINPUT_STATE& s) {
    ZeroMemory(&s, sizeof(s));
    return XInputGetState(idx, &s) == ERROR_SUCCESS;
}
void SetRumble(int idx, float low, float high) {
    XINPUT_VIBRATION v{};
    v.wLeftMotorSpeed  = (WORD)(low  * 65535);
    v.wRightMotorSpeed = (WORD)(high * 65535);
    XInputSetState(idx, &v);
}
