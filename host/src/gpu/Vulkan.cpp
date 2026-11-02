// The host's vulkan-1.dll for Windows modules (vkd3d-proton loads it with
// LoadLibrary and asks GetProcAddress only for vkGetInstanceProcAddr). Every
// command resolves to its Windows x64 adapter in vulkan_abi.c, after the
// driver's own function is stored in the adapter's slot.
#include "Radv.h"
#include "VulkanAbi.h"
#include "../guest_abi.h"
#include "../win32/Win32.h"
#include <cstring>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using azimuth::win32::log;

namespace {
using GetProc=void*(*)(void*,const char*);
using EnumerateInstance=int32_t(*)(const char*,uint32_t*,void*);
using EnumerateDevice=int32_t(*)(void*,const char*,uint32_t*,void*);
struct ExtensionProperties {char name[256];uint32_t specVersion;};   // VkExtensionProperties
static_assert(sizeof(ExtensionProperties)==260,"VkExtensionProperties");
constexpr int32_t Success=0,Incomplete=5,ErrorInitializationFailed=-3;

// Extensions whose create infos or commands hand the driver callbacks it
// would call with the System V convention: not offered, and their commands
// resolve to null.
const char* const HiddenExtensions[]={"VK_EXT_debug_utils","VK_EXT_debug_report","VK_EXT_device_memory_report"};
const char* const HiddenCommands[]={"vkCreateDebugUtilsMessengerEXT","vkCreateDebugReportCallbackEXT"};

// Instance extensions the host offers when the driver lacks them:
// VK_KHR_win32_surface, which vkd3d-proton requires, and VK_KHR_surface under
// it. Their commands are the host's own; the driver never sees them.
struct Provided {const char* extension;uint32_t version;};
const Provided ProvidedExtensions[]={{"VK_KHR_surface",25},{"VK_KHR_win32_surface",6}};
// The driver's own instance extensions, from the last enumeration.
std::set<std::string> driverInstanceNames;
// Surface extensions the host enables on the application's instance when the
// driver has them: the game's swap chain is presented on one of their
// surfaces (the driver's WSI shows it through VideoOut).
const char* const PresentExtensions[]={"VK_KHR_display","VK_EXT_headless_surface"};
const char* const HostCommands[]={"vkCreateWin32SurfaceKHR","vkGetPhysicalDeviceWin32PresentationSupportKHR"};

std::mutex lock;
GetProc driverDeviceProcAddr;
EnumerateInstance driverInstanceExtensions;
EnumerateDevice driverDeviceExtensions;
using CreateInstance=int32_t(*)(const VkInstanceCreateInfo*,const void*,VkInstance*);
CreateInstance driverCreateInstance;
std::set<std::string> reported;

GetProc driver(){return reinterpret_cast<GetProc>(azimuth::gpu::driverInstanceProcAddr());}
const std::unordered_map<std::string,const GuestVulkanCommand*>& commands() {
    static auto table=[] {
        std::unordered_map<std::string,const GuestVulkanCommand*> map;
        for(auto c=guestVulkanCommands;c->name;++c)map.emplace(c->name,c);
        return map;
    }();
    return table;
}
const void* exported(const char* name) {
    for(auto e=guestExports;e->name;++e)
        if(!std::strcmp(e->library,"vulkan_1")&&!std::strcmp(e->name,name))return e->address;
    return nullptr;
}
void reportOnce(const std::string& text){if(reported.insert(text).second)log(text);}
bool hidden(const char* name) {
    for(auto h:HiddenCommands)if(!std::strcmp(h,name))return true;
    return false;
}

// `device`: resolved through vkGetDeviceProcAddr on `object`, else through
// vkGetInstanceProcAddr (object = instance or null).
void* resolve(void* object,const char* name,bool device) {
    if(!name)return nullptr;
    GetProc instanceProc=driver();
    if(!instanceProc)return nullptr;
    std::lock_guard<std::mutex> guard(lock);
    // The driver's vkGetDeviceProcAddr is one function for every device; it
    // needs an instance to be looked up.
    if(!driverDeviceProcAddr&&object&&!device)
        driverDeviceProcAddr=reinterpret_cast<GetProc>(instanceProc(object,"vkGetDeviceProcAddr"));
    if(!std::strcmp(name,"vkGetInstanceProcAddr")||!std::strcmp(name,"vkGetDeviceProcAddr"))
        return const_cast<void*>(exported(name));
    if(hidden(name))return nullptr;
    for(auto h:HostCommands)if(!std::strcmp(h,name))return const_cast<void*>(commands().at(name)->adapter);
    auto found=commands().find(name);
    if(found==commands().end()) {
        if(instanceProc(device?nullptr:object,name))reportOnce(std::string("Vulkan: o driver tem ")+name+", mas o host nao tem adaptador");
        return nullptr;
    }
    auto command=found->second;
    void* function=nullptr;
    if(device)function=driverDeviceProcAddr?driverDeviceProcAddr(object,name):nullptr;
    else function=instanceProc(object,name);
    if(!function)return nullptr;
    if(!command->driver) {
        // Hand-written adapters (extension lists) call the host, which calls these.
        if(!std::strcmp(name,"vkCreateInstance"))driverCreateInstance=reinterpret_cast<CreateInstance>(function);
        else if(!std::strcmp(name,"vkEnumerateInstanceExtensionProperties"))driverInstanceExtensions=reinterpret_cast<EnumerateInstance>(function);
        else if(!std::strcmp(name,"vkEnumerateDeviceExtensionProperties"))driverDeviceExtensions=reinterpret_cast<EnumerateDevice>(function);
    } else *command->driver=function;
    return const_cast<void*>(command->adapter);
}

bool hiddenExtension(const char* name) {
    for(auto h:HiddenExtensions)if(!std::strncmp(h,name,256))return true;
    return false;
}
// The driver's list without the hidden extensions and, for the instance,
// with the ones the host provides, in the Vulkan two-call form.
template<class Call>int32_t extensions(Call call,uint32_t* count,void* out,bool instance) {
    if(!count)return ErrorInitializationFailed;
    std::vector<ExtensionProperties> all;int32_t result;
    do {
        uint32_t total=0;
        if((result=call(&total,nullptr))!=Success)return result;
        all.resize(total);
        result=call(&total,all.data());
        all.resize(total);
    } while(result==Incomplete);
    if(result!=Success)return result;
    std::vector<ExtensionProperties> shown;
    for(auto& e:all)if(!hiddenExtension(e.name))shown.push_back(e);
    if(instance) {
        std::lock_guard<std::mutex> guard(lock);
        for(auto& e:all)driverInstanceNames.insert(e.name);
        for(auto& p:ProvidedExtensions) {
            if(driverInstanceNames.count(p.extension))continue;
            ExtensionProperties added{};std::strncpy(added.name,p.extension,sizeof added.name-1);added.specVersion=p.version;
            shown.push_back(added);
        }
    }
    if(!out){*count=uint32_t(shown.size());return Success;}
    uint32_t copied=*count<shown.size()?*count:uint32_t(shown.size());
    std::memcpy(out,shown.data(),copied*sizeof(ExtensionProperties));
    *count=copied;
    return copied<shown.size()?Incomplete:Success;
}
}

extern "C" void* win_vkGetInstanceProcAddr(void* instance,const char* name){return resolve(instance,name,false);}
extern "C" void* win_vkGetDeviceProcAddr(void* device,const char* name){return resolve(device,name,true);}

extern "C" const void* hostVulkanAllocator(const void* allocator) {
    if(allocator) {
        std::lock_guard<std::mutex> guard(lock);
        reportOnce("Vulkan: VkAllocationCallbacks do chamador ignorado (callbacks na convencao do Windows); o driver usa a propria alocacao");
    }
    return nullptr;
}
extern "C" int32_t hostVulkanInstanceExtensions(const char* layer,uint32_t* count,void* properties) {
    auto call=driverInstanceExtensions;
    if(!call)return ErrorInitializationFailed;
    return extensions([&](uint32_t* n,void* p){return call(layer,n,p);},count,properties,!layer);
}
extern "C" int32_t hostVulkanDeviceExtensions(void* physicalDevice,const char* layer,uint32_t* count,void* properties) {
    auto call=driverDeviceExtensions;
    if(!call)return ErrorInitializationFailed;
    return extensions([&](uint32_t* n,void* p){return call(physicalDevice,layer,n,p);},count,properties,false);
}

// The application's create info with the host's own extensions taken out.
extern "C" int32_t hostVulkanCreateInstance(const void* info,void* instance) {
    auto call=driverCreateInstance;
    if(!call||!info||!instance)return ErrorInitializationFailed;
    VkInstanceCreateInfo copy=*static_cast<const VkInstanceCreateInfo*>(info);
    std::vector<const char*> names;std::string list,provided;
    for(uint32_t n=0;n<copy.enabledExtensionCount;++n) {
        auto name=copy.ppEnabledExtensionNames[n];
        bool host=false;
        {
            std::lock_guard<std::mutex> guard(lock);
            for(auto& p:ProvidedExtensions)host=host||(!std::strcmp(p.extension,name)&&!driverInstanceNames.count(name));
        }
        (host?provided:list)+=std::string(" ")+name;
        if(!host)names.push_back(name);
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        for(auto extra:PresentExtensions) {
            bool asked=false;
            for(auto n:names)asked=asked||!std::strcmp(n,extra);
            if(!asked&&driverInstanceNames.count(extra)){names.push_back(extra);list+=std::string(" ")+extra+" (para apresentar)";}
        }
    }
    copy.enabledExtensionCount=uint32_t(names.size());
    copy.ppEnabledExtensionNames=names.empty()?nullptr:names.data();
    int32_t result=call(&copy,nullptr,static_cast<VkInstance*>(instance));
    log("Vulkan: vkCreateInstance "+std::to_string(result)+"; extensoes do driver:"+(list.empty()?" nenhuma":list)+
        (provided.empty()?"":"; do host:"+provided));
    return result;
}
// The surface the game's swap chain presents on (title 0.93): a display plane
// surface on the driver's display when it is the VideoOut one, else a headless
// surface. The choice and what the driver offered are logged.
extern "C" int32_t hostVulkanPresentSurface(void* instance,void* physicalDevice,uint64_t* surface) {
    if(!surface)return ErrorInitializationFailed;
    *surface=0;
    auto get=driver();
    if(!get||!instance)return ErrorInitializationFailed;
    auto proc=[&](const char* name){return get(instance,name);};
    auto vkInstance=static_cast<VkInstance>(instance);
    auto physical=static_cast<VkPhysicalDevice>(physicalDevice);
    auto displays=reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPropertiesKHR>(proc("vkGetPhysicalDeviceDisplayPropertiesKHR"));
    auto modes=reinterpret_cast<PFN_vkGetDisplayModePropertiesKHR>(proc("vkGetDisplayModePropertiesKHR"));
    auto planes=reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR>(proc("vkGetPhysicalDeviceDisplayPlanePropertiesKHR"));
    auto createDisplay=reinterpret_cast<PFN_vkCreateDisplayPlaneSurfaceKHR>(proc("vkCreateDisplayPlaneSurfaceKHR"));
    auto createHeadless=reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(proc("vkCreateHeadlessSurfaceEXT"));
    std::string report="Vulkan: superficie para o swap chain;";
    if(displays&&modes&&planes&&createDisplay&&physical) {
        uint32_t count=0;displays(physical,&count,nullptr);
        std::vector<VkDisplayPropertiesKHR> list(count);
        if(count)displays(physical,&count,list.data());
        report+=" "+std::to_string(count)+" tela(s):";
        for(auto& d:list)report+=std::string(" \"")+(d.displayName?d.displayName:"?")+"\" "+std::to_string(d.physicalResolution.width)+"x"+std::to_string(d.physicalResolution.height);
        for(auto& d:list) {
            if(!d.displayName||!std::strstr(d.displayName,"VideoOut"))continue;
            uint32_t modeCount=0;modes(physical,d.display,&modeCount,nullptr);
            std::vector<VkDisplayModePropertiesKHR> modeList(modeCount);
            if(modeCount)modes(physical,d.display,&modeCount,modeList.data());
            if(modeList.empty()){report+="; tela sem modos";break;}
            // The mode at the display's own resolution, else the first.
            auto mode=modeList[0];
            for(auto& m:modeList)if(m.parameters.visibleRegion.width==d.physicalResolution.width&&m.parameters.visibleRegion.height==d.physicalResolution.height){mode=m;break;}
            uint32_t planeCount=0;planes(physical,&planeCount,nullptr);
            std::vector<VkDisplayPlanePropertiesKHR> planeList(planeCount);
            if(planeCount)planes(physical,&planeCount,planeList.data());
            VkDisplaySurfaceCreateInfoKHR info{};
            info.sType=VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
            info.displayMode=mode.displayMode;info.planeIndex=0;
            info.planeStackIndex=planeList.empty()?0:planeList[0].currentStackIndex;
            info.transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;info.globalAlpha=1.0f;info.alphaMode=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
            info.imageExtent=mode.parameters.visibleRegion;
            VkSurfaceKHR created=VK_NULL_HANDLE;
            int32_t result=createDisplay(vkInstance,&info,nullptr,&created);
            log(report+"; vkCreateDisplayPlaneSurfaceKHR "+std::to_string(result)+" no modo "+std::to_string(info.imageExtent.width)+"x"+
                std::to_string(info.imageExtent.height)+" a "+std::to_string(mode.parameters.refreshRate)+" mHz");
            if(result==Success){*surface=uint64_t(created);return Success;}
            break;
        }
    }else report+=" sem VK_KHR_display;";
    if(!createHeadless){log(report+" sem VK_EXT_headless_surface; nenhuma superficie");return ErrorInitializationFailed;}
    VkHeadlessSurfaceCreateInfoEXT info{};info.sType=VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    VkSurfaceKHR created=VK_NULL_HANDLE;
    int32_t result=createHeadless(vkInstance,&info,nullptr,&created);
    log(report+" vkCreateHeadlessSurfaceEXT "+std::to_string(result));
    if(result==Success)*surface=uint64_t(created);
    return result;
}
// The game's swap chain asks the host's surface factory (SwapChain.cpp), not
// vkCreateWin32SurfaceKHR, for its surface.
extern "C" int32_t hostVulkanCreateWin32Surface(void*,const void*,uint64_t* surface) {
    if(surface)*surface=0;
    std::lock_guard<std::mutex> guard(lock);
    reportOnce("Vulkan: vkCreateWin32SurfaceKHR nao suportado; o swap chain do jogo usa a superficie do host");
    return ErrorInitializationFailed;
}
extern "C" uint32_t hostVulkanWin32PresentationSupport(void*,uint32_t){return 1;}
extern "C" void hostVulkanError(const char* command,int32_t result) {
    std::lock_guard<std::mutex> guard(lock);
    reportOnce(std::string("Vulkan: ")+command+" devolveu "+std::to_string(result)+" (erro do driver)");
}
