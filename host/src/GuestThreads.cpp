#include "GuestThreads.h"
#include "Modules.h"
#include "Platform.h"
#include "guest_abi.h"
#include "win32/Win32.h"
#include "win32/WriteWatch.h"
#include "win32/VectoredExceptions.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csetjmp>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <memory>
#include <pthread.h>
#include <string>
#include <tuple>
#include <mutex>
#include <ucontext.h>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include <vector>

namespace azimuth {
namespace {
constexpr uint32_t DllProcessAttach=1,DllThreadAttach=2,DllThreadDetach=3;
constexpr size_t StackGuard=64u<<10;
GuestSetup setup;
std::atomic<uint32_t> nextId{0x100};

// The calling engine thread: where a stop or ExitThread returns to.
struct State {
    std::jmp_buf escape;
    void* originalGs=nullptr;
    uint32_t id=0,exitCode=0;
    uintptr_t stackTop=0;   // a host frame above every engine frame: the end of what a report may read
    uintptr_t threadStackTop=0;   // the same on the thread's own stack, while a fiber's runs
};
thread_local State* current=nullptr;

std::mutex runLock;
std::condition_variable runChanged;
StopInfo firstStop;
std::atomic<bool> stopping{false};
unsigned live=0,announced=0;

enum : int {JumpExit=1,JumpStop=2};
// The engine threads' pthreads, for sampling them.
std::mutex threadsLock;
// The pthread and the highest stack address the sampler may read (a host
// frame of the thread, above every engine frame), and the thread's TEB.
struct Sampled {pthread_t thread;uintptr_t limit;void* teb;};
std::map<uint32_t,Sampled> threads;
struct Registered {
    uint32_t id;
    Registered(uint32_t value,uintptr_t limit,void* teb):id(value){std::lock_guard<std::mutex> guard(threadsLock);threads[id]={pthread_self(),limit,teb};}
    ~Registered(){std::lock_guard<std::mutex> guard(threadsLock);threads.erase(id);}
};
// The engine is optional: the d3d12 check runs loaded DLLs without it.
uint64_t engineBase(){return setup.image?setup.image->base():0;}
void detach(bool mainThread,ThreadBlock& block) {
    if(mainThread){detachThreadFromModules(block,false);return;}
    detachThreadFromModules(block,true);
    for(auto callback:setup.tlsCallbacks)guestCallTlsCallback(callback,engineBase(),DllThreadDetach);
    win32::runFlsCallbacks();
}
}
void setupGuest(const GuestSetup& value){setup=value;}
uint32_t newThreadId(){return nextId.fetch_add(4);}
uint32_t currentGuestThreadId(){return current?current->id:0;}
uintptr_t currentStackTop(){return current?current->stackTop:0;}
void setFiberStackTop(uintptr_t top){if(current)current->stackTop=top?top:current->threadStackTop;}

void announceGuestThread(){std::lock_guard<std::mutex> lock(runLock);++announced;}
void withdrawGuestThread(){{std::lock_guard<std::mutex> lock(runLock);--announced;}runChanged.notify_all();}
GuestOutcome runAsGuest(uint32_t threadId,bool mainThread,size_t stackSize,const std::function<uint32_t()>& body) {
    if(!mainThread) {
        bool late;
        {std::lock_guard<std::mutex> lock(runLock);--announced;late=stopping;if(!late)++live;}
        if(late){runChanged.notify_all();GuestOutcome outcome;outcome.end=GuestEnd::Stopped;return outcome;}
    }else {std::lock_guard<std::mutex> lock(runLock);++live;}
    auto leave=[]{{std::lock_guard<std::mutex> lock(runLock);--live;}runChanged.notify_all();};
    {
        // The main thread draws the launcher and later runs the game: it goes first.
        auto scheduling=platform::shareCpu(mainThread);
        static std::atomic<unsigned> logged{0};
        if(!scheduling.empty()&&logged++<16)win32::log("Agendamento da thread do motor "+std::to_string(threadId)+": "+scheduling);
    }
    char marker;
    auto top=(reinterpret_cast<uintptr_t>(&marker)+4095)&~uintptr_t(4095);
    std::unique_ptr<ThreadBlock> owned;
    State state;state.id=threadId;state.stackTop=state.threadStackTop=reinterpret_cast<uintptr_t>(&marker);
    try {
        owned=std::make_unique<ThreadBlock>(*setup.process,reinterpret_cast<void*>(top),reinterpret_cast<void*>(top-stackSize+StackGuard),threadId);
        if(setup.image)setup.image->initializeStaticTls(owned->staticTls(),ThreadBlock::TlsCapacity);
        if(int error=platform::getGsBase(&state.originalGs))throw std::runtime_error("ler a base original do gs falhou, errno "+std::to_string(error));
    }catch(...){leave();throw;}
    auto& block=*owned;
    GuestOutcome outcome;
    Registered registered(threadId,reinterpret_cast<uintptr_t>(&marker),block.teb());
    // The fault handler's own stack, for a fault that is a stack overflow.
    constexpr size_t AltStackSize=256u<<10;
    // A mapping of its own, outside the heap: corruption there reached the
    // handler's stack (title 0.80).
    void* altStack=mmap(nullptr,AltStackSize,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    struct AltStackFree{void* p;~AltStackFree(){if(p!=MAP_FAILED)munmap(p,AltStackSize);}} altStackFree{altStack};
    stack_t alternate{};alternate.ss_sp=altStack==MAP_FAILED?nullptr:altStack;alternate.ss_size=AltStackSize;
    sigaltstack(&alternate,nullptr);
    struct AltStackOff{~AltStackOff(){stack_t off{};off.ss_flags=SS_DISABLE;sigaltstack(&off,nullptr);}} altStackOff;
    current=&state;
    if(int error=block.enter()){current=nullptr;leave();throw std::runtime_error("definir o gs falhou, errno "+std::to_string(error));}
    switch(setjmp(state.escape)) {
        case 0:
            for(auto callback:setup.tlsCallbacks)guestCallTlsCallback(callback,engineBase(),mainThread?DllProcessAttach:DllThreadAttach);
            attachThreadToModules(block,!mainThread);
            state.exitCode=body();
            outcome.end=GuestEnd::Returned;
            detach(mainThread,block);
            break;
        case JumpExit:
            // ExitThread still notifies the TLS callbacks and FLS; a stop meanwhile ends it.
            outcome.end=GuestEnd::Exited;
            if(setjmp(state.escape)==0)detach(mainThread,block);
            break;
        default:
            outcome.end=GuestEnd::Stopped;
            break;
    }
    outcome.code=state.exitCode;
    forgetThreadBlock(block);
    platform::setGsBase(state.originalGs);
    current=nullptr;
    {std::lock_guard<std::mutex> lock(runLock);--live;}
    runChanged.notify_all();
    return outcome;
}
void exitGuestThread(uint32_t code) {
    if(!current)std::abort();
    current->exitCode=code;
    std::longjmp(current->escape,JumpExit);
}
void stopRun(StopInfo info) {
    if(info.reason.find(" ultimas chamadas:")==std::string::npos&&!info.missingImport)info.reason+=" | ultimas chamadas: "+recentHostCalls(24);
    {
        std::lock_guard<std::mutex> lock(runLock);
        if(!firstStop.stopped){info.stopped=true;info.threadId=current?current->id:0;firstStop=std::move(info);}
        stopping=true;
    }
    runChanged.notify_all();
    win32::wakeWaiters();
    if(!current)std::abort();
    std::longjmp(current->escape,JumpStop);
}
bool runStopping(){return stopping.load(std::memory_order_relaxed);}
void* guestThreadTeb(uint32_t id) {
    std::lock_guard<std::mutex> guard(threadsLock);
    auto found=threads.find(id);
    return found==threads.end()?nullptr:found->second.teb;
}
std::vector<uint32_t> guestThreadIds() {
    std::lock_guard<std::mutex> guard(threadsLock);
    std::vector<uint32_t> ids;
    for(auto const& entry:threads)ids.push_back(entry.first);
    return ids;
}
StopInfo stopInfo(){std::lock_guard<std::mutex> lock(runLock);return firstStop;}
void leaveIfStopping() {
    if(stopping.load(std::memory_order_relaxed)&&current)std::longjmp(current->escape,JumpStop);
}
bool waitForStop(int timeoutMs) {
    std::unique_lock<std::mutex> lock(runLock);
    return runChanged.wait_for(lock,std::chrono::milliseconds(timeoutMs),[]{return stopping.load();});
}
unsigned liveGuestThreads(){std::lock_guard<std::mutex> lock(runLock);return live;}
bool waitForGuestThreads(int timeoutMs) {
    std::unique_lock<std::mutex> lock(runLock);
    return runChanged.wait_for(lock,std::chrono::milliseconds(timeoutMs),[]{return live==0&&announced==0;});
}

namespace {
int faultReport=-1;
thread_local bool inFault=false;
const char* signalName(int number) {
    switch(number) {
    case SIGSEGV:return "SIGSEGV";case SIGBUS:return "SIGBUS";case SIGILL:return "SIGILL";case SIGFPE:return "SIGFPE";
    case SIGTRAP:return "SIGTRAP (int3)";case SIGABRT:return "SIGABRT";case SIGSYS:return "SIGSYS";default:return "?";
    }
}
// Writes a NUL-terminated text straight to the report, allocating nothing.
void rawReport(const char* text,int length) {
    if(faultReport<0||length<=0)return;
    (void)!write(faultReport,text,size_t(length));fsync(faultReport);
}
void onFault(int number,siginfo_t* info,void* context) {
    uint64_t rip=0,rsp=0,err=0,trap=0;
    auto uc=static_cast<ucontext_t*>(context);
#if defined(__linux__)
    rip=uint64_t(uc->uc_mcontext.gregs[REG_RIP]);rsp=uint64_t(uc->uc_mcontext.gregs[REG_RSP]);err=uint64_t(uc->uc_mcontext.gregs[REG_ERR]);trap=uint64_t(uc->uc_mcontext.gregs[REG_TRAPNO]);
#else
    // The PS5 kernel's ucontext has its mcontext at +0x40, not at the SDK
    // header's +0x10 (measured by GPU probe 0.5); the mcontext is FreeBSD's.
    (void)uc;
    auto mc=reinterpret_cast<const mcontext_t*>(static_cast<const char*>(context)+0x40);
    rip=uint64_t(mc->mc_rip);rsp=uint64_t(mc->mc_rsp);err=uint64_t(mc->mc_err);trap=uint64_t(mc->mc_trapno);
#endif
    if(number==SIGSEGV&&(err&2)&&win32::watchFault(reinterpret_cast<uint64_t>(info->si_addr)))return;
    // Tracking faults can occur in stores made by a guest VEH callback.
    // Recover those before applying the recursive-fault guard.
    if(inFault){
        static const char text[]="FALHA FATAL: nova falha dentro do tratador de falhas; thread parada\n";
        rawReport(text,int(sizeof text-1));for(;;)sleep(3600);
    }
    inFault=true;
    if(number==SIGSEGV&&current&&win32::dispatchAccessViolation(reinterpret_cast<uint64_t>(info->si_addr),(err&16)?8:(err&2)?1:0,context)){
        inFault=false;return;
    }
    // Forza Horizon 2's States::StopTimeOfDay (title 1.23): it marks the time
    // of day stopped on the object ForzaSimulation's initialization creates,
    // `cmpb $0,0x27(%rax); jne +3; movb %bl,0x24(%rax)`. Loading from the USB
    // drive is slower than the Xbox's disk, the state script reaches this node
    // before the loading fiber has made the object, and rax is null. With no
    // object there is nothing to mark: the 9 bytes are skipped, as if the
    // flag were already set, and the thread goes on.
    if(number==SIGSEGV&&reinterpret_cast<uintptr_t>(info->si_addr)==0x27) {
        static const uint8_t pattern[9]={0x80,0x78,0x27,0x00,0x75,0x03,0x88,0x58,0x24};
#if defined(__linux__)
        auto& raxSlot=uc->uc_mcontext.gregs[REG_RAX];auto& ripSlot=uc->uc_mcontext.gregs[REG_RIP];
#else
        auto writable=reinterpret_cast<mcontext_t*>(static_cast<char*>(context)+0x40);
        auto& raxSlot=writable->mc_rax;auto& ripSlot=writable->mc_rip;
#endif
        if(raxSlot==0&&std::memcmp(reinterpret_cast<const void*>(rip),pattern,sizeof pattern)==0) {
            ripSlot+=sizeof pattern;
            static std::atomic<unsigned> skipped{0};
            if(skipped++<5){static const char text[]="StopTimeOfDay sem o objeto da hora do dia (simulacao ainda carregando): marcacao pulada\n";rawReport(text,int(sizeof text-1));}
            inFault=false;
            return;
        }
    }
    uint64_t base=setup.image?setup.image->base():0,size=setup.image?setup.image->size():0;
    {
        // The essentials, before anything that allocates or takes a lock.
        char first[300];
        int length=std::snprintf(first,sizeof first,"FALHA (registro imediato): sinal %d em 0x%llx (engine+0x%llx), endereco 0x%llx, rsp 0x%llx, thread do motor %u, trap 0x%llx, err 0x%llx\n",
                                 number,static_cast<unsigned long long>(rip),
                                 static_cast<unsigned long long>(base&&rip>=base&&rip-base<size?rip-base:0),
                                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->si_addr)),
                                 static_cast<unsigned long long>(rsp),current?current->id:0,static_cast<unsigned long long>(trap),static_cast<unsigned long long>(err));
        rawReport(first,length);
        // Register values only: no dereference of unverified guest memory.
        uint64_t r[16]={};
#if defined(__linux__)
        auto g=uc->uc_mcontext.gregs;
        uint64_t linuxRegs[16]={uint64_t(g[REG_RAX]),uint64_t(g[REG_RBX]),uint64_t(g[REG_RCX]),uint64_t(g[REG_RDX]),uint64_t(g[REG_RSI]),uint64_t(g[REG_RDI]),
                                uint64_t(g[REG_RBP]),uint64_t(g[REG_RSP]),uint64_t(g[REG_R8]),uint64_t(g[REG_R9]),uint64_t(g[REG_R10]),uint64_t(g[REG_R11]),
                                uint64_t(g[REG_R12]),uint64_t(g[REG_R13]),uint64_t(g[REG_R14]),uint64_t(g[REG_R15])};
        std::memcpy(r,linuxRegs,sizeof r);
#else
        uint64_t bsdRegs[16]={uint64_t(mc->mc_rax),uint64_t(mc->mc_rbx),uint64_t(mc->mc_rcx),uint64_t(mc->mc_rdx),uint64_t(mc->mc_rsi),uint64_t(mc->mc_rdi),
                              uint64_t(mc->mc_rbp),uint64_t(mc->mc_rsp),uint64_t(mc->mc_r8),uint64_t(mc->mc_r9),uint64_t(mc->mc_r10),uint64_t(mc->mc_r11),
                              uint64_t(mc->mc_r12),uint64_t(mc->mc_r13),uint64_t(mc->mc_r14),uint64_t(mc->mc_r15)};
        std::memcpy(r,bsdRegs,sizeof r);
#endif
        static const char* const names[16]={"rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","r8","r9","r10","r11","r12","r13","r14","r15"};
        char regs[640];int used=std::snprintf(regs,sizeof regs,"FALHA registradores:");
        for(int k=0;k<16&&used<int(sizeof regs)-40;++k)used+=std::snprintf(regs+used,sizeof regs-size_t(used)," %s=0x%llx",names[k],static_cast<unsigned long long>(r[k]));
        used+=std::snprintf(regs+used,sizeof regs-size_t(used),"\n");
        rawReport(regs,used);

    }
    char where[96];
    auto inModule=describeModuleAddress(rip);
    if(base&&rip>=base&&rip-base<size)std::snprintf(where,sizeof where,"engine+0x%llx",static_cast<unsigned long long>(rip-base));
    else if(!inModule.empty())std::snprintf(where,sizeof where,"%s",inModule.c_str());
    else std::snprintf(where,sizeof where,"0x%llx",static_cast<unsigned long long>(rip));
    char line[256];
    int n=std::snprintf(line,sizeof line,"FALHA FATAL: %s em %s, endereco acessado 0x%llx, rsp 0x%llx, codigo si %d, trap 0x%llx, err 0x%llx (bit 1 = escrita, bit 0 = pagina presente), thread do motor %u",
                        signalName(number),where,static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->si_addr)),
                        static_cast<unsigned long long>(rsp),info->si_code,static_cast<unsigned long long>(trap),static_cast<unsigned long long>(err),current?current->id:0);
    // A host thread, or a second fault while handling one (an unreadable
    // stack): reported, then the thread is parked. A title may not exit
    // through _exit: each try raised SIGSYS again, until the console reported
    // "FPU not available" (title 0.72).
    if(!current) {
        if(faultReport>=0&&n>0){(void)!write(faultReport,line,size_t(n));(void)!write(faultReport,"\n",1);fsync(faultReport);}
        for(;;)sleep(3600);
    }
    StopInfo stop;stop.reason=line;stop.returnAddress=rip;
    // Do not scan the faulting stack: even an address below stackTop can
    // belong to an inaccessible guard or a decommitted fiber page.
    // inFault stays set: stopRun reports and allocates in a heap that may be
    // the corrupted thing, and the thread ends in its host frame anyway.
    // The faulting engine thread ends in its host frame like any stop.
    stopRun(std::move(stop));
}
}
namespace {
constexpr unsigned StackWords=512;
std::atomic<uint64_t> sampledRip{0};
std::atomic<uintptr_t> sampleLimit{0};
uint64_t sampledStack[StackWords];
std::atomic<bool> sampleReady{false};
void onSample(int,siginfo_t*,void* context) {
    uint64_t rip=0,rsp=0;
#if defined(__linux__)
    auto uc=static_cast<ucontext_t*>(context);
    rip=uint64_t(uc->uc_mcontext.gregs[REG_RIP]);rsp=uint64_t(uc->uc_mcontext.gregs[REG_RSP]);
#else
    auto mc=reinterpret_cast<const mcontext_t*>(static_cast<const char*>(context)+0x40);
    rip=uint64_t(mc->mc_rip);rsp=uint64_t(mc->mc_rsp);
#endif
    // Only up to the thread's own host frame: above it the stack may end (title 0.48).
    auto stack=reinterpret_cast<const uint64_t*>(rsp);
    // The sampled thread's own bound: a fiber's stack when one runs (title 1.12).
    uintptr_t limit=current?current->stackTop:sampleLimit.load();
    for(unsigned k=0;k<StackWords;++k)
        sampledStack[k]=rsp+8*(k+1)<=limit?stack[k]:0;
    sampledRip=rip;sampleReady=true;
}
std::string where(uint64_t address) {
    uint64_t base=setup.image?setup.image->base():0,size=setup.image?setup.image->size():0;
    char text[64];
    if(base&&address>=base&&address-base<size){std::snprintf(text,sizeof text,"engine+0x%llx",static_cast<unsigned long long>(address-base));return text;}
    auto module=describeModuleAddress(address);
    if(!module.empty())return module;
    std::snprintf(text,sizeof text,"0x%llx",static_cast<unsigned long long>(address));return text;
}
}
std::string sampleGuestThreads() {
    static std::once_flag installed;
    std::call_once(installed,[]{
        struct sigaction action{};
        action.sa_sigaction=onSample;action.sa_flags=SA_SIGINFO|SA_RESTART;
        sigemptyset(&action.sa_mask);sigaction(SIGUSR2,&action,nullptr);
    });
    std::map<uint32_t,Sampled> copy;
    {std::lock_guard<std::mutex> guard(threadsLock);copy=threads;}
    std::string out=" "+std::to_string(copy.size())+" threads registradas; trava do GDI com "+win32::gdiOwner();
    uint64_t base=setup.image?setup.image->base():0,size=setup.image?setup.image->size():0;
    for(auto const& [id,entry]:copy) {
        auto thread=entry.thread;
        out+="\n    thread "+std::to_string(id)+": rip";
        std::string chain;
        for(int sample=0;sample<4;++sample) {
            sampleReady=false;sampleLimit=entry.limit;
            {std::lock_guard<std::mutex> guard(threadsLock);if(!threads.count(id))break;if(pthread_kill(thread,SIGUSR2)!=0)break;}
            int waited=0;while(!sampleReady&&waited<50){usleep(1000);++waited;}
            if(!sampleReady){out+=" (sem resposta)";break;}
            out+=" "+where(sampledRip);
            if(sample==0) {
                unsigned found=0;
                for(unsigned k=0;k<StackWords&&found<10;++k)
                    if(base&&sampledStack[k]>=base&&sampledStack[k]-base<size){chain+=" "+where(sampledStack[k]);++found;}
            }
            usleep(3000);
        }
        if(!chain.empty())out+=" | pilha do motor:"+chain;
    }
    return out;
}
void installFaultHandlers(int reportFd) {
    faultReport=reportFd;
    struct sigaction action{};
    // SA_ONSTACK: on the thread's alternate stack (runAsGuest), so a stack
    // overflow is reported too. SIGTRAP is the engine's int3 (__debugbreak),
    // SIGABRT and SIGSYS the other ways the process could end unreported
    // (title 0.71: it ended right after splitting its graphics heap, silently).
    action.sa_sigaction=onFault;action.sa_flags=SA_SIGINFO|SA_NODEFER|SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for(int number:{SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGTRAP,SIGABRT,SIGSYS})sigaction(number,&action,nullptr);
}
}

// Every Windows entry point calls this first (guest_abi.c).
extern "C" void hostGate(void){azimuth::leaveIfStopping();}
namespace {
constexpr unsigned RingSize=64;
// A thread's recent host calls. Rings are kept for the whole run, so a stop
// report can list what the other threads did too (a crash thread is rarely
// the one that reports it).
struct Ring {
    uint32_t thread=0;unsigned next=0;const char* names[RingSize]{};
    // The host functions entered and not yet returned (a callback into the engine nests them).
    std::atomic<unsigned> depth{0};const char* active[8]{};
    // Time inside the host (title 1.20, the open world ran at 11 frames a
    // second with 3.5 million host calls a second): the time-stamp counter at
    // each entry, the cycles per function (a small table by name pointer) and
    // the cycles of the outermost calls, read without a lock by the report.
    uint64_t entered[8]{};
    struct Stat {const char* name=nullptr;bool blocking=false;uint64_t cycles=0,count=0;};
    static constexpr unsigned StatSlots=256;
    Stat stats[StatSlots];
    uint64_t hostCycles=0;
};
std::mutex ringsLock;
std::vector<Ring*> rings;
thread_local Ring* ownRing=nullptr;
Ring* ringOf() {
    if(!ownRing) {
        auto ring=new Ring;ring->thread=azimuth::currentGuestThreadId();
        std::lock_guard<std::mutex> guard(ringsLock);
        rings.push_back(ring);ownRing=ring;
    }
    return ownRing;
}
// A name is a string literal of the host binary: a pointer outside the span
// of the exports table's names is not read (title 0.73: a ring overwritten in
// the heap faulted the report).
bool plausibleName(const char* name) {
    static const auto span=[] {
        uintptr_t low=~uintptr_t(0),high=0;
        for(auto e=guestExports;e->name;++e){auto p=reinterpret_cast<uintptr_t>(e->name);low=std::min(low,p);high=std::max(high,p);}
        return std::pair<uintptr_t,uintptr_t>(low-(16u<<20),high+(16u<<20));
    }();
    auto p=reinterpret_cast<uintptr_t>(name);
    return p>=span.first&&p<span.second;
}
std::string describeRing(const Ring& ring,unsigned count) {
    unsigned total=ring.next;
    if(count>RingSize)count=RingSize;
    if(count>total)count=total;
    std::string out;
    for(unsigned n=total-count;n<total;++n) {
        const char* name=ring.names[n%RingSize];if(!name)continue;
        if(!out.empty())out+=" ";
        out+=plausibleName(name)?name:"?";
    }
    return out;
}
}
extern "C" void hostGateNamed(const char* name) {
    auto ring=ringOf();
    ring->names[ring->next%RingSize]=name;
    ++ring->next;
    unsigned depth=ring->depth.load();
    if(depth<8){ring->active[depth]=name;ring->entered[depth]=__builtin_ia32_rdtsc();}
    ring->depth=depth+1;
    azimuth::leaveIfStopping();
}
extern "C" void hostLeave(int*) {
    auto ring=ringOf();
    unsigned depth=ring->depth.load();
    if(!depth)return;
    ring->depth=--depth;
    if(depth>=8)return;
    uint64_t cycles=__builtin_ia32_rdtsc()-ring->entered[depth];
    const char* name=ring->active[depth];
    unsigned slot=unsigned(reinterpret_cast<uintptr_t>(name)>>3)%Ring::StatSlots;
    for(unsigned probe=0;probe<8;++probe,slot=(slot+1)%Ring::StatSlots) {
        auto& stat=ring->stats[slot];
        if(!stat.name) {
            // Waits and sleeps are time asleep, not work: the thread's share
            // of host work leaves them out.
            stat.blocking=std::strstr(name,"Wait")||std::strstr(name,"Sleep")||std::strstr(name,"SwitchTo")||std::strstr(name,"GetQueuedCompletion")||std::strstr(name,"GetMessage");
            stat.name=name;
        }
        if(stat.name==name) {
            stat.cycles+=cycles;++stat.count;
            if(depth==0&&!stat.blocking)ring->hostCycles+=cycles;
            return;
        }
    }
}
std::string azimuth::recentHostCalls(unsigned count) {return describeRing(*ringOf(),count);}
// Where the host's time went since the last call: the functions with the most
// time inside them (all threads), and the threads with the largest share of
// their time inside the host. Cycles become seconds by the time-stamp counter's
// rate measured between two calls.
std::string azimuth::hostTimeReport(unsigned count) {
    static uint64_t tscBefore=0;static std::chrono::steady_clock::time_point timeBefore;
    static std::map<const char*,std::pair<uint64_t,uint64_t>> functionsBefore;
    static std::map<Ring*,uint64_t> threadsBefore;
    uint64_t tsc=__builtin_ia32_rdtsc();auto now=std::chrono::steady_clock::now();
    std::map<const char*,std::pair<uint64_t,uint64_t>> functions;
    std::vector<std::pair<uint64_t,Ring*>> threadCycles;
    {
        std::lock_guard<std::mutex> guard(ringsLock);
        for(auto ring:rings) {
            for(auto const& stat:ring->stats)if(stat.name){auto& f=functions[stat.name];f.first+=stat.cycles;f.second+=stat.count;}
            uint64_t host=ring->hostCycles;
            threadCycles.push_back({host-threadsBefore[ring],ring});threadsBefore[ring]=host;
        }
    }
    double seconds=std::chrono::duration<double>(now-timeBefore).count();
    bool first=tscBefore==0;
    double rate=first?0:double(tsc-tscBefore)/seconds;   // cycles per second
    tscBefore=tsc;timeBefore=now;
    std::vector<std::tuple<uint64_t,uint64_t,const char*>> deltas;
    for(auto const& [name,f]:functions) {
        auto& b=functionsBefore[name];
        if(f.first>b.first)deltas.push_back({f.first-b.first,f.second-b.second,name});
        b=f;
    }
    if(first||rate<=0)return "medindo o contador de ciclos";
    std::sort(deltas.begin(),deltas.end(),[](auto const& a,auto const& b){return std::get<0>(a)>std::get<0>(b);});
    std::sort(threadCycles.begin(),threadCycles.end(),[](auto const& a,auto const& b){return a.first>b.first;});
    std::string out="funcoes:";
    char text[160];
    for(size_t n=0;n<deltas.size()&&n<count;++n) {
        auto [cycles,calls,name]=deltas[n];
        double ms=double(cycles)/rate*1000.0/seconds;
        std::snprintf(text,sizeof text," %s %.0f ms/s (%.0f/s, %.0f ns)",plausibleName(name)?name:"?",ms,double(calls)/seconds,
                      calls?double(cycles)/rate*1e9/double(calls):0.0);
        out+=text;
    }
    out+=" | threads:";
    for(size_t n=0;n<threadCycles.size()&&n<count;++n) {
        if(!threadCycles[n].first)break;
        std::snprintf(text,sizeof text," %u %.0f%%",threadCycles[n].second->thread,100.0*double(threadCycles[n].first)/rate/seconds);
        out+=text;
    }
    return out;
}
std::string azimuth::busiestThreads(double seconds,unsigned count) {
    static std::map<Ring*,unsigned> before;
    std::vector<std::pair<unsigned,Ring*>> deltas;
    unsigned long long total=0;
    {
        std::lock_guard<std::mutex> guard(ringsLock);
        for(auto ring:rings) {
            unsigned now=ring->next,delta=now-before[ring];before[ring]=now;
            total+=delta;
            if(delta)deltas.push_back({delta,ring});
        }
    }
    std::sort(deltas.begin(),deltas.end(),[](auto const& a,auto const& b){return a.first>b.first;});
    char text[64];
    std::snprintf(text,sizeof text,"%.0f chamadas ao host/s;",double(total)/seconds);
    std::string out=text;
    for(size_t n=0;n<deltas.size()&&n<count;++n) {
        auto ring=deltas[n].second;
        const char* last=ring->names[(ring->next+RingSize-1)%RingSize];
        std::snprintf(text,sizeof text," thread %u: %.0f/s",ring->thread,double(deltas[n].first)/seconds);
        out+=text;out+=" (";out+=(last&&plausibleName(last))?last:"?";out+=")";
    }
    return out;
}
std::string azimuth::recentHostCallsAllThreads(unsigned count) {
    std::lock_guard<std::mutex> guard(ringsLock);
    std::string out;
    for(auto ring:rings)out+="\n    thread "+std::to_string(ring->thread)+": "+describeRing(*ring,count);
    return out;
}

// A diagnostic stub reached: runs on the engine thread with gs holding its
// TEB; records and unwinds, with no system call.
extern "C" __attribute__((noreturn)) void hostMissingImport(uint32_t slot,uint64_t returnAddress,const uint64_t* stack) {
    azimuth::StopInfo info;
    info.missingImport=true;info.slot=slot;info.returnAddress=returnAddress;
    auto base=azimuth::win32::process().imageBase;auto size=azimuth::win32::process().imageSize;
    // Only up to the thread's stack top (title 1.10: a thread pool callback near
    // its top read past it, and the crash hid the missing import).
    auto top=azimuth::currentStackTop();
    for(unsigned n=0;n<256&&info.trace.size()<12;++n) {
        if(top&&reinterpret_cast<uintptr_t>(stack+n+1)>top)break;
        if(stack[n]>=base&&stack[n]-base<size&&stack[n]!=returnAddress)info.trace.push_back(stack[n]);
    }
    azimuth::stopRun(std::move(info));
}
