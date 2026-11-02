// The host's DXGI swap chain (title 0.93): IDXGISwapChain4 over vkd3d-proton's
// own IDXGIVkSwapChain, which a D3D12 command queue builds through
// IDXGIVkSwapChainFactory (as DXVK's dxgi does). Its surface comes from the
// host's IDXGIVkSurfaceFactory: a surface of the driver's WSI, which shows the
// swap chain through VideoOut; the launcher's own VideoOut port is closed
// first. The vtables are in dxgi_abi.c.
#include "DxgiAbi.h"
#include "FrameIntervals.h"
#include "FrameWindow.h"
#include "../gfx/VideoOut.h"
#include "../guest_abi.h"
#include "../win32/Win32.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace azimuth {uint32_t currentGuestThreadId();}
using azimuth::win32::log;

extern "C" {
int32_t hostVulkanPresentSurface(void* instance,void* physicalDevice,uint64_t* surface);
int32_t dxgi_QueryInterface(void* self,const void* want,void** out);
uint32_t dxgi_AddRef(void* self);
uint32_t dxgi_Release(void* self);
int32_t dxgi_Present1(void* self,uint32_t interval,uint32_t flags,const void* parameters);
}

namespace {
constexpr int32_t Ok=0,NoInterface=int32_t(0x80004002),Pointer=int32_t(0x80004003),
                  InvalidArgument=int32_t(0x80070057),InvalidCall=int32_t(0x887a0001),NotFound=int32_t(0x887a0002),
                  StatisticsDisjoint=int32_t(0x887a000b);
struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
bool operator<(const Guid& x,const Guid& y){return std::memcmp(&x,&y,sizeof x)<0;}
constexpr Guid Unknown{0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
constexpr Guid SurfaceFactoryId{0x1e7895a1,0x1bc3,0x4f9c,{0xa6,0x70,0x29,0x0a,0x4b,0xc9,0x58,0x1a}};
constexpr Guid SwapChainFactoryId{0xe7d6c3ca,0x23a0,0x4e08,{0x9f,0x2f,0xea,0x52,0x31,0xdf,0x66,0x33}};
constexpr Guid VkSwapChain1Id{0x785326d4,0xb77b,0x4826,{0xae,0x70,0x8d,0x08,0x30,0x8e,0xe6,0xd1}};
constexpr Guid ChainIds[]={
    {0xaec22fb8,0x76f3,0x4639,{0x9b,0xe0,0x28,0xeb,0x43,0xa6,0x7a,0x2e}},   // IDXGIObject
    {0x3d3e0379,0xf9de,0x4d58,{0xbb,0x6c,0x18,0xd6,0x29,0x92,0xf1,0xa6}},   // IDXGIDeviceSubObject
    {0x310d36a0,0xd2e7,0x4c0a,{0xaa,0x04,0x6a,0x9d,0x23,0xb8,0x88,0x6a}},   // IDXGISwapChain
    {0x790a45f7,0x0d42,0x4876,{0x98,0x3a,0x0a,0x55,0xcf,0xe6,0xf4,0xaa}},   // IDXGISwapChain1
    {0xa8be2ac4,0x199f,0x4946,{0xb3,0x31,0x79,0x59,0x9f,0xb9,0x8d,0xe7}},   // IDXGISwapChain2
    {0x94d99bdb,0xf1f8,0x4ab0,{0xb2,0x36,0x7d,0xa0,0x17,0x0e,0xda,0xb1}},   // IDXGISwapChain3
    {0x3d585d5a,0xbd4a,0x489e,{0xb1,0xf4,0x3d,0xbc,0xb6,0x45,0x2f,0xfb}},   // IDXGISwapChain4
};
bool same(const void* a,const Guid& b){return !std::memcmp(a,&b,sizeof b);}
const Guid& id(const void* p){return *static_cast<const Guid*>(p);}
std::string text(const void* p) {
    Guid g;std::memcpy(&g,p,sizeof g);char s[40];
    std::snprintf(s,sizeof s,"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.a,g.b,g.c,g.d[0],g.d[1],g.d[2],g.d[3],g.d[4],g.d[5],g.d[6],g.d[7]);
    return s;
}
std::mutex lock;
std::set<std::string> reported;
void reportOnce(const std::string& line){std::lock_guard<std::mutex> guard(lock);if(reported.insert(line).second)log(line);}

// A method of a Windows COM object (vkd3d-proton's), by vtable slot.
template<class... A>uint64_t call(void* object,unsigned slot,A... args) {
    auto vtable=*static_cast<const void* const* const*>(object);
    uint64_t a[8]={reinterpret_cast<uint64_t>(object),uint64_t(args)...};
    return guestCall8(vtable[slot],a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
}
template<class T>uint64_t arg(T* p){return reinterpret_cast<uint64_t>(p);}
// IDXGIVkSwapChain's slots (vkd3d_swapchain_factory.idl).
enum VkSlot {VkRelease=2,VkGetDesc=3,VkGetDevice=5,VkGetImage=6,VkGetImageIndex=7,VkGetFrameLatency=8,VkGetFrameLatencyEvent=9,
             VkChangeProperties=10,VkSetFrameLatency=13,VkPresent=14,VkCheckColorSpaceSupport=15,VkSetColorSpace=16,
             VkSetHDRMetaData=17,VkGetLastPresentCount=18};

struct SurfaceFactory {const void* const* vtable;std::atomic<uint32_t> references{1};};
// DXGI_SWAP_CHAIN_DESC1 (48 bytes): Width, Height, Format, Stereo, SampleDesc
// (Count, Quality), BufferUsage, BufferCount, Scaling, SwapEffect, AlphaMode, Flags.
struct Desc1 {uint32_t width,height,format,stereo,sampleCount,sampleQuality,usage,count,scaling,effect,alpha,flags;};
static_assert(sizeof(Desc1)==48,"DXGI_SWAP_CHAIN_DESC1");
struct Chain {
    const void* const* vtable;std::atomic<uint32_t> references{1};
    void* vk=nullptr;          // IDXGIVkSwapChain
    void* vk1=nullptr;         // IDXGIVkSwapChain1, when it has it
    void* factory=nullptr;     // the host's IDXGIFactory (a reference)
    void* window=nullptr;
    bool fullscreen=true;
    uint32_t sourceWidth=0,sourceHeight=0;
    uint64_t presents=0;
    std::atomic<int64_t> lastPresentNs{0},lastPresentDurationNs{0},lastSlowLogNs{0};
    std::map<Guid,std::vector<uint8_t>> privateData;
};
Chain* chain(void* self){return static_cast<Chain*>(self);}

// The window's client size for a zero width or height, as DXGI does.
void fillSize(void* window,Desc1& d) {
    if(d.width&&d.height)return;
    int w=0,h=0;
    if(!window||!azimuth::win32::windowClientSize(window,w,h)||w<=0||h<=0){w=1920;h=1080;}
    if(!d.width)d.width=uint32_t(w);
    if(!d.height)d.height=uint32_t(h);
}
int32_t create(void* factory,void* device,void* window,const Desc1& asked,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!device)return InvalidArgument;
    Desc1 desc=asked;fillSize(window,desc);
    void* vkFactory=nullptr;
    int32_t result=int32_t(call(device,0,arg(&SwapChainFactoryId),arg(&vkFactory)));
    if(result<0||!vkFactory) {
        log("DXGI: o dispositivo do swap chain nao e uma fila do vkd3d-proton (IDXGIVkSwapChainFactory: "+std::to_string(result)+")");
        return InvalidCall;
    }
    auto surfaces=new SurfaceFactory;surfaces->vtable=guestDxgiSurfaceFactoryVtable;
    void* vk=nullptr;
    result=int32_t(call(vkFactory,3,arg(surfaces),arg(&desc),arg(&vk)));
    call(vkFactory,2);
    if(!--surfaces->references)delete surfaces;
    log("DXGI: swap chain "+std::to_string(desc.width)+"x"+std::to_string(desc.height)+", formato "+std::to_string(desc.format)+", "+
        std::to_string(desc.count)+" buffers, efeito "+std::to_string(desc.effect)+", flags "+std::to_string(desc.flags)+": "+
        (result<0?"falhou ("+std::to_string(result)+")":"criado"));
    if(result<0||!vk)return result<0?result:InvalidCall;
    auto c=new Chain;c->vtable=guestDxgiSwapChainVtable;c->vk=vk;c->factory=factory;c->window=window;
    dxgi_AddRef(factory);
    void* vk1=nullptr;
    if(int32_t(call(vk,0,arg(&VkSwapChain1Id),arg(&vk1)))>=0)c->vk1=vk1;
    *out=c;return Ok;
}
Desc1 currentDesc(Chain* c){Desc1 d{};call(c->vk,VkGetDesc,arg(&d));return d;}
int32_t resize(Chain* c,uint32_t count,uint32_t width,uint32_t height,uint32_t format,uint32_t flags,const uint32_t* masks,void* const* queues) {
    Desc1 d=currentDesc(c);
    if(count)d.count=count;
    d.width=width;d.height=height;fillSize(c->window,d);
    if(format)d.format=format;
    d.flags=flags;
    int32_t result=int32_t(call(c->vk,VkChangeProperties,arg(&d),arg(masks),arg(queues)));
    log("DXGI: ResizeBuffers para "+std::to_string(d.width)+"x"+std::to_string(d.height)+", "+std::to_string(d.count)+" buffers: "+std::to_string(result));
    return result;
}
}

extern "C" {
// IDXGIFactory::CreateSwapChain: DXGI_SWAP_CHAIN_DESC (72 bytes) into DESC1.
int32_t dxgi_CreateSwapChain(void* self,void* device,const void* desc,void** out) {
    if(!desc)return InvalidArgument;
    auto p=static_cast<const uint8_t*>(desc);
    Desc1 d{};
    std::memcpy(&d.width,p,4);std::memcpy(&d.height,p+4,4);std::memcpy(&d.format,p+16,4);
    std::memcpy(&d.sampleCount,p+28,4);std::memcpy(&d.sampleQuality,p+32,4);
    std::memcpy(&d.usage,p+36,4);std::memcpy(&d.count,p+40,4);
    void* window;std::memcpy(&window,p+48,8);
    std::memcpy(&d.effect,p+60,4);std::memcpy(&d.flags,p+64,4);
    return create(self,device,window,d,out);
}
int32_t dxgi_CreateSwapChainForHwnd(void* self,void* device,void* window,const void* desc,const void*,void*,void** out) {
    if(!desc)return InvalidArgument;
    Desc1 d;std::memcpy(&d,desc,sizeof d);
    return create(self,device,window,d,out);
}

// IDXGIVkSurfaceFactory
int32_t dxgi_SurfaceQueryInterface(void* self,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    if(!same(want,Unknown)&&!same(want,SurfaceFactoryId))return NoInterface;
    ++static_cast<SurfaceFactory*>(self)->references;*out=self;return Ok;
}
uint32_t dxgi_SurfaceAddRef(void* self){return ++static_cast<SurfaceFactory*>(self)->references;}
uint32_t dxgi_SurfaceRelease(void* self) {
    auto f=static_cast<SurfaceFactory*>(self);
    uint32_t left=--f->references;
    if(!left)delete f;
    return left;
}
int32_t dxgi_CreateSurface(void*,void* instance,void* adapter,uint64_t* surface) {
    // The screen goes from the launcher to the game: the driver's WSI opens its own VideoOut port.
    azimuth::video::release();
    return hostVulkanPresentSurface(instance,adapter,surface);
}

// IDXGISwapChain4
int32_t dxgi_ChainQueryInterface(void* self,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    bool known=same(want,Unknown);
    for(auto& g:ChainIds)known=known||same(want,g);
    if(!known){reportOnce("DXGI: swap chain sem a interface "+text(want));return NoInterface;}
    ++chain(self)->references;*out=self;return Ok;
}
uint32_t dxgi_ChainAddRef(void* self){return ++chain(self)->references;}
uint32_t dxgi_ChainRelease(void* self) {
    auto c=chain(self);
    uint32_t left=--c->references;
    if(!left) {
        if(c->vk1)call(c->vk1,VkRelease);
        call(c->vk,VkRelease);
        dxgi_Release(c->factory);
        delete c;
    }
    return left;
}
int32_t dxgi_ChainSetPrivateData(void* self,const void* key,uint32_t size,const void* data) {
    if(!key)return InvalidArgument;
    std::lock_guard<std::mutex> guard(lock);
    auto& map=chain(self)->privateData;
    if(!data||!size){map.erase(id(key));return Ok;}
    map[id(key)].assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size);
    return Ok;
}
int32_t dxgi_ChainSetPrivateDataInterface(void*,const void*,void*){reportOnce("DXGI: SetPrivateDataInterface no swap chain ignorado");return Ok;}
int32_t dxgi_ChainGetPrivateData(void* self,const void* key,uint32_t* size,void* data) {
    if(!key||!size)return InvalidArgument;
    std::lock_guard<std::mutex> guard(lock);
    auto& map=chain(self)->privateData;
    auto found=map.find(id(key));
    if(found==map.end()){*size=0;return NotFound;}
    if(!data){*size=uint32_t(found->second.size());return Ok;}
    if(*size<found->second.size())return int32_t(0x887a0003);   // DXGI_ERROR_MORE_DATA
    std::memcpy(data,found->second.data(),found->second.size());*size=uint32_t(found->second.size());
    return Ok;
}
int32_t dxgi_ChainGetParent(void* self,const void* want,void** parent){return dxgi_QueryInterface(chain(self)->factory,want,parent);}
int32_t dxgi_ChainGetDevice(void* self,const void* want,void** device){return int32_t(call(chain(self)->vk,VkGetDevice,arg(want),arg(device)));}
int32_t dxgi_Present(void* self,uint32_t interval,uint32_t flags){return dxgi_Present1(self,interval,flags,nullptr);}
azimuth::gpu::FrameIntervals frameIntervals;
azimuth::gpu::FrameWindow frameWindow;
std::atomic<uint64_t> presentedFrames{0};   // every swap chain's presents, for the host's performance line
std::atomic<uint64_t> presentNanos{0};      // time spent inside Present
std::atomic<uint32_t> lastInterval{0},lastFlags{0};
int32_t dxgi_Present1(void* self,uint32_t interval,uint32_t flags,const void* parameters) {
    ++presentedFrames;lastInterval=interval;lastFlags=flags;
    auto began=std::chrono::steady_clock::now();
    struct Timed{std::chrono::steady_clock::time_point began;~Timed(){presentNanos+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-began).count());}} timed{began};
    auto c=chain(self);int64_t gap=0,previous=0,entry=azimuth::win32::fastMonotonicNs();
    if(!(flags&1u)){ // DXGI_PRESENT_TEST does not submit a frame.
        auto before=c->lastPresentNs.exchange(entry,std::memory_order_relaxed);
        if(before>0&&entry>before){gap=entry-before;frameIntervals.record(uint64_t(gap));}
        previous=c->lastPresentDurationNs.load(std::memory_order_relaxed);
        frameWindow.record(uint64_t(entry),uint64_t(gap));
    }
    int32_t result=int32_t(call(c->vk,VkPresent,interval,flags,arg(parameters)));
    auto end=azimuth::win32::fastMonotonicNs();
    if(!(flags&1u))c->lastPresentDurationNs.store(end-entry,std::memory_order_relaxed);
    if(gap>50000000){
        auto last=c->lastSlowLogNs.load(std::memory_order_relaxed);
        if(entry-last>=1000000000&&c->lastSlowLogNs.compare_exchange_strong(last,entry,std::memory_order_relaxed)){
            char line[320];std::snprintf(line,sizeof line,"Quadro lento: thread %u, fim intervalo mono ns %llu, intervalo %.2f ms, Present anterior %.2f ms, Present atual %.2f ms",
                azimuth::currentGuestThreadId(),static_cast<unsigned long long>(entry),double(gap)/1e6,double(previous)/1e6,double(end-entry)/1e6);
            log(line);
        }
    }
    if(++c->presents==1||result<0)reportOnce("DXGI: Present "+std::string(c->presents==1?"(primeiro) ":"")+"= "+std::to_string(result));
    return result;
}
int32_t dxgi_GetBuffer(void* self,uint32_t index,const void* want,void** buffer){return int32_t(call(chain(self)->vk,VkGetImage,index,arg(want),arg(buffer)));}
// The game always fills the screen; fullscreen state is only remembered.
int32_t dxgi_SetFullscreenState(void* self,int32_t fullscreen,void*){chain(self)->fullscreen=fullscreen!=0;return Ok;}
int32_t dxgi_GetFullscreenState(void* self,int32_t* fullscreen,void** output) {
    if(fullscreen)*fullscreen=chain(self)->fullscreen;
    if(output)*output=nullptr;
    return Ok;
}
// DXGI_SWAP_CHAIN_DESC from the current DESC1.
int32_t dxgi_ChainGetDesc(void* self,void* desc) {
    if(!desc)return InvalidArgument;
    auto c=chain(self);Desc1 d=currentDesc(c);
    auto p=static_cast<uint8_t*>(desc);std::memset(p,0,72);
    uint32_t windowed=!c->fullscreen;
    std::memcpy(p,&d.width,4);std::memcpy(p+4,&d.height,4);std::memcpy(p+16,&d.format,4);
    std::memcpy(p+28,&d.sampleCount,4);std::memcpy(p+32,&d.sampleQuality,4);
    std::memcpy(p+36,&d.usage,4);std::memcpy(p+40,&d.count,4);std::memcpy(p+48,&c->window,8);
    std::memcpy(p+56,&windowed,4);std::memcpy(p+60,&d.effect,4);std::memcpy(p+64,&d.flags,4);
    return Ok;
}
int32_t dxgi_ResizeBuffers(void* self,uint32_t count,uint32_t width,uint32_t height,uint32_t format,uint32_t flags) {
    return resize(chain(self),count,width,height,format,flags,nullptr,nullptr);
}
int32_t dxgi_ResizeBuffers1(void* self,uint32_t count,uint32_t width,uint32_t height,uint32_t format,uint32_t flags,const uint32_t* masks,void* const* queues) {
    return resize(chain(self),count,width,height,format,flags,masks,queues);
}
int32_t dxgi_ResizeTarget(void*,const void* mode){return mode?Ok:InvalidArgument;}
int32_t dxgi_GetContainingOutput(void*,void** output) {
    if(output)*output=nullptr;
    reportOnce("DXGI: GetContainingOutput sem saidas no host (DXGI_ERROR_NOT_FOUND)");
    return NotFound;
}
int32_t dxgi_GetFrameStatistics(void*,void*){return StatisticsDisjoint;}
int32_t dxgi_GetLastPresentCount(void* self,uint32_t* count) {
    if(!count)return InvalidArgument;
    auto c=chain(self);
    uint64_t last=c->presents;
    if(c->vk1)call(c->vk1,VkGetLastPresentCount,arg(&last));
    *count=uint32_t(last);return Ok;
}
int32_t dxgi_ChainGetDesc1(void* self,void* desc) {
    if(!desc)return InvalidArgument;
    Desc1 d=currentDesc(chain(self));std::memcpy(desc,&d,sizeof d);return Ok;
}
// DXGI_SWAP_CHAIN_FULLSCREEN_DESC: RefreshRate, ScanlineOrdering, Scaling, Windowed.
int32_t dxgi_GetFullscreenDesc(void* self,void* desc) {
    if(!desc)return InvalidArgument;
    uint32_t values[5]={60,1,0,0,uint32_t(!chain(self)->fullscreen)};
    std::memcpy(desc,values,sizeof values);return Ok;
}
int32_t dxgi_GetHwnd(void* self,void** window){if(!window)return InvalidArgument;*window=chain(self)->window;return Ok;}
int32_t dxgi_IsTemporaryMonoSupported(void*){return 0;}
int32_t dxgi_GetRestrictToOutput(void*,void** output){if(!output)return InvalidArgument;*output=nullptr;return Ok;}
int32_t dxgi_SetBackgroundColor(void*,const void*){return Ok;}
int32_t dxgi_GetBackgroundColor(void*,void* color){if(!color)return InvalidArgument;std::memset(color,0,16);return Ok;}
int32_t dxgi_SetRotation(void*,uint32_t rotation){return rotation<=1?Ok:InvalidArgument;}
int32_t dxgi_GetRotation(void*,uint32_t* rotation){if(!rotation)return InvalidArgument;*rotation=1;return Ok;}   // DXGI_MODE_ROTATION_IDENTITY
int32_t dxgi_SetSourceSize(void* self,uint32_t width,uint32_t height) {
    auto c=chain(self);Desc1 d=currentDesc(c);
    if(!width||!height||width>d.width||height>d.height)return InvalidArgument;
    c->sourceWidth=width;c->sourceHeight=height;
    reportOnce("DXGI: SetSourceSize lembrado, mas o swap chain mostra o buffer inteiro");
    return Ok;
}
int32_t dxgi_GetSourceSize(void* self,uint32_t* width,uint32_t* height) {
    if(!width||!height)return InvalidArgument;
    auto c=chain(self);Desc1 d=currentDesc(c);
    *width=c->sourceWidth?c->sourceWidth:d.width;*height=c->sourceHeight?c->sourceHeight:d.height;return Ok;
}
int32_t dxgi_SetMaximumFrameLatency(void* self,uint32_t latency){return int32_t(call(chain(self)->vk,VkSetFrameLatency,latency));}
int32_t dxgi_GetMaximumFrameLatency(void* self,uint32_t* latency) {
    if(!latency)return InvalidArgument;
    *latency=uint32_t(call(chain(self)->vk,VkGetFrameLatency));return Ok;
}
void* dxgi_GetFrameLatencyWaitableObject(void* self){return reinterpret_cast<void*>(call(chain(self)->vk,VkGetFrameLatencyEvent));}
uint32_t dxgi_GetCurrentBackBufferIndex(void* self){return uint32_t(call(chain(self)->vk,VkGetImageIndex));}
int32_t dxgi_CheckColorSpaceSupport(void* self,uint32_t space,uint32_t* support) {
    if(!support)return InvalidArgument;
    *support=uint32_t(call(chain(self)->vk,VkCheckColorSpaceSupport,space));return Ok;
}
int32_t dxgi_SetColorSpace1(void* self,uint32_t space){return int32_t(call(chain(self)->vk,VkSetColorSpace,space));}
// DXGI_VK_HDR_METADATA: the type, then DXGI_HDR_METADATA_HDR10 (28 bytes).
int32_t dxgi_SetHDRMetaData(void* self,uint32_t type,uint32_t size,const void* data) {
    constexpr uint32_t Hdr10Bytes=28;
    uint8_t metadata[4+Hdr10Bytes]={};
    std::memcpy(metadata,&type,4);
    if(type==1) {   // DXGI_HDR_METADATA_TYPE_HDR10
        if(!data||size<Hdr10Bytes)return InvalidArgument;
        std::memcpy(metadata+4,data,Hdr10Bytes);
    }else if(type!=0)return InvalidArgument;
    return int32_t(call(chain(self)->vk,VkSetHDRMetaData,arg(metadata)));
}
}

namespace azimuth::gpu {
std::string frameIntervalReport(){return ::frameIntervals.report();}
std::string frameWindowReport(){
    auto s=::frameWindow.sample(uint64_t(win32::fastMonotonicNs()));if(!s.valid)return {};
    char line[360];
    std::snprintf(line,sizeof line,"FPS 100ms: %.2f fps, %llu quadros, janela %.3f ms, max intervalo %.3f ms, sem novo quadro %.3f ms, fim mono ns %llu, ultimo quadro mono ns %llu",
        s.fps(),static_cast<unsigned long long>(s.frames),double(s.elapsedNs)/1e6,double(s.maxIntervalNs)/1e6,double(s.noFrameNs)/1e6,
        static_cast<unsigned long long>(s.endNs),static_cast<unsigned long long>(s.lastFrameNs));return line;
}
uint64_t presentedFrames(){return ::presentedFrames.load();}
uint64_t presentNanoseconds(){return ::presentNanos.load();}
uint32_t presentInterval(){return ::lastInterval.load();}
uint32_t presentFlags(){return ::lastFlags.load();}
}
