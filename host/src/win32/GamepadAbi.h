#pragma once
#include <stdint.h>

// The vtables of the host's Windows.Gaming.Input objects (gamepad_abi.c), and
// what they call (Xinput.cpp).
#ifdef __cplusplus
extern "C" {
#endif
enum {
    GamepadStaticsSlots=11,   // IGamepadStatics
    GamepadListSlots=10,      // IVectorView<Gamepad>
    GamepadSlots=9,           // IGamepad
};
extern const void* const guestGamepadStaticsVtable[GamepadStaticsSlots];
extern const void* const guestGamepadListVtable[GamepadListSlots];
extern const void* const guestGamepadVtable[GamepadSlots];
#ifdef __cplusplus
}
#endif
