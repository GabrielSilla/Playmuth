// Vulkan with the Windows x64 calling convention: the host's vulkan-1.dll.
// Compiled for x86_64-pc-freebsd like guest_abi.c, for the same reason (the
// PS5 target rejects ms_abi); it calls only the driver through the slots the
// host fills and this project's own functions. vulkan_commands.inc is
// generated from vulkan_core.h by host/tools/gen_vulkan_commands.py.
//
// Each adapter is a plain call: the compiler moves the arguments from the
// Windows registers and stack to the System V ones. No Vulkan command passes
// a structure by value; structure fields keep one layout on both sides. The
// adapters skip hostGate: Vulkan calls return without waiting on the engine.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include "VulkanAbi.h"

#define VK_COMMAND(ret,name,params,args) \
    static PFN_##name driver_##name; \
    static __attribute__((ms_abi)) ret ms_##name params {return driver_##name args;}
// A driver failure is logged once per command and result: vkd3d-proton turns
// it into a bare HRESULT (title 0.22: E_OUTOFMEMORY from D3D12CreateDevice).
#define VK_RESULT_COMMAND(ret,name,params,args) \
    static PFN_##name driver_##name; \
    static __attribute__((ms_abi)) ret ms_##name params { \
        ret result=driver_##name args; \
        if((int32_t)result<0)hostVulkanError(#name,(int32_t)result); \
        return result; \
    }
#include "vulkan_commands.inc"
#undef VK_COMMAND
#undef VK_RESULT_COMMAND

static __attribute__((ms_abi)) VkResult ms_vkCreateInstance(
    const VkInstanceCreateInfo* info,const VkAllocationCallbacks* allocator,VkInstance* instance) {
    hostVulkanAllocator(allocator);
    return (VkResult)hostVulkanCreateInstance(info,instance);
}
// VK_KHR_win32_surface (vulkan_win32.h), which the host offers itself: the
// create info is passed as it is (HINSTANCE, HWND after the header).
static __attribute__((ms_abi)) VkResult ms_vkCreateWin32SurfaceKHR(
    VkInstance instance,const void* info,const VkAllocationCallbacks* allocator,VkSurfaceKHR* surface) {
    hostVulkanAllocator(allocator);
    return (VkResult)hostVulkanCreateWin32Surface(instance,info,(uint64_t*)surface);
}
static __attribute__((ms_abi)) VkBool32 ms_vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice,uint32_t family) {
    return hostVulkanWin32PresentationSupport(physicalDevice,family);
}
static __attribute__((ms_abi)) VkResult ms_vkEnumerateInstanceExtensionProperties(
    const char* layer,uint32_t* count,VkExtensionProperties* properties) {
    return (VkResult)hostVulkanInstanceExtensions(layer,count,properties);
}
static __attribute__((ms_abi)) VkResult ms_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,const char* layer,uint32_t* count,VkExtensionProperties* properties) {
    return (VkResult)hostVulkanDeviceExtensions(physicalDevice,layer,count,properties);
}

// The host indexes this table by name on first use.
#define VK_COMMAND(ret,name,params,args) {#name,(const void*)&ms_##name,(void**)&driver_##name},
#define VK_RESULT_COMMAND(ret,name,params,args) {#name,(const void*)&ms_##name,(void**)&driver_##name},
const GuestVulkanCommand guestVulkanCommands[]={
#include "vulkan_commands.inc"
    {"vkCreateInstance",(const void*)&ms_vkCreateInstance,0},
    {"vkCreateWin32SurfaceKHR",(const void*)&ms_vkCreateWin32SurfaceKHR,0},
    {"vkGetPhysicalDeviceWin32PresentationSupportKHR",(const void*)&ms_vkGetPhysicalDeviceWin32PresentationSupportKHR,0},
    {"vkEnumerateDeviceExtensionProperties",(const void*)&ms_vkEnumerateDeviceExtensionProperties,0},
    {"vkEnumerateInstanceExtensionProperties",(const void*)&ms_vkEnumerateInstanceExtensionProperties,0},
    {0,0,0}
};
#undef VK_COMMAND
#undef VK_RESULT_COMMAND
