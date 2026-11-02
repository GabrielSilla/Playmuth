// The host's DXGI (Dxgi.h): a factory and one adapter, described from the
// Vulkan driver vkd3d-proton will use, so that D3D12CreateDevice finds the
// same device. The vtables are in dxgi_abi.c.
#include "Dxgi.h"
#include "DxgiAbi.h"
#include "Radv.h"
#include "../win32/Win32.h"
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

using azimuth::win32::log;

namespace {
constexpr int32_t Ok=0,NotImplemented=int32_t(0x80004001),NoInterface=int32_t(0x80004002),Pointer=int32_t(0x80004003),
                  InvalidArgument=int32_t(0x80070057),NotFound=int32_t(0x887a0002),Unsupported=int32_t(0x887a0004),
                  MoreData=int32_t(0x887a0003);
struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
bool operator<(const Guid& x,const Guid& y){return std::memcmp(&x,&y,sizeof x)<0;}
constexpr Guid Unknown{0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
constexpr Guid DxgiObject{0xaec22fb8,0x76f3,0x4639,{0x9b,0xe0,0x28,0xeb,0x43,0xa6,0x7a,0x2e}};
constexpr Guid FactoryIds[]={
    {0x7b7166ec,0x21c7,0x44ae,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}},
    {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}},
    {0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}},
    {0x25483823,0xcd46,0x4c7d,{0x86,0xca,0x47,0xaa,0x95,0xb8,0x37,0xbd}},
    {0x1bc6ea02,0xef36,0x464f,{0xbf,0x0c,0x21,0xca,0x39,0xe5,0x16,0x8a}},
    {0x7632e1f5,0xee65,0x4dca,{0x87,0xfd,0x84,0xcd,0x75,0xf8,0x83,0x8d}},
    {0xc1b6694f,0xff09,0x44a9,{0xb0,0x3c,0x77,0x90,0x0a,0x0a,0x1d,0x17}},
};
constexpr Guid AdapterIds[]={
    {0x2411e7e1,0x12ac,0x4ccf,{0xbd,0x14,0x97,0x98,0xe8,0x53,0x4d,0xc0}},
    {0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05}},
    {0x0aa1ae0a,0xfa0e,0x4b84,{0x86,0x44,0xe0,0x5f,0xf8,0xe5,0xac,0xb5}},
    {0x645967a4,0x1392,0x4310,{0xa7,0x98,0x80,0x53,0xce,0x3e,0x93,0xfd}},
    {0x3c8d99d1,0x4fbf,0x4181,{0xa8,0x2c,0xaf,0x66,0xbf,0x7b,0xd2,0x4e}},
};
// The adapter's LUID: any value unique in this process ("PS5").
constexpr uint32_t LuidLow=0x00355350;
std::string text(const Guid& g) {
    char s[40];
    std::snprintf(s,sizeof s,"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.a,g.b,g.c,g.d[0],g.d[1],g.d[2],g.d[3],g.d[4],g.d[5],g.d[6],g.d[7]);
    return s;
}
std::mutex lock;
std::set<std::string> reported;
void reportOnce(const std::string& line){std::lock_guard<std::mutex> guard(lock);if(reported.insert(line).second)log(line);}

// The Vulkan device the adapter describes.
struct Device {bool found=false;std::u16string name;uint32_t vendor=0,device=0;uint64_t local=0,shared=0;};
const Device& driverDevice() {
    static Device d=[] {
        Device out;
        auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(azimuth::gpu::driverInstanceProcAddr());
        if(!gipa)return out;
        auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr,"Azimuth_PS5 DXGI",1,"Azimuth_PS5",1,VK_API_VERSION_1_1};
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr,0,&app,0,nullptr,0,nullptr};
        VkInstance instance=VK_NULL_HANDLE;
        if(!create||create(&info,nullptr,&instance)!=VK_SUCCESS){log("DXGI: vkCreateInstance falhou ao descrever o adaptador");return out;}
        auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance,"vkEnumeratePhysicalDevices"));
        auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance,"vkGetPhysicalDeviceProperties"));
        auto memory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));
        auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"));
        uint32_t count=1;VkPhysicalDevice physical=VK_NULL_HANDLE;
        VkResult r=enumerate?enumerate(instance,&count,&physical):VK_ERROR_INITIALIZATION_FAILED;
        if((r==VK_SUCCESS||r==VK_INCOMPLETE)&&physical&&properties&&memory) {
            VkPhysicalDeviceProperties p{};properties(physical,&p);
            VkPhysicalDeviceMemoryProperties m{};memory(physical,&m);
            out.found=true;out.vendor=p.vendorID;out.device=p.deviceID;
            for(auto c=p.deviceName;*c&&out.name.size()<127;++c)out.name+=char16_t(uint8_t(*c));
            for(uint32_t n=0;n<m.memoryHeapCount;++n)
                (m.memoryHeaps[n].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT?out.local:out.shared)+=m.memoryHeaps[n].size;
            log("DXGI: adaptador 0 = \""+std::string(p.deviceName)+"\", vendor "+std::to_string(p.vendorID)+", device "+
                std::to_string(p.deviceID)+", "+std::to_string(out.local>>20)+" MiB locais, "+std::to_string(out.shared>>20)+" MiB compartilhados");
        }else log("DXGI: nenhum dispositivo Vulkan para descrever");
        if(destroy)destroy(instance,nullptr);
        return out;
    }();
    return d;
}

// The COM layout: the vtable pointer first (no C++ virtual functions here).
struct Object {
    const void* const* vtable;
    std::atomic<uint32_t> references{1};
    bool factory;
    std::map<Guid,std::vector<uint8_t>> privateData;
};
struct Factory:Object {void* window=nullptr;};
struct Adapter:Object {Factory* parent=nullptr;};
Object* object(void* self){return static_cast<Object*>(self);}
const Guid& id(const void* p){return *static_cast<const Guid*>(p);}
bool same(const void* a,const Guid& b){return !std::memcmp(a,&b,sizeof b);}

Factory* newFactory() {
    auto f=new Factory;f->vtable=guestDxgiFactoryVtable;f->factory=true;
    return f;
}
Adapter* newAdapter(Factory* parent) {
    auto a=new Adapter;a->vtable=guestDxgiAdapterVtable;a->factory=false;a->parent=parent;
    ++parent->references;
    return a;
}
void destroy(Object* o) {
    if(o->factory)delete static_cast<Factory*>(o);
    else delete static_cast<Adapter*>(o);
}
int32_t queryInterface(Object* o,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    bool known=same(want,Unknown)||same(want,DxgiObject);
    if(o->factory)for(auto& g:FactoryIds)known=known||same(want,g);
    else for(auto& g:AdapterIds)known=known||same(want,g);
    if(!known) {
        reportOnce(std::string("DXGI: ")+(o->factory?"fabrica":"adaptador")+" sem a interface "+text(id(want)));
        return NoInterface;
    }
    ++o->references;*out=o;return Ok;
}
// The adapter at `index` of `factory`, as the interface `want`.
int32_t enumerate(Factory* factory,uint32_t index,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(index>0||!driverDevice().found)return NotFound;
    auto adapter=newAdapter(factory);
    int32_t result=queryInterface(adapter,want,out);
    --adapter->references;   // the query took the reference handed out
    if(result!=Ok){--factory->references;delete adapter;}
    return result;
}
// DXGI_ADAPTER_DESC (304 bytes), then DESC1's Flags at 304 and DESC2/3's
// preemption granularities at 308 and 312.
void describe(void* desc,size_t size) {
    auto& d=driverDevice();
    std::memset(desc,0,size);
    auto p=static_cast<uint8_t*>(desc);
    std::memcpy(p,d.name.data(),d.name.size()*2);
    uint32_t ids[4]={d.vendor,d.device,0,0};std::memcpy(p+256,ids,16);
    uint64_t memory[3]={d.local,0,d.shared};std::memcpy(p+272,memory,24);
    uint32_t luid[2]={LuidLow,0};std::memcpy(p+296,luid,8);
}
}

extern "C" {
int32_t dxgiMissing(const char* object,uint32_t slot) {
    reportOnce(std::string("DXGI: metodo ")+object+" #"+std::to_string(slot)+" ainda sem implementacao (E_NOTIMPL)");
    return NotImplemented;
}
int32_t dxgi_QueryInterface(void* self,const void* want,void** out){return queryInterface(object(self),want,out);}
uint32_t dxgi_AddRef(void* self){return ++object(self)->references;}
uint32_t dxgi_Release(void* self) {
    auto o=object(self);
    uint32_t left=--o->references;
    if(!left) {
        Factory* parent=o->factory?nullptr:static_cast<Adapter*>(o)->parent;
        destroy(o);
        if(parent)dxgi_Release(parent);
    }
    return left;
}
int32_t dxgi_SetPrivateData(void* self,const void* key,uint32_t size,const void* data) {
    if(!key)return InvalidArgument;
    std::lock_guard<std::mutex> guard(lock);
    auto& map=object(self)->privateData;
    if(!data||!size){map.erase(id(key));return Ok;}
    auto bytes=static_cast<const uint8_t*>(data);
    map[id(key)].assign(bytes,bytes+size);
    return Ok;
}
// The interface is kept as its pointer, without a reference.
int32_t dxgi_SetPrivateDataInterface(void* self,const void* key,void* unknown) {
    return dxgi_SetPrivateData(self,key,unknown?sizeof unknown:0,unknown?&unknown:nullptr);
}
int32_t dxgi_GetPrivateData(void* self,const void* key,uint32_t* size,void* data) {
    if(!key||!size)return InvalidArgument;
    std::lock_guard<std::mutex> guard(lock);
    auto& map=object(self)->privateData;
    auto found=map.find(id(key));
    if(found==map.end()){*size=0;return NotFound;}
    auto need=uint32_t(found->second.size());
    if(!data){*size=need;return Ok;}
    if(*size<need){*size=need;return MoreData;}
    std::memcpy(data,found->second.data(),need);*size=need;return Ok;
}
int32_t dxgi_GetParent(void* self,const void* want,void** parent) {
    auto o=object(self);
    if(!o->factory)if(auto f=static_cast<Adapter*>(o)->parent)return queryInterface(f,want,parent);
    if(parent)*parent=nullptr;
    return NoInterface;
}

int32_t dxgi_EnumAdapters(void* self,uint32_t index,void** adapter){return enumerate(static_cast<Factory*>(object(self)),index,&AdapterIds[0],adapter);}
int32_t dxgi_EnumAdapters1(void* self,uint32_t index,void** adapter){return enumerate(static_cast<Factory*>(object(self)),index,&AdapterIds[1],adapter);}
int32_t dxgi_EnumAdapterByGpuPreference(void* self,uint32_t index,uint32_t,const void* want,void** adapter) {
    return enumerate(static_cast<Factory*>(object(self)),index,want,adapter);
}
int32_t dxgi_EnumAdapterByLuid(void* self,uint64_t luid,const void* want,void** adapter) {
    if(luid!=LuidLow){if(adapter)*adapter=nullptr;return InvalidArgument;}
    return enumerate(static_cast<Factory*>(object(self)),0,want,adapter);
}
int32_t dxgi_MakeWindowAssociation(void* self,void* window,uint32_t){static_cast<Factory*>(object(self))->window=window;return Ok;}
int32_t dxgi_GetWindowAssociation(void* self,void** window) {
    if(!window)return InvalidArgument;
    *window=static_cast<Factory*>(object(self))->window;return Ok;
}
int32_t dxgi_IsCurrent(void*){return 1;}
int32_t dxgi_IsWindowedStereoEnabled(void*){return 0;}
uint32_t dxgi_GetCreationFlags(void*){return 0;}
// DXGI_FEATURE_PRESENT_ALLOW_TEARING (0): a BOOL, false.
int32_t dxgi_CheckFeatureSupport(void*,uint32_t feature,void* data,uint32_t size) {
    if(feature!=0||!data||size!=4)return InvalidArgument;
    *static_cast<int32_t*>(data)=0;return Ok;
}

int32_t dxgi_EnumOutputs(void*,uint32_t index,void** output) {
    if(output)*output=nullptr;
    reportOnce("DXGI: EnumOutputs("+std::to_string(index)+"): o host ainda nao tem saidas (DXGI_ERROR_NOT_FOUND)");
    return NotFound;
}
int32_t dxgi_GetDesc(void*,void* desc){if(!desc)return InvalidArgument;describe(desc,304);return Ok;}
int32_t dxgi_GetDesc1(void*,void* desc){if(!desc)return InvalidArgument;describe(desc,312);return Ok;}
int32_t dxgi_GetDesc2(void*,void* desc){if(!desc)return InvalidArgument;describe(desc,320);return Ok;}
int32_t dxgi_GetDesc3(void*,void* desc){if(!desc)return InvalidArgument;describe(desc,320);return Ok;}
int32_t dxgi_CheckInterfaceSupport(void*,const void*,int64_t*){return Unsupported;}
// DXGI_QUERY_VIDEO_MEMORY_INFO: Budget, CurrentUsage, AvailableForReservation,
// CurrentReservation. Group 0 is local memory, 1 the rest.
int32_t dxgi_QueryVideoMemoryInfo(void*,uint32_t node,uint32_t group,void* info) {
    if(node!=0||group>1||!info)return InvalidArgument;
    auto& d=driverDevice();
    uint64_t budget=group?d.shared:d.local;
    uint64_t values[4]={budget,0,budget/2,0};
    std::memcpy(info,values,sizeof values);
    return Ok;
}
int32_t dxgi_SetVideoMemoryReservation(void*,uint32_t node,uint32_t group,uint64_t){return node==0&&group<=1?Ok:InvalidArgument;}
}

namespace azimuth::gpu {
int32_t createDxgiFactory(const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    auto factory=newFactory();
    int32_t result=queryInterface(factory,want,out);
    --factory->references;
    if(result!=Ok)delete factory;
    else log("DXGI: fabrica criada ("+text(id(want))+")");
    return result;
}
}
