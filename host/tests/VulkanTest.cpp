// The host's vulkan-1.dll as a Windows module sees it, checked on Linux over
// the system's Vulkan (lavapipe in WSL): LoadLibrary + GetProcAddress for
// vkGetInstanceProcAddr, then every call through the Windows x64 adapters,
// as vkd3d-proton will make them. VulkanTest
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include "../src/Teb.h"
#include "../src/win32/Win32.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT

namespace azimuth::win32 {
void log(const std::string& text){std::printf("  [host] %s\n",text.c_str());}
void stopGuest(const std::string& reason){throw std::runtime_error("parada: "+reason);}
}

namespace {
// The Windows-convention form of each command the test calls.
#define MS_TYPE(ret,name,...) typedef ret (__attribute__((ms_abi)) *Ms_##name)(__VA_ARGS__)
MS_TYPE(PFN_vkVoidFunction,vkGetInstanceProcAddr,VkInstance,const char*);
MS_TYPE(PFN_vkVoidFunction,vkGetDeviceProcAddr,VkDevice,const char*);
MS_TYPE(VkResult,vkEnumerateInstanceVersion,uint32_t*);
MS_TYPE(VkResult,vkEnumerateInstanceExtensionProperties,const char*,uint32_t*,VkExtensionProperties*);
MS_TYPE(VkResult,vkCreateInstance,const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
MS_TYPE(VkResult,vkEnumeratePhysicalDevices,VkInstance,uint32_t*,VkPhysicalDevice*);
MS_TYPE(void,vkGetPhysicalDeviceProperties,VkPhysicalDevice,VkPhysicalDeviceProperties*);
MS_TYPE(void,vkGetPhysicalDeviceMemoryProperties,VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*);
MS_TYPE(VkResult,vkEnumerateDeviceExtensionProperties,VkPhysicalDevice,const char*,uint32_t*,VkExtensionProperties*);
MS_TYPE(VkResult,vkCreateDevice,VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
MS_TYPE(void,vkGetDeviceQueue,VkDevice,uint32_t,uint32_t,VkQueue*);
MS_TYPE(VkResult,vkCreateBuffer,VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer*);
MS_TYPE(void,vkGetBufferMemoryRequirements,VkDevice,VkBuffer,VkMemoryRequirements*);
MS_TYPE(VkResult,vkAllocateMemory,VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory*);
MS_TYPE(VkResult,vkBindBufferMemory,VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize);
MS_TYPE(VkResult,vkMapMemory,VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void**);
MS_TYPE(VkResult,vkCreateCommandPool,VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool*);
MS_TYPE(VkResult,vkAllocateCommandBuffers,VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*);
MS_TYPE(VkResult,vkBeginCommandBuffer,VkCommandBuffer,const VkCommandBufferBeginInfo*);
MS_TYPE(void,vkCmdFillBuffer,VkCommandBuffer,VkBuffer,VkDeviceSize,VkDeviceSize,uint32_t);
MS_TYPE(void,vkCmdUpdateBuffer,VkCommandBuffer,VkBuffer,VkDeviceSize,VkDeviceSize,const void*);
MS_TYPE(void,vkCmdPipelineBarrier,VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*);
MS_TYPE(VkResult,vkEndCommandBuffer,VkCommandBuffer);
MS_TYPE(VkResult,vkCreateFence,VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence*);
MS_TYPE(VkResult,vkQueueSubmit,VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
MS_TYPE(VkResult,vkWaitForFences,VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t);
MS_TYPE(void,vkDestroyFence,VkDevice,VkFence,const VkAllocationCallbacks*);
MS_TYPE(void,vkDestroyCommandPool,VkDevice,VkCommandPool,const VkAllocationCallbacks*);
MS_TYPE(void,vkFreeMemory,VkDevice,VkDeviceMemory,const VkAllocationCallbacks*);
MS_TYPE(void,vkDestroyBuffer,VkDevice,VkBuffer,const VkAllocationCallbacks*);
MS_TYPE(void,vkDestroyDevice,VkDevice,const VkAllocationCallbacks*);
MS_TYPE(void,vkDestroyInstance,VkInstance,const VkAllocationCallbacks*);

int checks=0;
void check(bool ok,const std::string& what){++checks;if(!ok)throw std::runtime_error("falhou: "+what);}
// An allocator whose callbacks would crash the driver if it called them
// (Windows-convention functions called with the System V convention).
__attribute__((ms_abi)) void* neverCalled(void*,size_t,size_t,VkSystemAllocationScope){std::abort();}
bool hasExtension(const std::vector<VkExtensionProperties>& list,const char* name) {
    for(auto& e:list)if(!std::strcmp(e.extensionName,name))return true;
    return false;
}
}
using namespace azimuth::win32;

int main() {
    azimuth::ProcessBlock processBlock(0);
    azimuth::ThreadBlock thread(processBlock,nullptr,nullptr,256);
    if(thread.enter()){std::fprintf(stderr,"gs nao configurado\n");return 1;}
    configure({});
    try {
        void* module=win_LoadLibraryExW(reinterpret_cast<const uint16_t*>(u"vulkan-1.dll"),nullptr,0);
        check(module!=nullptr,"LoadLibrary vulkan-1.dll");
        auto gipa=reinterpret_cast<Ms_vkGetInstanceProcAddr>(win_GetProcAddress(module,"vkGetInstanceProcAddr"));
        check(gipa!=nullptr,"GetProcAddress vkGetInstanceProcAddr");
        check(win_GetProcAddress(module,"vkCreateInstance")!=nullptr,"vulkan-1.dll exporta os comandos globais");
#define GLOBAL(name) auto name=reinterpret_cast<Ms_##name>(gipa(nullptr,#name));check(name!=nullptr,#name)
        GLOBAL(vkEnumerateInstanceVersion);GLOBAL(vkEnumerateInstanceExtensionProperties);GLOBAL(vkCreateInstance);
        uint32_t api=0;check(vkEnumerateInstanceVersion(&api)==VK_SUCCESS&&api>=VK_API_VERSION_1_1,"vkEnumerateInstanceVersion");
        uint32_t count=0;check(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr)==VK_SUCCESS,"extensoes da instancia (contagem)");
        std::vector<VkExtensionProperties> instanceExtensions(count);
        check(vkEnumerateInstanceExtensionProperties(nullptr,&count,instanceExtensions.data())==VK_SUCCESS,"extensoes da instancia");
        check(!hasExtension(instanceExtensions,"VK_EXT_debug_utils")&&!hasExtension(instanceExtensions,"VK_EXT_debug_report"),"extensoes de callback escondidas");
        if(count>1) {
            uint32_t fewer=count-1;std::vector<VkExtensionProperties> part(fewer);
            check(vkEnumerateInstanceExtensionProperties(nullptr,&fewer,part.data())==VK_INCOMPLETE&&fewer==count-1,"VK_INCOMPLETE com lista curta");
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr,"VulkanTest",1,"Azimuth_PS5",1,VK_API_VERSION_1_1};
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr,0,&app,0,nullptr,0,nullptr};
        VkAllocationCallbacks allocator{};allocator.pfnAllocation=reinterpret_cast<PFN_vkAllocationFunction>(&neverCalled);
        VkInstance instance=VK_NULL_HANDLE;
        check(vkCreateInstance(&instanceInfo,&allocator,&instance)==VK_SUCCESS,"vkCreateInstance com allocator do chamador");
        check(gipa(instance,"vkCreateDebugUtilsMessengerEXT")==nullptr,"vkCreateDebugUtilsMessengerEXT escondido");
        check(gipa(instance,"vkGetInstanceProcAddr")==reinterpret_cast<PFN_vkVoidFunction>(gipa),"vkGetInstanceProcAddr resolve para o do host");
#define INSTANCE(name) auto name=reinterpret_cast<Ms_##name>(gipa(instance,#name));check(name!=nullptr,#name)
        INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);
        INSTANCE(vkEnumerateDeviceExtensionProperties);INSTANCE(vkCreateDevice);INSTANCE(vkDestroyInstance);INSTANCE(vkGetDeviceProcAddr);
        uint32_t devices=1;VkPhysicalDevice physical=VK_NULL_HANDLE;
        VkResult r=vkEnumeratePhysicalDevices(instance,&devices,&physical);
        check((r==VK_SUCCESS||r==VK_INCOMPLETE)&&physical,"vkEnumeratePhysicalDevices");
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
        std::printf("  dispositivo: %s, Vulkan %u.%u\n",properties.deviceName,VK_API_VERSION_MAJOR(properties.apiVersion),VK_API_VERSION_MINOR(properties.apiVersion));
        check(properties.deviceName[0]!=0,"vkGetPhysicalDeviceProperties");
        count=0;check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr)==VK_SUCCESS&&count>0,"extensoes do dispositivo");
        float priority=1.f;VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,nullptr,0,0,1,&priority};
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,nullptr,0,1,&queueInfo,0,nullptr,0,nullptr,nullptr};
        VkDevice device=VK_NULL_HANDLE;check(vkCreateDevice(physical,&deviceInfo,nullptr,&device)==VK_SUCCESS,"vkCreateDevice");
        check(reinterpret_cast<void*>(vkGetDeviceProcAddr)==win_GetProcAddress(module,"vkGetDeviceProcAddr"),"vkGetDeviceProcAddr do host");
#define DEVICE(name) auto name=reinterpret_cast<Ms_##name>(vkGetDeviceProcAddr(device,#name));check(name!=nullptr,#name)
        DEVICE(vkGetDeviceQueue);DEVICE(vkCreateBuffer);DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkAllocateMemory);
        DEVICE(vkBindBufferMemory);DEVICE(vkMapMemory);DEVICE(vkCreateCommandPool);DEVICE(vkAllocateCommandBuffers);
        DEVICE(vkBeginCommandBuffer);DEVICE(vkCmdFillBuffer);DEVICE(vkCmdUpdateBuffer);DEVICE(vkCmdPipelineBarrier);
        DEVICE(vkEndCommandBuffer);DEVICE(vkCreateFence);DEVICE(vkQueueSubmit);DEVICE(vkWaitForFences);DEVICE(vkDestroyFence);
        DEVICE(vkDestroyCommandPool);DEVICE(vkFreeMemory);DEVICE(vkDestroyBuffer);DEVICE(vkDestroyDevice);
        VkQueue queue=VK_NULL_HANDLE;vkGetDeviceQueue(device,0,0,&queue);check(queue!=VK_NULL_HANDLE,"vkGetDeviceQueue");
        // The probe's GPU writes, now through the adapters: an update of 64
        // bytes, then a fill of the rest of 4096 bytes.
        constexpr VkDeviceSize Size=4096;constexpr uint32_t Pattern=0xa5c3e10fu;
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,nullptr,0,Size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE,0,nullptr};
        VkBuffer buffer=VK_NULL_HANDLE;check(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer)==VK_SUCCESS,"vkCreateBuffer");
        VkMemoryRequirements need{};vkGetBufferMemoryRequirements(device,buffer,&need);
        VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        uint32_t type=UINT32_MAX;constexpr VkMemoryPropertyFlags Host=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(uint32_t n=0;n<memory.memoryTypeCount&&type==UINT32_MAX;++n)if((need.memoryTypeBits>>n&1)&&(memory.memoryTypes[n].propertyFlags&Host)==Host)type=n;
        check(type!=UINT32_MAX,"memoria visivel ao host");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,nullptr,need.size,type};
        VkDeviceMemory deviceMemory=VK_NULL_HANDLE;check(vkAllocateMemory(device,&allocation,nullptr,&deviceMemory)==VK_SUCCESS,"vkAllocateMemory");
        check(vkBindBufferMemory(device,buffer,deviceMemory,0)==VK_SUCCESS,"vkBindBufferMemory");
        uint32_t* words=nullptr;check(vkMapMemory(device,deviceMemory,0,Size,0,reinterpret_cast<void**>(&words))==VK_SUCCESS&&words,"vkMapMemory");
        std::memset(words,0,Size);
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,nullptr,0,0};
        VkCommandPool pool=VK_NULL_HANDLE;check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool)==VK_SUCCESS,"vkCreateCommandPool");
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,nullptr,pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
        VkCommandBuffer command=VK_NULL_HANDLE;check(vkAllocateCommandBuffers(device,&commandInfo,&command)==VK_SUCCESS,"vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,nullptr,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,nullptr};
        check(vkBeginCommandBuffer(command,&begin)==VK_SUCCESS,"vkBeginCommandBuffer");
        uint32_t update[16];for(uint32_t n=0;n<16;++n)update[n]=Pattern^n;
        vkCmdUpdateBuffer(command,buffer,0,sizeof update,update);
        vkCmdFillBuffer(command,buffer,sizeof update,Size-sizeof update,Pattern);
        VkMemoryBarrier hostRead{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&hostRead,0,nullptr,0,nullptr);
        check(vkEndCommandBuffer(command)==VK_SUCCESS,"vkEndCommandBuffer");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,nullptr,0};
        VkFence fence=VK_NULL_HANDLE;check(vkCreateFence(device,&fenceInfo,nullptr,&fence)==VK_SUCCESS,"vkCreateFence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO,nullptr,0,nullptr,nullptr,1,&command,0,nullptr};
        check(vkQueueSubmit(queue,1,&submit,fence)==VK_SUCCESS,"vkQueueSubmit");
        // uint64_t timeout and VkBool32 in the fourth and fifth places: stack
        // arguments on the Windows side.
        check(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull)==VK_SUCCESS,"vkWaitForFences");
        unsigned wrong=0;
        for(uint32_t n=0;n<Size/4;++n)if(words[n]!=(n<16?Pattern^n:Pattern))++wrong;
        std::printf("  %u de %u palavras diferentes do esperado\n",wrong,unsigned(Size/4));
        check(!wrong,"conteudo escrito pela GPU");
        vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);
        vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,deviceMemory,nullptr);
        vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,&allocator);
    } catch(const std::exception& error) {
        std::printf("VulkanTest: %s (depois de %d verificacoes)\n",error.what(),checks);
        return 1;
    }
    std::printf("VulkanTest: %d verificacoes passaram pelos adaptadores Windows x64\n",checks);
    return 0;
}
