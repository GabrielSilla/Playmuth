#pragma once
#include <stdint.h>

// The host's vulkan-1.dll: Vulkan commands with the Windows x64 convention,
// for Windows modules such as vkd3d-proton (vulkan_abi.c, compiled like
// guest_abi.c). Each adapter calls the driver's System V function stored in
// its slot; the host fills the slot when the command is resolved (Vulkan.cpp).
#ifdef __cplusplus
extern "C" {
#endif
// One per command, ending with a null entry. `driver` is the
// slot for the driver's function; null for the commands the host writes
// itself (procedure lookup and extension lists), whose `adapter` calls the host.
typedef struct {const char* name;const void* adapter;void** driver;} GuestVulkanCommand;
extern const GuestVulkanCommand guestVulkanCommands[];

// System V side, in the host (Vulkan.cpp).
// A VkAllocationCallbacks the caller passed: its callbacks use the Windows
// convention, so the driver gets null (its own allocation) instead.
const void* hostVulkanAllocator(const void* allocator);
// A negative VkResult from the driver, logged once per command and result.
void hostVulkanError(const char* command,int32_t result);
// vkCreateInstance without the extensions the host provides itself.
int32_t hostVulkanCreateInstance(const void* info,void* instance);
// VK_KHR_win32_surface: a surface for a window of the host.
int32_t hostVulkanCreateWin32Surface(void* instance,const void* info,uint64_t* surface);
uint32_t hostVulkanWin32PresentationSupport(void* physicalDevice,uint32_t family);
// vkEnumerate{Instance,Device}ExtensionProperties without hidden extensions.
int32_t hostVulkanInstanceExtensions(const char* layer,uint32_t* count,void* properties);
int32_t hostVulkanDeviceExtensions(void* physicalDevice,const char* layer,uint32_t* count,void* properties);
#ifdef __cplusplus
}
#endif
