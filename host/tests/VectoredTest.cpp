#include "../src/Teb.h"
#include "../src/GuestThreads.h"
#include "../src/win32/Win32.h"
#include "../src/win32/VectoredExceptions.h"
#include "../src/win32/WriteWatch.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ucontext.h>
#include <sys/wait.h>
#include <unistd.h>
#include <emmintrin.h>
#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT
namespace azimuth::win32 {
void log(const std::string& text){std::fprintf(stderr,"%s\n",text.c_str());}
void stopGuest(const std::string& text){std::fprintf(stderr,"STOP %s\n",text.c_str());std::exit(2);}
}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Record {uint32_t code,flags;uint64_t nested,address;uint32_t count,pad;uint64_t info[15];};
struct Pointers {Record* record;uint8_t* context;};
uint8_t *sealed=nullptr,*auxiliary=nullptr;
unsigned visits=0,order=0;int nestedFd=-1;
__attribute__((ms_abi)) uint32_t unseal(void* value){
    auto p=static_cast<Pointers*>(value);++visits;
    CHECK(p->record->code==0xc0000005&&p->record->count==2);
    CHECK(p->record->info[0]==1&&p->record->info[1]==reinterpret_cast<uint64_t>(sealed));
    CHECK(order==1);order=2;
    if(nestedFd>=0){CHECK(azimuth::win32::watchedPread(nestedFd,auxiliary,4096,65536)==4096);CHECK(auxiliary[0]==0x5b);}
    auxiliary[0]=42; // A tracking fault while already inside the guest VEH.
    uint32_t old=0;CHECK(win_VirtualProtect(sealed,16384,4,&old)&&old==2);
    return ~0u;
}
__attribute__((ms_abi)) uint32_t first(void*){order=1;return 0;}
__attribute__((ms_abi)) uint32_t mustNotRun(void*){CHECK(false);return 0;}
__attribute__((ms_abi)) uint32_t changeContext(void* value){
    auto p=static_cast<Pointers*>(value);uint64_t rax=1234,rip=5678;
    std::memcpy(p->context+0x78,&rax,8);std::memcpy(p->context+0xf8,&rip,8);return ~0u;
}
int main(){
    azimuth::ProcessBlock process(0);azimuth::setupGuest({nullptr,&process,{}});azimuth::installFaultHandlers(2);
    auto outcome=azimuth::runAsGuest(256,true,1<<20,[&]()->uint32_t{
        sealed=static_cast<uint8_t*>(win_VirtualAlloc(nullptr,65536,0x203000,4));CHECK(sealed);
        auxiliary=static_cast<uint8_t*>(win_VirtualAlloc(nullptr,65536,0x203000,4));CHECK(auxiliary);
        uint32_t old=0;CHECK(win_VirtualProtect(sealed,16384,2,&old));
        auto removed=win_AddVectoredExceptionHandler(0,reinterpret_cast<void*>(mustNotRun));
        CHECK(win_RemoveVectoredExceptionHandler(removed)==1);CHECK(!win_RemoveVectoredExceptionHandler(removed));
        auto a=win_AddVectoredExceptionHandler(0,reinterpret_cast<void*>(unseal));
        auto b=win_AddVectoredExceptionHandler(1,reinterpret_cast<void*>(first));
        // Same non-temporal instruction as the 1.29 console's first fault.
        _mm_stream_si64(reinterpret_cast<long long*>(sealed),0x12345678);_mm_sfence();
        CHECK(visits==1&&order==2&&*reinterpret_cast<uint64_t*>(sealed)==0x12345678);
        void* pages[16];uint64_t count=16;uint32_t gran=0;
        CHECK(!win_GetWriteWatch(1,sealed,65536,pages,&count,&gran)&&count>0);
        count=16;CHECK(!win_GetWriteWatch(1,auxiliary,65536,pages,&count,&gran)&&count>0);
        // A callback reads different data while the outer copy still owns
        // its source buffer. Sharing one TLS buffer would corrupt the result.
        auto input=std::tmpfile();CHECK(input);uint8_t content[65536];
        std::memset(content,0xa6,sizeof content);CHECK(std::fwrite(content,1,sizeof content,input)==sizeof content);
        std::memset(content,0x5b,sizeof content);CHECK(std::fwrite(content,1,sizeof content,input)==sizeof content);CHECK(!std::fflush(input));
        nestedFd=fileno(input);CHECK(win_VirtualProtect(sealed,16384,2,&old));order=0;
        CHECK(azimuth::win32::watchedPread(nestedFd,sealed,65536,0)==65536);
        for(unsigned i=0;i<65536;++i)CHECK(sealed[i]==0xa6);
        CHECK(visits==2&&order==2);nestedFd=-1;CHECK(!std::fclose(input));
        // Bulk CRT preparation must leave sealed pages to this same VEH.
        for(unsigned kind=0;kind<3;++kind){
            CHECK(win_VirtualProtect(sealed,16384,2,&old));order=0;
            auto previous=visits;
            if(kind==0){CHECK(win_memcpy(sealed,content,65536)==sealed);CHECK(!std::memcmp(sealed,content,65536));}
            if(kind==1){CHECK(win_memmove(sealed,auxiliary,65536)==sealed);CHECK(!std::memcmp(sealed,auxiliary,65536));}
            if(kind==2){CHECK(win_memset(sealed,0x72,65536)==sealed);for(unsigned i=0;i<65536;++i)CHECK(sealed[i]==0x72);}
            CHECK(visits==previous+1&&order==2);
        }
        CHECK(win_RemoveVectoredExceptionHandler(a));CHECK(win_RemoveVectoredExceptionHandler(b));
        auto c=win_AddVectoredExceptionHandler(0,reinterpret_cast<void*>(changeContext));
        ucontext_t uc{};CHECK(!getcontext(&uc));
        CHECK(azimuth::win32::dispatchAccessViolation(0x10000,0,&uc));
        CHECK(uc.uc_mcontext.gregs[REG_RAX]==1234&&uc.uc_mcontext.gregs[REG_RIP]==5678);
        CHECK(win_RemoveVectoredExceptionHandler(c));
        CHECK(win_VirtualFree(sealed,0,0x8000));CHECK(win_VirtualFree(auxiliary,0,0x8000));return 0;
    });
    CHECK(outcome.end==azimuth::GuestEnd::Returned);
    // An unhandled fault with invalid RDI must report and stop, not park in
    // a second fault while dereferencing RDI. Alarm makes that failure bounded.
    auto child=fork();CHECK(child>=0);
    if(!child){
        alarm(5);
        auto stopped=azimuth::runAsGuest(260,true,1<<20,[]()->uint32_t{
            __asm__ volatile("mov $0x60000, %%rdi; movb (%%rdi), %%al":::"rdi","rax","memory");return 0;
        });
        _exit(stopped.end==azimuth::GuestEnd::Stopped?0:1);
    }
    int status=0;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    std::puts("OK: VEH ordering/removal, MOVNTI unseal, nested tracking, context edits, fatal invalid RDI");
}
