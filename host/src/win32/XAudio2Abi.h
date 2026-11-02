#pragma once
#include <stdint.h>

// The vtables of the host's XAudio2 objects (xaudio2_abi.c), and what they call.
#ifdef __cplusplus
extern "C" {
#endif
enum {
    XAudio2EngineSlots=13,   // IXAudio2 (2.9)
    XAudio2SourceSlots=29,   // IXAudio2SourceVoice
    XAudio2MasterSlots=20,   // IXAudio2MasteringVoice
};
extern const void* const guestXAudio2EngineVtable[XAudio2EngineSlots];
extern const void* const guestXAudio2SourceVtable[XAudio2SourceSlots];
extern const void* const guestXAudio2MasterVtable[XAudio2MasterSlots];
// The Windows x64 start routine of the XAudio2 thread (for CreateThread).
extern const void* const guestXAudio2ThreadStart;
#ifdef __cplusplus
}
#endif
