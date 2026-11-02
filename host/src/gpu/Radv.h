#pragma once

// The Vulkan driver behind the host's vulkan-1.dll: on the PS5 the RADV
// module shipped in the title folder (radv/, title 1.27), loaded the way GPU probe 0.20-0.22
// validated it (titles 0.14-0.17); on Linux the system's Vulkan loader.
namespace azimuth::gpu {
// The driver's vkGetInstanceProcAddr (System V), loaded on the first call and
// kept loaded until the process ends; null, with the reason logged, when
// there is no driver (a payload, a missing or unknown module).
void* driverInstanceProcAddr();
}
