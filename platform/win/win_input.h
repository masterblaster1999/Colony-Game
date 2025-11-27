#pragma once
#include <stdint.h>

// -------- Buttons / Keys / Gamepad -------------------------------------------
struct Button { bool down = false; uint8_t trans = 0; };
static inline void set_button(Button& b, bool d){ if (b.down != d) { b.down = d; b.trans++; } }
// (overload name matches usage pattern in codebase)
static inline void begin_frame(Button& b){ b.trans = 0; }
static inline bool  pressed(const Button& b){ return b.down && b.trans > 0; }

enum KeyCode{
    Key_Unknown=0,
    Key_W,Key_A,Key_S,Key_D, Key_Q,Key_E,
    Key_Space, Key_Escape, Key_Up,Key_Down,Key_Left,Key_Right,
    Key_F1,Key_F2,Key_F3,Key_F4,Key_F5,Key_F6,Key_F7,Key_F8,Key_F9,Key_F10,Key_F11,Key_F12,
    Key_Z, Key_H, Key_G,
    Key_Count
};

struct Gamepad{
    bool connected=false; float lx=0,ly=0,rx=0,ry=0, lt=0,rt=0;
    Button a,b,x,y, lb,rb, back,start, lsb,rsb, up,down,left,right;
};

struct InputState{
    int mouseX=0,mouseY=0,mouseDX=0,mouseDY=0;
    float wheel=0.f;
    Button mouseL,mouseM,mouseR;
    Button key[Key_Count]{};
    Gamepad pads[4]{};
    bool rawMouse=false;
    char text[128]{}; int textLen=0;
};

// Reset per-frame counters (overload)
static inline void begin_frame(InputState& in){
    in.wheel = 0.f; in.mouseDX = 0; in.mouseDY = 0; in.textLen = 0; in.text[0] = 0;
    begin_frame(in.mouseL); begin_frame(in.mouseM); begin_frame(in.mouseR);
    for (int i=0; i<Key_Count; ++i) begin_frame(in.key[i]);
}

// Global input (defined once in win_input.cpp)
extern InputState g_in;
