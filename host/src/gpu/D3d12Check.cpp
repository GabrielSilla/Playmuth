// The D3D12 check (see D3d12Check.h). COM methods are reached through their
// vtables with the Windows convention (guestCall8); the slot numbers are the
// d3d12.h interface order (IUnknown 0-2, ID3D12Object 3-6, ...).
#include "D3d12Check.h"
#include "../guest_abi.h"
#include "../win32/Win32.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <functional>
#include <unistd.h>
#include <string>
#include <thread>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../win32/exports.inc"
#undef EXPORT

namespace azimuth::gpu {
namespace {
using win32::log;
struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
constexpr Guid FactoryId{0x1bc6ea02,0xef36,0x464f,{0xbf,0x0c,0x21,0xca,0x39,0xe5,0x16,0x8a}};
constexpr Guid DeviceId{0x189819f1,0x1db6,0x4b57,{0xbe,0x54,0x18,0x21,0x33,0x9b,0x85,0xf7}};
constexpr Guid QueueId{0x0ec870a6,0x5d7e,0x4c22,{0x8c,0xfc,0x5b,0xaa,0xe0,0x76,0x16,0xed}};
constexpr Guid AllocatorId{0x6102dee4,0xaf59,0x4b09,{0xb9,0x99,0xb4,0x4d,0x73,0xf0,0x9b,0x24}};
constexpr Guid ListId{0x5b160d0f,0xac1b,0x4185,{0x8b,0xa8,0xb3,0xae,0x42,0xa5,0xa4,0x55}};
constexpr Guid FenceId{0x0a753dcf,0xc4d8,0x4b91,{0xad,0xf6,0xbe,0x5a,0x60,0xd9,0x5a,0x76}};
// Vtable slots.
enum : unsigned {
    Release=2,
    DeviceCreateCommandQueue=8,DeviceCreateCommandAllocator=9,DeviceCreateCommandList=12,DeviceCreateFence=36,
    QueueExecuteCommandLists=10,QueueSignal=14,
    ListClose=9,
    FenceGetCompletedValue=8,
    FactoryCreateSwapChainForHwnd=15,ChainPresent=8,ChainGetBuffer=9,
};
constexpr Guid ResourceId{0x696442be,0xa72e,0x4059,{0xbc,0x79,0x5b,0x5c,0x98,0x04,0x0f,0xad}};
constexpr uint32_t FeatureLevel11_0=0xb000,ListTypeDirect=0;

uint64_t u(const void* p){return reinterpret_cast<uint64_t>(p);}
// object->method(args...), Windows convention.
uint64_t call(void* object,unsigned slot,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0,uint64_t f=0,uint64_t g=0) {
    auto vtable=*static_cast<void* const* const*>(object);
    return guestCall8(vtable[slot],u(object),a,b,c,d,e,f,g);
}
std::string hr(uint64_t value){char text[16];std::snprintf(text,sizeof text,"0x%08x",unsigned(uint32_t(value)));return text;}
bool ok(uint64_t value){return int32_t(uint32_t(value))>=0;}
}

#if defined(__linux__)
// XAudio2 (title 0.98): FMOD's use, a float stereo voice fed buffer by buffer,
// with its callback called from the mixer's engine thread.
namespace {
std::atomic<int> buffersEnded{0};
__attribute__((ms_abi)) void callbackNothing(void*){}
__attribute__((ms_abi)) void callbackPassStart(void*,uint32_t){}
__attribute__((ms_abi)) void callbackContext(void*,void*){}
__attribute__((ms_abi)) void callbackBufferEnd(void*,void*){++buffersEnded;}
__attribute__((ms_abi)) void callbackError(void*,void*,int32_t){}
const void* const voiceCallbackVtable[7]={(const void*)&callbackPassStart,(const void*)&callbackNothing,(const void*)&callbackNothing,
    (const void*)&callbackContext,(const void*)&callbackBufferEnd,(const void*)&callbackContext,(const void*)&callbackError};
struct {const void* const* vtable;} voiceCallback={voiceCallbackVtable};
// ConcRT and the thread pool (title 1.10).
std::atomic<int> poolFired{0};
__attribute__((ms_abi)) void taskProc(void* counter){++*static_cast<std::atomic<int>*>(counter);}
__attribute__((ms_abi)) void timerFired(void*,void*,void*){poolFired+=1;}
__attribute__((ms_abi)) void waitFired(void*,void*,void*,uint32_t result){if(result==0)poolFired+=10;}
// A thread outside the scheduler that schedules a task and ends: ConcRT
// waits for its handle in the thread pool, and that callback unpins msvcr110
// with FreeLibraryWhenCallbackReturns (title 1.10 stopped there).
void* scheduleTask=nullptr;
std::atomic<int> externalRan{0};
__attribute__((ms_abi)) uint32_t externalThread(void*) {
    guestCall8(scheduleTask,uint64_t(&taskProc),uint64_t(&externalRan),0,0,0,0,0,0);
    return 0;
}
// Fibers (title 1.12), through the entry points the engine gets from
// GetProcAddress: two fibers switch back and forth 1000 times keeping a double
// across the switches, read GetFiberData through gs:[0x20] as the game does,
// and a third runs the exception cases on its own stack.
typedef __attribute__((ms_abi)) void* (*FiberConvertFn)(void*);
typedef __attribute__((ms_abi)) void* (*FiberCreateFn)(uint64_t,void*,void*);
typedef __attribute__((ms_abi)) void (*FiberFn)(void*);
typedef __attribute__((ms_abi)) int32_t (*FiberBackFn)(void);
FiberFn switchToFiber=nullptr;
void* mainFiber=nullptr;void* fiberA=nullptr;void* fiberB=nullptr;
const void* sehOnFiber=nullptr;uint64_t sehOnFiberResult=0;
int fiberSteps=0;bool fiberDataOk=true;double fiberSum=0;
void* currentFiberData(){void* fiber;__asm__ volatile("movq %%gs:0x20,%0":"=r"(fiber));return *static_cast<void**>(fiber);}
__attribute__((ms_abi)) void fiberAMain(void* data) {
    double sum=0.5;
    for(int n=0;n<1000;++n){if(currentFiberData()!=data)fiberDataOk=false;sum+=1.0;++fiberSteps;switchToFiber(fiberB);}
    fiberSum=sum;
    switchToFiber(mainFiber);
}
__attribute__((ms_abi)) void fiberBMain(void* data) {
    for(;;){if(currentFiberData()!=data)fiberDataOk=false;++fiberSteps;switchToFiber(fiberA);}
}
__attribute__((ms_abi)) void fiberSehMain(void*) {
    sehOnFiberResult=guestCall8(sehOnFiber,0,0,0,0,0,0,0,0)&0xffffffff;
    switchToFiber(mainFiber);
}
bool fiberCheck(void* seh) {
    void* kernel=win_LoadLibraryA("kernel32.dll");
    auto convert=(FiberConvertFn)win_GetProcAddress(kernel,"ConvertThreadToFiber");
    auto create=(FiberCreateFn)win_GetProcAddress(kernel,"CreateFiber");
    auto remove=(FiberFn)win_GetProcAddress(kernel,"DeleteFiber");
    auto back=(FiberBackFn)win_GetProcAddress(kernel,"ConvertFiberToThread");
    switchToFiber=(FiberFn)win_GetProcAddress(kernel,"SwitchToFiber");
    if(!convert||!create||!remove||!back||!switchToFiber){log("Fibras: entradas ausentes no kernel32");return false;}
    mainFiber=convert((void*)0x1234);
    fiberA=create(64u<<10,(void*)&fiberAMain,(void*)0xa);
    fiberB=create(0,(void*)&fiberBMain,(void*)0xb);
    switchToFiber(fiberA);
    bool switched=fiberSteps==2000&&fiberSum==1000.5&&fiberDataOk&&currentFiberData()==(void*)0x1234;
    bool exceptions=true;
    if(seh) {
        sehOnFiber=win_GetProcAddress(seh,"sehTest");
        void* fiber=create(0,(void*)&fiberSehMain,nullptr);
        switchToFiber(fiber);
        remove(fiber);
        exceptions=sehOnFiberResult==31;
    }
    remove(fiberA);remove(fiberB);
    bool converted=back()==1;
    log("Fibras: "+std::to_string(fiberSteps)+" passos, soma "+std::to_string(fiberSum)+(fiberDataOk?", GetFiberData certo":", GetFiberData ERRADO")+
        ", excecoes numa fibra = "+std::to_string(sehOnFiberResult)+(converted?", thread restaurada":", ConvertFiberToThread falhou"));
    return switched&&exceptions&&converted;
}
// Critical sections (title 1.13): recursion, TryEnter from another thread,
// a leave by a thread that did not enter (allowed on Windows), and a wait
// that ends when the owner leaves.
bool waitUntil(const std::function<bool()>& done);
alignas(8) uint8_t section[40];
std::atomic<int> sectionStep{0};
__attribute__((ms_abi)) uint32_t sectionTry(void*){return win_TryEnterCriticalSection(section);}
__attribute__((ms_abi)) uint32_t sectionLeave(void*){win_LeaveCriticalSection(section);return 0;}
__attribute__((ms_abi)) uint32_t sectionHold(void*) {
    win_EnterCriticalSection(section);sectionStep=1;
    usleep(200000);sectionStep=2;
    win_LeaveCriticalSection(section);
    return 0;
}
uint32_t threadResult(void* start) {
    uint32_t id=0,code=99;
    void* thread=win_CreateThread(nullptr,0,start,nullptr,0,&id);
    if(!thread||win_WaitForSingleObject(thread,5000)!=0)return 98;
    win_GetExitCodeThread(thread,&code);win_CloseHandle(thread);
    return code;
}
bool sectionCheck() {
    win_InitializeCriticalSection(section);
    win_EnterCriticalSection(section);win_EnterCriticalSection(section);
    bool recursive=win_TryEnterCriticalSection(section)==1;
    win_LeaveCriticalSection(section);win_LeaveCriticalSection(section);
    bool busy=threadResult((void*)&sectionTry)==0;            // still held once
    threadResult((void*)&sectionLeave);                       // released by another thread
    bool released=threadResult((void*)&sectionTry)==1;        // that thread now holds it...
    threadResult((void*)&sectionLeave);                       // ...and another releases it
    uint32_t id=0;void* holder=win_CreateThread(nullptr,0,(void*)&sectionHold,nullptr,0,&id);
    waitUntil([]{return sectionStep.load()==1;});
    win_EnterCriticalSection(section);
    bool waited=sectionStep.load()==2;
    win_LeaveCriticalSection(section);
    win_WaitForSingleObject(holder,5000);win_CloseHandle(holder);
    win_DeleteCriticalSection(section);
    log(std::string("Secoes criticas: recursao ")+(recursive?"ok":"FALHOU")+", ocupada "+(busy?"ok":"FALHOU")+
        ", liberada por outra thread "+(released?"ok":"FALHOU")+", espera "+(waited?"ok":"FALHOU"));
    return recursive&&busy&&released&&waited;
}
bool waitUntil(const std::function<bool()>& done) {
    for(int n=0;n<200&&!done();++n)usleep(10000);
    return done();
}
bool concurrencyCheck() {
    void* crt=win_LoadLibraryA("msvcr110.dll");
    if(!crt){log("msvcr110.dll ausente: ConcRT nao verificado");return true;}
    auto processors=uint32_t(guestCall8(win_GetProcAddress(crt,"?GetProcessorCount@Concurrency@@YAIXZ"),0,0,0,0,0,0,0,0));
    // CurrentScheduler::ScheduleTask creates the default scheduler, as the game's first task does.
    static std::atomic<int> ran{0};
    for(int n=0;n<8;++n)guestCall8(win_GetProcAddress(crt,"?ScheduleTask@CurrentScheduler@Concurrency@@SAXP6AXPEAX@Z0@Z"),uint64_t(&taskProc),uint64_t(&ran),0,0,0,0,0,0);
    bool tasks=waitUntil([]{return ran.load()==8;});
    scheduleTask=win_GetProcAddress(crt,"?ScheduleTask@CurrentScheduler@Concurrency@@SAXP6AXPEAX@Z0@Z");
    uint32_t id=0;
    void* external=win_CreateThread(nullptr,0,(void*)&externalThread,nullptr,0,&id);
    tasks=tasks&&external&&win_WaitForSingleObject(external,5000)==0&&waitUntil([]{return externalRan.load()==1;});
    usleep(300000);   // ConcRT's wait for the ended thread fires in the pool
    log("ConcRT: tarefa de uma thread externa "+std::string(externalRan.load()==1?"executada":"NAO executada"));
    log("ConcRT: "+std::to_string(processors)+" processadores, "+std::to_string(ran.load())+" de 8 tarefas executadas");
    void* timer=win_CreateThreadpoolTimer((void*)&timerFired,nullptr,nullptr);
    int64_t due=-100000;   // 10 ms
    win_SetThreadpoolTimer(timer,&due,0,0);
    void* event=win_CreateEventW(nullptr,0,0,nullptr);
    void* wait=win_CreateThreadpoolWait((void*)&waitFired,nullptr,nullptr);
    win_SetThreadpoolWait(wait,event,nullptr);
    win_SetEvent(event);
    bool pool=waitUntil([]{return poolFired.load()==11;});
    win_WaitForThreadpoolTimerCallbacks(timer,1);win_CloseThreadpoolTimer(timer);
    win_WaitForThreadpoolWaitCallbacks(wait,1);win_CloseThreadpoolWait(wait);win_CloseHandle(event);
    log(std::string("Pool de threads: timer e espera ")+(pool?"chamados":"NAO chamados ("+std::to_string(poolFired.load())+")"));
    return tasks&&pool;
}
bool xaudio2Check() {
    void* engine=nullptr;
    if(win_XAudio2Create(&engine,0,0)<0||!engine){log("[FALHOU] XAudio2Create");return false;}
    void* master=nullptr;
    uint64_t r=call(engine,7,u(&master),2,48000,0,0,0,0);
    log("CreateMasteringVoice: "+hr(r));
    if(!ok(r)||!master)return false;
    // WAVEFORMATEX: IEEE float, 2 channels, 48 kHz.
    uint8_t format[18]={3,0,2,0,0x80,0xbb,0,0,0,0xdc,5,0,8,0,32,0,0,0};
    void* voice=nullptr;
    r=call(engine,5,u(&voice),u(format),0,0,u(&voiceCallback),0,0);
    log("CreateSourceVoice (float estereo): "+hr(r));
    if(!ok(r)||!voice)return false;
    static float samples[3][480*2];
    for(int n=0;n<3;++n) {
        uint8_t buffer[48]={};uint32_t bytes=sizeof samples[n];const void* data=samples[n];
        std::memcpy(buffer+4,&bytes,4);std::memcpy(buffer+8,&data,8);
        if(!ok(call(voice,21,u(buffer),0))){log("[FALHOU] SubmitSourceBuffer");return false;}
    }
    call(engine,8);call(voice,19,0,0);
    auto start=std::chrono::steady_clock::now();
    while(buffersEnded<3&&std::chrono::steady_clock::now()-start<std::chrono::seconds(2))std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint8_t state[24]={};call(voice,25,u(state),0);
    uint64_t played;std::memcpy(&played,state+16,8);
    log("XAudio2: "+std::to_string(buffersEnded.load())+" OnBufferEnd, "+std::to_string(played)+" amostras tocadas");
    call(voice,18);call(master,18);call(engine,2);
    return buffersEnded==3&&played==1440;
}
}
#endif

uint32_t d3d12Check() {
    log("Verificacao D3D12: d3d12.dll (vkd3d-proton) pelo loader do host, sobre o vulkan-1.dll do host");
    void* module=win_LoadLibraryA("d3d12.dll");
    if(!module){log("[FALHOU] d3d12.dll nao carregou");return 1;}
    void* create=win_GetProcAddress(module,"D3D12CreateDevice");
    if(!create){log("[FALHOU] D3D12CreateDevice ausente no d3d12.dll");return 1;}
    // As the engine does: the adapter comes from the DXGI factory (D3D12CreateDevice of
    // the host's d3d12 library forwards to vkd3d-proton with it).
    void* factory=nullptr;void* adapter=nullptr;
    auto made=win_CreateDXGIFactory2(0,&FactoryId,&factory);
    log("CreateDXGIFactory2 (IDXGIFactory4): "+hr(uint64_t(uint32_t(made))));
    if(ok(made)&&factory)log("EnumAdapters1(0): "+hr(call(factory,12,0,u(&adapter))));
    void* device=nullptr;
    auto result=win_D3D12CreateDevice(adapter,FeatureLevel11_0,&DeviceId,&device);
    log("D3D12CreateDevice (adaptador do DXGI, FL 11_0): "+hr(uint64_t(uint32_t(result)))+", dispositivo "+std::to_string(u(device)));
    if(!ok(result)||!device){log("[FALHOU] D3D12CreateDevice");return 1;}

    struct {int32_t type,priority,flags;uint32_t nodeMask;} queueDesc{int32_t(ListTypeDirect),0,0,0};
    void* queue=nullptr;
    result=call(device,DeviceCreateCommandQueue,u(&queueDesc),u(&QueueId),u(&queue));
    log("CreateCommandQueue (direta): "+hr(result));
    if(!ok(result)||!queue){log("[FALHOU] CreateCommandQueue");return 1;}
    void* allocator=nullptr;
    result=call(device,DeviceCreateCommandAllocator,ListTypeDirect,u(&AllocatorId),u(&allocator));
    log("CreateCommandAllocator: "+hr(result));
    if(!ok(result)||!allocator){log("[FALHOU] CreateCommandAllocator");return 1;}
    void* list=nullptr;
    result=call(device,DeviceCreateCommandList,0,ListTypeDirect,u(allocator),0,u(&ListId),u(&list));
    log("CreateCommandList: "+hr(result));
    if(!ok(result)||!list){log("[FALHOU] CreateCommandList");return 1;}
    result=call(list,ListClose);
    log("Close da lista vazia: "+hr(result));
    if(!ok(result)){log("[FALHOU] Close");return 1;}
    void* fence=nullptr;
    result=call(device,DeviceCreateFence,0,0,u(&FenceId),u(&fence));
    log("CreateFence: "+hr(result));
    if(!ok(result)||!fence){log("[FALHOU] CreateFence");return 1;}

    void* lists[1]={list};
    call(queue,QueueExecuteCommandLists,1,u(lists));
    result=call(queue,QueueSignal,u(fence),1);
    log("ExecuteCommandLists + Signal(1): "+hr(result));
    if(!ok(result)){log("[FALHOU] Signal");return 1;}
    auto start=std::chrono::steady_clock::now();
    uint64_t value=0;
    while((value=call(fence,FenceGetCompletedValue))<1&&std::chrono::steady_clock::now()-start<std::chrono::seconds(2))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    log("Fence: valor "+std::to_string(value)+" depois de "+std::to_string(ms)+" ms");
    if(value<1){log("[FALHOU] a fila nao concluiu a lista");return 1;}

    // The swap chain the engine makes (title 0.93): the host's DXGI over
    // vkd3d-proton's, on the host's surface, presented a few times.
    if(factory) {
        uint32_t desc[12]={1280,720,28,0,1,0,0x20,3,0,4,0,0};   // R8G8B8A8_UNORM, RENDER_TARGET_OUTPUT, 3 buffers, FLIP_DISCARD
        void* chain=nullptr;
        result=call(factory,FactoryCreateSwapChainForHwnd,u(queue),0,u(desc),0,0,u(&chain));
        log("CreateSwapChainForHwnd 1280x720: "+hr(result));
        if(!ok(result)||!chain){log("[FALHOU] CreateSwapChainForHwnd");return 1;}
        void* buffer=nullptr;
        result=call(chain,ChainGetBuffer,0,u(&ResourceId),u(&buffer));
        log("GetBuffer(0): "+hr(result));
        if(!ok(result)||!buffer){log("[FALHOU] GetBuffer");return 1;}
        call(buffer,Release);
        for(int n=0;n<3;++n) {
            result=call(chain,ChainPresent,1,0);
            log("Present "+std::to_string(n+1)+": "+hr(result));
            if(!ok(result)){log("[FALHOU] Present");return 1;}
        }
        call(chain,Release);
        call(factory,Release);
    }
    for(void* object:{fence,list,allocator,queue,device})call(object,Release);
#if defined(__linux__)
    if(!xaudio2Check()){log("[FALHOU] XAudio2");return 1;}
    // Exception dispatch (title 1.00): tests/seh's DLL, built with MSVC's C++
    // tables on msvcr110, returns one bit per case that worked (all five: 31).
    win_LoadLibraryA("msvcr110.dll");   // as the engine loads it before the game's code runs
    if(void* seh=win_LoadLibraryA("sehtest.dll")) {
        uint64_t passed=guestCall8(win_GetProcAddress(seh,"sehTest"),0,0,0,0,0,0,0,0)&0xffffffff;
        log("Excecoes: sehTest = "+std::to_string(passed)+" (31 = throw/catch, destrutores, rethrow, catch(...), __try/__except)");
        if(passed!=31){log("[FALHOU] excecoes");return 1;}
    }else log("sehtest.dll ausente (tests/seh/build.sh): excecoes nao verificadas");
    if(!sectionCheck()){log("[FALHOU] secoes criticas");return 1;}
    if(!concurrencyCheck()){log("[FALHOU] ConcRT e pool de threads");return 1;}
    if(!fiberCheck(win_LoadLibraryA("sehtest.dll"))){log("[FALHOU] fibras");return 1;}
    // FFmpeg (title 1.03): avcodec-62 with avutil-60 on the Universal CRT, as
    // the engine loads them for SHAPE's audio; the XMA2 decoder must be there.
    if(void* avcodec=win_LoadLibraryA("avcodec-62.dll")) {
        void* avutil=win_LoadLibraryA("avutil-60.dll");
        auto version=uint32_t(guestCall8(win_GetProcAddress(avcodec,"avcodec_version"),0,0,0,0,0,0,0,0));
        auto info=reinterpret_cast<const char*>(guestCall8(win_GetProcAddress(avutil,"av_version_info"),0,0,0,0,0,0,0,0));
        auto xma=guestCall8(win_GetProcAddress(avcodec,"avcodec_find_decoder_by_name"),reinterpret_cast<uint64_t>("xma2"),0,0,0,0,0,0,0);
        log("FFmpeg: avcodec "+std::to_string(version>>16)+"."+std::to_string((version>>8)&0xff)+"."+std::to_string(version&0xff)+
            ", avutil "+(info?info:"?")+", decodificador xma2 "+(xma?"presente":"ausente"));
        if(!xma){log("[FALHOU] FFmpeg sem o decodificador xma2");return 1;}
    }else log("avcodec-62.dll ausente: FFmpeg nao verificado");
#endif
    log("[PASSOU] D3D12 pelo vkd3d-proton: dispositivo, fila, lista executada, fence sinalizada e swap chain apresentado");
    return 0;
}
}
