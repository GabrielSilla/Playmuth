#include "VectoredExceptions.h"
#include "Win32.h"
#include "../guest_abi.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <ucontext.h>

namespace {
// Nodes live until process exit: removal may race with a signal dispatch.
struct Handler {void* function;std::atomic<bool> active{true};std::atomic<Handler*> next{nullptr};explicit Handler(void* f):function(f){}};
static_assert(std::atomic<Handler*>::is_always_lock_free,"signal-safe handler list");
static_assert(std::atomic<bool>::is_always_lock_free,"signal-safe removal");
std::atomic<Handler*> handlers{nullptr};
std::mutex handlersLock;
std::atomic<uint64_t> accessViolations{0},continuedViolations{0};
struct Record {uint32_t code=0xc0000005,flags=0;uint64_t nested=0,address=0;uint32_t count=2,pad=0;uint64_t information[15]{};};
struct alignas(16) Context {uint8_t bytes[0x4d0]{};};
static_assert(sizeof(Record)==152);
template<class T> void put(Context& c,unsigned offset,T value){std::memcpy(c.bytes+offset,&value,sizeof value);}
template<class T> T get(const Context& c,unsigned offset){T value;std::memcpy(&value,c.bytes+offset,sizeof value);return value;}
}
extern "C" void* win_AddVectoredExceptionHandler(uint32_t first,void* function){
    if(!function){azimuth::win32::setLastError(87);return nullptr;}
    auto node=new Handler(function);
    std::lock_guard<std::mutex> guard(handlersLock);
    if(first||!handlers.load()){
        node->next=handlers.load();handlers.store(node,std::memory_order_release);
    }else{
        auto tail=handlers.load();while(auto next=tail->next.load())tail=next;
        tail->next.store(node,std::memory_order_release);
    }
    return node;
}
extern "C" uint32_t win_RemoveVectoredExceptionHandler(void* handle){
    for(auto h=handlers.load(std::memory_order_acquire);h;h=h->next.load(std::memory_order_acquire))
        if(h==handle)return h->active.exchange(false)?1:0;
    return 0;
}
bool azimuth::win32::dispatchAccessViolation(uint64_t address,uint64_t access,void* nativeContext){
    if(!handlers.load(std::memory_order_acquire))return false;
    accessViolations.fetch_add(1,std::memory_order_relaxed);
    Context c;Record record;
#if defined(__linux__)
    auto uc=static_cast<ucontext_t*>(nativeContext);auto g=uc->uc_mcontext.gregs;
    const int indexes[]={REG_RAX,REG_RCX,REG_RDX,REG_RBX,REG_RSP,REG_RBP,REG_RSI,REG_RDI,REG_R8,REG_R9,REG_R10,REG_R11,REG_R12,REG_R13,REG_R14,REG_R15,REG_RIP};
    for(unsigned i=0;i<17;++i)put(c,0x78+i*8,uint64_t(g[indexes[i]]));
    put(c,0x44,uint32_t(g[REG_EFL]));
    if(uc->uc_mcontext.fpregs)std::memcpy(c.bytes+0x100,uc->uc_mcontext.fpregs,512);
#else
    // Console layout measured by the probe; SDK ucontext has a different prefix.
    auto mc=reinterpret_cast<mcontext_t*>(static_cast<char*>(nativeContext)+0x40);
    auto values={uint64_t(mc->mc_rax),uint64_t(mc->mc_rcx),uint64_t(mc->mc_rdx),uint64_t(mc->mc_rbx),uint64_t(mc->mc_rsp),uint64_t(mc->mc_rbp),uint64_t(mc->mc_rsi),uint64_t(mc->mc_rdi),uint64_t(mc->mc_r8),uint64_t(mc->mc_r9),uint64_t(mc->mc_r10),uint64_t(mc->mc_r11),uint64_t(mc->mc_r12),uint64_t(mc->mc_r13),uint64_t(mc->mc_r14),uint64_t(mc->mc_r15),uint64_t(mc->mc_rip)};
    unsigned index=0;for(auto value:values)put(c,0x78+index++*8,value);
    put(c,0x44,uint32_t(mc->mc_rflags));std::memcpy(c.bytes+0x100,mc->mc_fpstate,512);
#endif
    put(c,0x30,uint32_t(0x10000b)); // AMD64 CONTROL | INTEGER | FLOATING_POINT
    put(c,0x34,get<uint32_t>(c,0x118)); // MXCSR mirrored from FXSAVE
    record.address=get<uint64_t>(c,0xf8);record.information[0]=access;record.information[1]=address;
    struct {Record* record;void* context;} pointers{&record,c.bytes};
    for(auto h=handlers.load(std::memory_order_acquire);h;h=h->next.load(std::memory_order_acquire)){
        if(!h->active.load(std::memory_order_acquire))continue;
        int32_t result=int32_t(guestCallThreadStart(h->function,&pointers));
        if(result!=-1)continue;
        put(c,0x118,get<uint32_t>(c,0x34));
#if defined(__linux__)
        for(unsigned i=0;i<17;++i)g[indexes[i]]=get<uint64_t>(c,0x78+i*8);
        g[REG_EFL]=get<uint32_t>(c,0x44);
        if(uc->uc_mcontext.fpregs)std::memcpy(uc->uc_mcontext.fpregs,c.bytes+0x100,512);
#else
        mc->mc_rax=get<uint64_t>(c,0x78);mc->mc_rcx=get<uint64_t>(c,0x80);mc->mc_rdx=get<uint64_t>(c,0x88);mc->mc_rbx=get<uint64_t>(c,0x90);
        mc->mc_rsp=get<uint64_t>(c,0x98);mc->mc_rbp=get<uint64_t>(c,0xa0);mc->mc_rsi=get<uint64_t>(c,0xa8);mc->mc_rdi=get<uint64_t>(c,0xb0);
        mc->mc_r8=get<uint64_t>(c,0xb8);mc->mc_r9=get<uint64_t>(c,0xc0);mc->mc_r10=get<uint64_t>(c,0xc8);mc->mc_r11=get<uint64_t>(c,0xd0);
        mc->mc_r12=get<uint64_t>(c,0xd8);mc->mc_r13=get<uint64_t>(c,0xe0);mc->mc_r14=get<uint64_t>(c,0xe8);mc->mc_r15=get<uint64_t>(c,0xf0);mc->mc_rip=get<uint64_t>(c,0xf8);
        mc->mc_rflags=get<uint32_t>(c,0x44);std::memcpy(mc->mc_fpstate,c.bytes+0x100,512);
#endif
        continuedViolations.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    return false;
}

std::string azimuth::win32::vectoredExceptionReport(){
    return "Excecoes vetorizadas: "+std::to_string(accessViolations.exchange(0))+" acessos invalidos, "+std::to_string(continuedViolations.exchange(0))+" retomados pelo motor (intervalo de ~10 s)";
}
