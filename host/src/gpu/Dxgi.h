#pragma once
#include <cstdint>
#include <string>

namespace azimuth::gpu {
// CreateDXGIFactory1/2: the host's factory (IDXGIFactory up to 6) with one
// adapter, the Vulkan driver's first device. No outputs or swap chains yet.
int32_t createDxgiFactory(const void* id,void** out);
// Frames presented by every swap chain so far (SwapChain.cpp).
uint64_t presentedFrames();
std::string frameIntervalReport();
// One sampler only, independent of the 10-second counters.
std::string frameWindowReport();
// Time spent inside Present so far, and the last call's SyncInterval and flags.
uint64_t presentNanoseconds();
uint32_t presentInterval();
uint32_t presentFlags();
}
