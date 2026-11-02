#pragma once
#include <cstdint>

namespace azimuth::gpu {
// The D3D12 check (host --d3d12, title mode d3d12), on an engine thread with
// no engine: vkd3d-proton's d3d12.dll loaded by the host's loader over the
// host's vulkan-1.dll, then a device, a direct queue, an allocator, an empty
// command list executed and a fence signalled. 0 when every step passed.
uint32_t d3d12Check();
}
