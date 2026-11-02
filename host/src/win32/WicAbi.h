#pragma once
#include <stdint.h>

// The vtables of the host's WIC objects (wic_abi.c), and what they call.
#ifdef __cplusplus
extern "C" {
#endif
enum {
    WicFactorySlots=28,     // IWICImagingFactory
    WicStreamSlots=18,      // IWICStream
    WicDecoderSlots=14,     // IWICBitmapDecoder
    WicFrameSlots=11,       // IWICBitmapFrameDecode
    WicConverterSlots=10,   // IWICFormatConverter
};
extern const void* const guestWicFactoryVtable[WicFactorySlots];
extern const void* const guestWicStreamVtable[WicStreamSlots];
extern const void* const guestWicDecoderVtable[WicDecoderSlots];
extern const void* const guestWicFrameVtable[WicFrameSlots];
extern const void* const guestWicConverterVtable[WicConverterSlots];
// A method the host does not implement: logs it, returns E_NOTIMPL.
int32_t wicMissing(const char* object,uint32_t slot);
#ifdef __cplusplus
}
#endif
