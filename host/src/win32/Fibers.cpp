// Windows fibers for the game's job system (title 1.12: the engine's
// kernelx.dll!ConvertThreadToFiber stopped the game once it was started from
// the menu). A fiber is a stack and a saved stack pointer; SwitchToFiber saves
// the callee-saved registers of the host frame it runs in (fiberSwitch in
// guest_abi.c) and resumes the other fiber's. The engine's own callee-saved
// registers (rdi, rsi, xmm6-15) are kept by the ms_abi entry of SwitchToFiber
// on each fiber's stack. The TEB follows the running fiber: FiberData at
// gs:[0x20] (GetCurrentFiber, GetFiberData read it inline) and the stack base
// and limit.
#include "Win32.h"
#include "../GuestThreads.h"
#include "../HostHeap.h"
#include "../guest_abi.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

using namespace azimuth::win32;

extern "C" void fiberSwitch(void** save,void* next);   // guest_abi.c
extern "C" void fiberStart(void);                       // guest_abi.c: a new fiber's first return lands here

namespace {
constexpr size_t TebStackBase=0x08,TebStackLimit=0x10,TebFiberData=0x20,TebSameTebFlags=0x17ee;
constexpr uint16_t HasFiberData=0x4;
constexpr uint32_t ErrorAlreadyFiber=1280,ErrorAlreadyThread=1281;
constexpr size_t DefaultStack=1u<<20,MinimumStack=64u<<10;
constexpr size_t HostRoom=256u<<10;   // the host's frames on a fiber's stack: SwitchToFiber, logging, guest calls
// GetFiberData() is the first field, as on Windows.
struct Fiber {
    void* data=nullptr;
    void* start=nullptr;
    void* sp=nullptr;                  // saved by fiberSwitch while it does not run
    uint8_t* stack=nullptr;size_t stackSize=0;   // none for a thread converted to a fiber
    uint64_t stackBase=0,stackLimit=0;           // the TEB's while it runs
    unsigned id=0;                               // for the log: 0 is a converted thread
    bool running=false;uint32_t thread=0;        // for the stop report
};
std::atomic<unsigned> created{0};
// Every fiber, for the stop report's view of the suspended ones (title 1.18:
// the game crashes in a state while its loading fiber is somewhere unknown).
std::mutex fibersLock;
std::set<Fiber*> fibers;
void track(Fiber* fiber){std::lock_guard<std::mutex> guard(fibersLock);fibers.insert(fiber);}
void untrack(Fiber* fiber){std::lock_guard<std::mutex> guard(fibersLock);fibers.erase(fiber);}
// The first switches and every 1000th one are logged, with where the game
// switched from (title 1.15: the game crashed 3.5 s into the open world's
// load, in a state that expects what the loading fiber initializes).
std::atomic<uint64_t> switches{0};
void logSwitch(const Fiber& from,const Fiber& to) {
    uint64_t n=++switches;
    if(n>40&&n%1000)return;
    log("SwitchToFiber "+std::to_string(n)+" na thread "+std::to_string(azimuth::currentGuestThreadId())+": fibra "+std::to_string(from.id)+
        " -> "+std::to_string(to.id)+"; chamado de"+codeCallers());
}
template<class T>T get(size_t at){T value;std::memcpy(&value,teb()+at,sizeof value);return value;}
template<class T>void set(size_t at,T value){std::memcpy(teb()+at,&value,sizeof value);}
Fiber* running(){return get<Fiber*>(TebFiberData);}
bool isFiber(){return (get<uint16_t>(TebSameTebFlags)&HasFiberData)&&running();}
// The TEB and the host's stack bound for the fiber about to run.
void enter(Fiber& fiber) {
    set(TebFiberData,&fiber);
    set(TebStackBase,fiber.stackBase);set(TebStackLimit,fiber.stackLimit);
    azimuth::setFiberStackTop(fiber.stack?uintptr_t(fiber.stackBase):0);
}
}

// A new fiber's routine, on its own stack. Its return ends the thread, as on Windows.
extern "C" void fiberMain(Fiber* fiber) {
    guestCall8(fiber->start,uint64_t(fiber->data),0,0,0,0,0,0,0);
    log("Fibra "+std::to_string(fiber->id)+": a rotina retornou, a thread "+std::to_string(azimuth::currentGuestThreadId())+" termina");
    azimuth::exitGuestThread(0);
}

// Each fiber, and for a suspended one the code addresses on its stack from
// where it switched away (the engine, the game and the DLLs, newest first).
namespace azimuth::win32 {
std::string describeFibers() {
    std::lock_guard<std::mutex> guard(fibersLock);
    std::string out;
    for(auto fiber:fibers) {
        char text[96];
        std::snprintf(text,sizeof text,"\n    fibra %u (thread %u)%s:",fiber->id,fiber->thread,fiber->running?" rodando":" suspensa");
        out+=text;
        if(fiber->running||!fiber->sp)continue;
        auto at=static_cast<const uint64_t*>(fiber->sp);
        auto end=reinterpret_cast<const uint64_t*>(fiber->stackBase);
        unsigned found=0;
        for(;at+1<=end&&found<24;++at) {
            uint64_t v=*at;
            bool code=(v>=0x140000000ull&&v<0x150000000ull)||(v>=0x180000000ull&&v<0x190000000ull)||(v>=0x1000010000ull&&v<0x1004000000ull);
            if(code){std::snprintf(text,sizeof text," 0x%llx",static_cast<unsigned long long>(v));out+=text;++found;}
        }
    }
    return out;
}
}
extern "C" void* win_ConvertThreadToFiberEx(void* parameter,uint32_t) {
    if(isFiber()){setLastError(ErrorAlreadyFiber);return nullptr;}
    auto fiber=new Fiber;
    fiber->data=parameter;
    fiber->stackBase=get<uint64_t>(TebStackBase);fiber->stackLimit=get<uint64_t>(TebStackLimit);
    fiber->running=true;fiber->thread=azimuth::currentGuestThreadId();
    track(fiber);
    set(TebFiberData,fiber);
    set<uint16_t>(TebSameTebFlags,get<uint16_t>(TebSameTebFlags)|HasFiberData);
    return fiber;
}
extern "C" void* win_ConvertThreadToFiber(void* parameter){return win_ConvertThreadToFiberEx(parameter,0);}
extern "C" int32_t win_ConvertFiberToThread(void) {
    if(!isFiber()){setLastError(ErrorAlreadyThread);return 0;}
    auto fiber=running();
    set<void*>(TebFiberData,nullptr);
    set<uint16_t>(TebSameTebFlags,get<uint16_t>(TebSameTebFlags)&~HasFiberData);
    if(!fiber->stack){untrack(fiber);delete fiber;}
    return 1;
}
extern "C" int32_t win_IsThreadAFiber(void){return isFiber();}
extern "C" void* win_CreateFiberEx(uint64_t commit,uint64_t reserve,uint32_t,void* start,void* parameter) {
    if(!start){setLastError(ErrorInvalidParameter);return nullptr;}
    size_t size=size_t(reserve?reserve:commit?commit:DefaultStack);
    if(size<MinimumStack)size=MinimumStack;
    size=(size+HostRoom+0xffff)&~size_t(0xffff);
    auto stack=static_cast<uint8_t*>(azimuth::heapAlloc(size,64,false));
    if(!stack){setLastError(ErrorNotEnoughMemory);return nullptr;}
    auto fiber=new Fiber;
    fiber->data=parameter;fiber->start=start;fiber->id=created+1;
    fiber->stack=stack;fiber->stackSize=size;
    track(fiber);
    fiber->stackBase=uint64_t(stack+size);fiber->stackLimit=uint64_t(stack);
    // The frame fiberSwitch restores: x87 control word, MXCSR, r15, r14, r13,
    // r12 (the fiber, for fiberStart), rbx, rbp, then the return address.
    auto frame=reinterpret_cast<uint64_t*>((uint64_t(stack+size)-256)&~uint64_t(15));
    uint64_t words[9]={0x27f,0x1f80,0,0,0,uint64_t(fiber),0,0,uint64_t(&fiberStart)};
    std::memcpy(frame,words,sizeof words);
    fiber->sp=frame;
    if(created++<8) {
        char text[128];
        std::snprintf(text,sizeof text,"CreateFiber: fibra %u com pilha de %zu KiB, rotina %p",created.load(),size>>10,start);
        log(text);
    }
    return fiber;
}
extern "C" void* win_CreateFiber(uint64_t size,void* start,void* parameter){return win_CreateFiberEx(size,size,0,start,parameter);}
extern "C" void win_SwitchToFiber(void* target) {
    auto to=static_cast<Fiber*>(target);
    auto from=running();
    if(!to||to==from)return;
    if(!from||!isFiber()){log("SwitchToFiber numa thread que nao e fibra");return;}
    logSwitch(*from,*to);
    from->running=false;to->running=true;to->thread=azimuth::currentGuestThreadId();
    enter(*to);
    fiberSwitch(&from->sp,to->sp);
    // Resumed: whoever switched back already set the TEB for this fiber.
}
extern "C" void win_DeleteFiber(void* target) {
    auto fiber=static_cast<Fiber*>(target);
    if(!fiber)return;
    static std::atomic<unsigned> deleted{0};
    if(deleted++<20)log("DeleteFiber: fibra "+std::to_string(fiber->id)+(fiber==running()?" (a que roda: a thread termina)":""));
    // Deleting the running fiber ends the thread, as on Windows.
    if(fiber==running())azimuth::exitGuestThread(0);
    untrack(fiber);
    if(fiber->stack)azimuth::heapFree(fiber->stack);
    delete fiber;
}
