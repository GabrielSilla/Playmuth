#pragma once
#include <cstdint>

// The PS5 controller for the engine. The launcher takes the keyboard (Enter,
// arrows, Escape), the game XInput: a thread polls ScePad and gives the
// engine both (win32/User32.cpp keys, win32/Xinput.cpp state).
namespace azimuth::input {
// The latest pad sample. Buttons are ScePad's bits.
struct Pad {
    uint32_t buttons=0;
    uint8_t leftX=128,leftY=128,rightX=128,rightY=128,left2=0,right2=0;   // sticks 0..255, centre 128
    bool connected=false;
    uint32_t packet=0;   // counts changes, as XINPUT_STATE.dwPacketNumber
};
// ScePad button bits (SCE_PAD_BUTTON_*).
enum : uint32_t {
    L3=0x2,R3=0x4,Options=0x8,Up=0x10,Right=0x20,Down=0x40,Left=0x80,L2=0x100,R2=0x200,L1=0x400,R1=0x800,
    Triangle=0x1000,Circle=0x2000,Cross=0x4000,Square=0x8000,TouchPad=0x100000
};
// Starts polling (once; on the console only: nothing happens on Linux).
void start();
Pad pad();
// Rumble, as XInputSetState gives it: each motor 0..255 (title 1.07).
void vibrate(uint8_t large,uint8_t small);
// XInputGetState was called: while the engine polls XInput, the pad's buttons
// are not also posted as keys (title 1.08: the launcher saw every press twice).
void polledByXInput();
}
