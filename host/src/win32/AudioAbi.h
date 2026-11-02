#pragma once
#include <stdint.h>

// The vtables of the host's WASAPI objects (audio_abi.c), and what they call.
#ifdef __cplusplus
extern "C" {
#endif
enum {
    AudioEnumeratorSlots=8,    // IMMDeviceEnumerator
    AudioCollectionSlots=5,    // IMMDeviceCollection
    AudioDeviceSlots=7,        // IMMDevice
    AudioStoreSlots=8,         // IPropertyStore
    AudioClientSlots=21,       // IAudioClient3
    AudioRenderSlots=5,        // IAudioRenderClient
    AudioClockSlots=6,         // IAudioClock
    AudioSimpleVolumeSlots=7,  // ISimpleAudioVolume
    AudioStreamVolumeSlots=8,  // IAudioStreamVolume
};
extern const void* const guestAudioEnumeratorVtable[AudioEnumeratorSlots];
extern const void* const guestAudioCollectionVtable[AudioCollectionSlots];
extern const void* const guestAudioDeviceVtable[AudioDeviceSlots];
extern const void* const guestAudioStoreVtable[AudioStoreSlots];
extern const void* const guestAudioClientVtable[AudioClientSlots];
extern const void* const guestAudioRenderVtable[AudioRenderSlots];
extern const void* const guestAudioClockVtable[AudioClockSlots];
extern const void* const guestAudioSimpleVolumeVtable[AudioSimpleVolumeSlots];
extern const void* const guestAudioStreamVolumeVtable[AudioStreamVolumeSlots];
// A method the host does not implement: logs it, returns E_NOTIMPL.
int32_t audioMissing(const char* object,uint32_t slot);
#ifdef __cplusplus
}
#endif
