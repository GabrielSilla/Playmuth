#pragma once
#include <stdint.h>

// The vtables of the host's DXGI objects (dxgi_abi.c), and what they call.
#ifdef __cplusplus
extern "C" {
#endif
enum {DxgiFactorySlots=30,DxgiAdapterSlots=19,     // IDXGIFactory6, IDXGIAdapter4
      DxgiSurfaceFactorySlots=4,DxgiSwapChainSlots=41};   // IDXGIVkSurfaceFactory, IDXGISwapChain4
extern const void* const guestDxgiFactoryVtable[DxgiFactorySlots];
extern const void* const guestDxgiAdapterVtable[DxgiAdapterSlots];
extern const void* const guestDxgiSurfaceFactoryVtable[DxgiSurfaceFactorySlots];
extern const void* const guestDxgiSwapChainVtable[DxgiSwapChainSlots];
// A method the host does not implement: logs it, returns E_NOTIMPL.
int32_t dxgiMissing(const char* object,uint32_t slot);
#ifdef __cplusplus
}
#endif
