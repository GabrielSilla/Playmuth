// KERNEL32 functions the engine's CRT calls while it starts: time, identity,
// errors, locks, fiber/thread local storage, the process heap, startup data,
// code pages and modules.
#include "Win32.h"
#include "../guest_abi.h"
#include "../GuestThreads.h"
#include "../HostHeap.h"
#include "../Modules.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <vector>
#include <unistd.h>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

namespace azimuth::win32 {
namespace {
Process current;
// Windows x64 TEB offsets.
constexpr size_t TebProcessId=0x40,TebThreadId=0x48,TebLastError=0x68,TebTlsSlots=0x1480,TebFlsData=0x17c8;
template<class T>T read(const uint8_t* at){T value;std::memcpy(&value,at,sizeof value);return value;}
template<class T>void write(uint8_t* at,T value){std::memcpy(at,&value,sizeof value);}

constexpr int64_t UnixToFileTime=116444736000000000LL;   // 100 ns units from 1601 to 1970
int64_t clockNs(clockid_t clock) {
    timespec now{};clock_gettime(clock,&now);
    return int64_t(now.tv_sec)*1000000000LL+now.tv_nsec;
}
}
void configure(const Process& process){current=process;}
const Process& process(){return current;}
uint8_t* teb(){uint8_t* self;__asm__ volatile("movq %%gs:0x30,%0":"=r"(self));return self;}
void setLastError(uint32_t error){write<uint32_t>(teb()+TebLastError,error);}
size_t length(const uint16_t* text){size_t n=0;if(text)while(text[n])++n;return n;}
std::string utf8(const uint16_t* text,size_t count) {
    std::string out;
    for(size_t n=0;n<count;++n) {
        uint32_t c=text[n];
        if(c>=0xd800&&c<0xdc00&&n+1<count&&text[n+1]>=0xdc00&&text[n+1]<0xe000)c=0x10000+((c-0xd800)<<10)+(text[++n]-0xdc00);
        if(c<0x80)out+=char(c);
        else if(c<0x800){out+=char(0xc0|c>>6);out+=char(0x80|(c&0x3f));}
        else if(c<0x10000){out+=char(0xe0|c>>12);out+=char(0x80|((c>>6)&0x3f));out+=char(0x80|(c&0x3f));}
        else {out+=char(0xf0|c>>18);out+=char(0x80|((c>>12)&0x3f));out+=char(0x80|((c>>6)&0x3f));out+=char(0x80|(c&0x3f));}
    }
    return out;
}
std::u16string utf16(const std::string& text) {
    std::u16string out;
    for(size_t n=0;n<text.size();) {
        uint32_t c=uint8_t(text[n]),extra=c>=0xf0?3:c>=0xe0?2:c>=0xc0?1:0;
        c=extra==3?c&7:extra==2?c&0xf:extra==1?c&0x1f:c;++n;
        for(uint32_t k=0;k<extra&&n<text.size();++k,++n)c=(c<<6)|(uint8_t(text[n])&0x3f);
        if(c>=0x10000){c-=0x10000;out+=char16_t(0xd800+(c>>10));out+=char16_t(0xdc00+(c&0x3ff));}
        else out+=char16_t(c);
    }
    return out;
}
}

using namespace azimuth::win32;

// ---- Time --------------------------------------------------------------
extern "C" void win_GetSystemTimeAsFileTime(void* fileTime) {
    int64_t value=clockNs(CLOCK_REALTIME)/100+UnixToFileTime;std::memcpy(fileTime,&value,8);
}
extern "C" void win_GetSystemTimePreciseAsFileTime(void* fileTime){win_GetSystemTimeAsFileTime(fileTime);}
extern "C" void win_GetSystemTime(void* systemTime) {
    int64_t ns=clockNs(CLOCK_REALTIME);time_t seconds=time_t(ns/1000000000);tm utc{};gmtime_r(&seconds,&utc);
    uint16_t fields[8]={uint16_t(utc.tm_year+1900),uint16_t(utc.tm_mon+1),uint16_t(utc.tm_wday),uint16_t(utc.tm_mday),
                        uint16_t(utc.tm_hour),uint16_t(utc.tm_min),uint16_t(utc.tm_sec),uint16_t(ns/1000000%1000)};
    std::memcpy(systemTime,fields,sizeof fields);
}
// SYSTEMTIME <-> FILETIME. The console's clock is UTC and the host keeps no
// time zone: local time is UTC.
namespace {
int64_t daysFromCivil(int64_t y,unsigned m,unsigned d) {
    y-=m<=2;int64_t era=(y>=0?y:y-399)/400;unsigned yoe=unsigned(y-era*400);
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+int64_t(doe)-719468;
}
}
extern "C" int32_t win_FileTimeToSystemTime(const int64_t* fileTime,uint16_t* systemTime) {
    if(!fileTime||!systemTime||*fileTime<0){setLastError(ErrorInvalidParameter);return 0;}
    int64_t ticks=*fileTime-UnixToFileTime;
    int64_t seconds=ticks>=0?ticks/10000000:-((-ticks+9999999)/10000000);
    time_t t=time_t(seconds);tm utc{};gmtime_r(&t,&utc);
    uint16_t fields[8]={uint16_t(utc.tm_year+1900),uint16_t(utc.tm_mon+1),uint16_t(utc.tm_wday),uint16_t(utc.tm_mday),
                        uint16_t(utc.tm_hour),uint16_t(utc.tm_min),uint16_t(utc.tm_sec),uint16_t((ticks-seconds*10000000)/10000)};
    std::memcpy(systemTime,fields,sizeof fields);return 1;
}
extern "C" int32_t win_SystemTimeToFileTime(const uint16_t* st,int64_t* fileTime) {
    if(!st||!fileTime||st[1]<1||st[1]>12||st[3]<1||st[3]>31||st[4]>23||st[5]>59||st[6]>59||st[7]>999){setLastError(ErrorInvalidParameter);return 0;}
    int64_t seconds=daysFromCivil(st[0],st[1],st[3])*86400+st[4]*3600+st[5]*60+st[6];
    *fileTime=seconds*10000000+int64_t(st[7])*10000+UnixToFileTime;return 1;
}
extern "C" int32_t win_FileTimeToLocalFileTime(const int64_t* in,int64_t* out){if(!in||!out){setLastError(ErrorInvalidParameter);return 0;}*out=*in;return 1;}
extern "C" int32_t win_LocalFileTimeToFileTime(const int64_t* in,int64_t* out){if(!in||!out){setLastError(ErrorInvalidParameter);return 0;}*out=*in;return 1;}
extern "C" int32_t win_SystemTimeToTzSpecificLocalTime(const void*,const uint16_t* in,uint16_t* out){if(!in||!out){setLastError(ErrorInvalidParameter);return 0;}std::memcpy(out,in,16);return 1;}
extern "C" void win_GetLocalTime(void* systemTime){win_GetSystemTime(systemTime);}
extern "C" int32_t win_CompareFileTime(const int64_t* first,const int64_t* second){return *first<*second?-1:*first>*second?1:0;}
// TIME_ZONE_INFORMATION (172 bytes), all zero: UTC with no daylight saving (TIME_ZONE_ID_UNKNOWN).
extern "C" uint32_t win_GetTimeZoneInformation(void* info){if(info)std::memset(info,0,172);return 0;}
// The clocks read the processor's time-stamp counter: clock_gettime is a
// system call on the console, ~950 ns (title 1.21 for the performance counter;
// title 1.26 for the rest: in the open world an engine thread read
// GetTickCount64 140 000 times a second, and every wait read the clock two or
// three times). The counter's rate is measured over 20 ms at first, then the
// anchor moves to the monotonic clock about once a second with the rate taken
// over the whole run, so the error stays in the microseconds. A reader takes
// the anchor under a sequence count; whichever reader finds it a second old
// moves it, never back.
namespace {
struct TscAnchor {std::atomic<uint32_t> sequence{0};std::atomic<uint64_t> tsc{0};std::atomic<int64_t> ns{0};std::atomic<double> rate{0};};
TscAnchor anchor;
uint64_t firstTsc=0,cyclesPerSecond=0;int64_t firstNs=0;
std::atomic<uint64_t> moveAt{0};
std::atomic_flag moving=ATOMIC_FLAG_INIT;
void publish(uint64_t tsc,int64_t ns,double rate) {
    anchor.sequence.fetch_add(1,std::memory_order_acq_rel);
    anchor.tsc.store(tsc,std::memory_order_relaxed);anchor.ns.store(ns,std::memory_order_relaxed);anchor.rate.store(rate,std::memory_order_relaxed);
    anchor.sequence.fetch_add(1,std::memory_order_release);
    moveAt.store(tsc+cyclesPerSecond,std::memory_order_relaxed);
}
void startTsc() {
    static std::once_flag started;
    std::call_once(started,[] {
        uint64_t a=__builtin_ia32_rdtsc();int64_t n0=clockNs(CLOCK_MONOTONIC);
        usleep(20000);
        uint64_t b=__builtin_ia32_rdtsc();int64_t n1=clockNs(CLOCK_MONOTONIC);
        double rate=double(n1-n0)/double(b-a);
        firstTsc=a;firstNs=n0;cyclesPerSecond=uint64_t(1e9/rate);
        publish(b,n1,rate);
    });
}
int64_t fromAnchor(uint64_t now) {
    for(;;) {
        auto before=anchor.sequence.load(std::memory_order_acquire);
        if(before&1)continue;
        auto tsc=anchor.tsc.load(std::memory_order_relaxed);auto ns=anchor.ns.load(std::memory_order_relaxed);auto rate=anchor.rate.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if(anchor.sequence.load(std::memory_order_relaxed)!=before)continue;
        return ns+int64_t(double(int64_t(now-tsc))*rate);
    }
}
}
int64_t azimuth::win32::fastMonotonicNs() {
    startTsc();
    uint64_t now=__builtin_ia32_rdtsc();
    if(now>=moveAt.load(std::memory_order_relaxed)&&!moving.test_and_set(std::memory_order_acquire)) {
        if(now>=moveAt.load(std::memory_order_relaxed)) {
            int64_t real=clockNs(CLOCK_MONOTONIC);uint64_t at=__builtin_ia32_rdtsc();
            int64_t fast=fromAnchor(at);
            publish(at,real>fast?real:fast,double(real-firstNs)/double(at-firstTsc));
        }
        moving.clear(std::memory_order_release);
    }
    return fromAnchor(now);
}
extern "C" uint32_t win_GetTickCount(void){return uint32_t(fastMonotonicNs()/1000000);}
// A 10 MHz performance counter (100 ns ticks), as Windows reports on most machines.
extern "C" int32_t win_QueryPerformanceCounter(int64_t* count){*count=fastMonotonicNs()/100;return 1;}
extern "C" int32_t win_QueryPerformanceFrequency(int64_t* frequency){*frequency=10000000;return 1;}
extern "C" uint64_t win_GetTickCount64(void){return uint64_t(fastMonotonicNs()/1000000);}

// ---- Identity and errors ------------------------------------------------
extern "C" uint32_t win_GetCurrentProcessId(void){return uint32_t(read<uint64_t>(teb()+TebProcessId));}
// The engine runs as a desktop process with no package identity, as on Windows:
// APPMODEL_ERROR_NO_PACKAGE (title 0.98: looked up just before the game's COMException).
namespace {
int32_t noPackage(uint32_t* length) {
    constexpr int32_t AppModelErrorNoPackage=15700;
    static std::atomic<bool> said{false};
    if(!said.exchange(true))log("GetCurrentPackage*: processo sem pacote (APPMODEL_ERROR_NO_PACKAGE), como o motor no Windows");
    if(length)*length=0;
    return AppModelErrorNoPackage;
}
}
extern "C" int32_t win_GetCurrentPackageId(uint32_t* length,uint8_t*){return noPackage(length);}
extern "C" int32_t win_GetCurrentPackageFullName(uint32_t* length,uint16_t*){return noPackage(length);}
extern "C" int32_t win_GetCurrentPackageFamilyName(uint32_t* length,uint16_t*){return noPackage(length);}
extern "C" uint32_t win_GetCurrentThreadId(void){return uint32_t(read<uint64_t>(teb()+TebThreadId));}
extern "C" void* win_GetCurrentProcess(void){return reinterpret_cast<void*>(intptr_t(-1));}
extern "C" void* win_GetCurrentThread(void){return reinterpret_cast<void*>(intptr_t(-2));}
extern "C" uint32_t win_GetLastError(void){return read<uint32_t>(teb()+TebLastError);}
extern "C" void win_SetLastError(uint32_t error){setLastError(error);}
// Encoding is the identity: the guest only needs Decode(Encode(p)) == p.
extern "C" void* win_EncodePointer(void* pointer){return pointer;}
extern "C" void* win_DecodePointer(void* pointer){return pointer;}
extern "C" int32_t win_IsDebuggerPresent(void){return 0;}
// The PS5's Zen 2: what Windows reports there (no 3DNow!, no AVX-512; the
// FS/GS base instructions are not reported, gs belongs to the TEB).
extern "C" int32_t win_IsProcessorFeaturePresent(uint32_t feature) {
    switch(feature) {
        case 2:case 3:case 6:case 8:case 9:case 10:case 12:case 13:case 14:case 17:case 23:case 28:case 32:
        case 36:case 37:case 38:case 39:case 40:return 1;
        default:return 0;
    }
}
extern "C" void win_OutputDebugStringA(const char* text){if(text)log(std::string("OutputDebugString do motor: ")+text);}
extern "C" void win_OutputDebugStringW(const uint16_t* text){if(text)log("OutputDebugString do motor: "+utf8(text,length(text)));}
extern "C" uint32_t win_SetErrorMode(uint32_t){return 0;}
// OSVERSIONINFOW (276 bytes) or OSVERSIONINFOEXW (284): Windows 10 22H2, the build the PC kit reports.
extern "C" int32_t win_GetVersionExW(void* info) {
    uint32_t size;if(!info){setLastError(ErrorInvalidParameter);return 0;}
    std::memcpy(&size,info,4);
    if(size!=276&&size!=284){setLastError(ErrorInsufficientBuffer);return 0;}
    auto p=static_cast<uint8_t*>(info);std::memset(p+4,0,size-4);
    uint32_t fields[4]={10,0,19045,2};std::memcpy(p+4,fields,16);   // major, minor, build, VER_PLATFORM_WIN32_NT
    if(size==284){uint16_t product=1;std::memcpy(p+282,&product,1);}   // wProductType VER_NT_WORKSTATION
    return 1;
}
namespace {std::atomic<void*> unhandledFilter{nullptr};}
extern "C" void* win_SetUnhandledExceptionFilter(void* filter){return unhandledFilter.exchange(filter);}
extern "C" int32_t win_TerminateProcess(void* process,uint32_t code) {
    if(process!=win_GetCurrentProcess()){setLastError(ErrorInvalidHandle);return 0;}
    stopGuest("o motor chamou TerminateProcess, codigo "+std::to_string(code));
}
extern "C" void win_ExitProcess(uint32_t code){stopGuest("o motor chamou ExitProcess, codigo "+std::to_string(code));}

// ---- Synchronization ----------------------------------------------------
// Blocking acquisitions wake every 100 ms: once the run is stopping, the
// waiting engine thread ends instead of waiting for a lock that may never be
// released.
// CRITICAL_SECTION (40 bytes): DebugInfo, LockCount, RecursionCount,
// OwningThread, LockSemaphore, SpinCount. Since title 1.14 a section's state
// is its own fields and no kernel object: OwningThread is the owner, taken with
// a compare-exchange; RecursionCount its count; LockSemaphore the number of
// waiters, who sleep on one of 64 shared mutex and condition pairs chosen by
// the section's address. Title 1.13 gave each section a mutex and a condition
// variable; on the console each is an object of libkernel's internal pool, and
// the game's thousands of sections used it up (SIGABRT in libkernel naming
// "SceKernelInternalMemory", two threads dead in EnterCriticalSection). As on
// Windows any thread may leave a section (title 1.12's recursive pthread mutex
// refused that and stayed held), and a wait of 10 s is reported with the owner.
std::string stackCallers();
namespace {
constexpr size_t CsLockCount=8,CsRecursion=12,CsOwner=16,CsWaiters=24,CsSpin=32;
struct Bucket {std::mutex lock;std::condition_variable freed;std::atomic<unsigned> sleepers{0};};
Bucket buckets[64];
Bucket& bucketOf(void* section){return buckets[(reinterpret_cast<uintptr_t>(section)>>4)%64];}
uint64_t* ownerOf(void* section){return reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(section)+CsOwner);}
uint64_t* waitersOf(void* section){return reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(section)+CsWaiters);}
int32_t* recursionOf(void* section){return reinterpret_cast<int32_t*>(static_cast<uint8_t*>(section)+CsRecursion);}
bool claim(void* section,uint64_t me) {
    uint64_t none=0;
    return __atomic_compare_exchange_n(ownerOf(section),&none,me,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
void entered(void* section){*recursionOf(section)=1;write<int32_t>(static_cast<uint8_t*>(section)+CsLockCount,0);}
std::atomic<unsigned> sectionReports{0};
}
extern "C" int32_t win_InitializeCriticalSectionEx(void* section,uint32_t spin,uint32_t) {
    auto p=static_cast<uint8_t*>(section);std::memset(p,0,40);
    // DebugInfo is -1 ("no debug information", as on Windows 8 and later and the
    // Xbox One), never 0: the game takes a zero DebugInfo for a section not yet
    // initialized and spins forever before TryEnterCriticalSection (title 0.88).
    write<int64_t>(p,-1);
    write<int32_t>(p+CsLockCount,-1);write<uint64_t>(p+CsSpin,spin);
    return 1;
}
extern "C" int32_t win_InitializeCriticalSectionAndSpinCount(void* section,uint32_t spin){return win_InitializeCriticalSectionEx(section,spin,0);}
extern "C" void win_EnterCriticalSection(void* section) {
    uint64_t me=win_GetCurrentThreadId();
    if(__atomic_load_n(ownerOf(section),__ATOMIC_SEQ_CST)==me){++*recursionOf(section);return;}
    for(int spin=0;spin<64;++spin) {
        if(claim(section,me)){entered(section);return;}
        __builtin_ia32_pause();
    }
    auto& bucket=bucketOf(section);
    std::unique_lock<std::mutex> guard(bucket.lock);
    __atomic_fetch_add(waitersOf(section),1,__ATOMIC_SEQ_CST);
    auto reportAt=std::chrono::steady_clock::now()+std::chrono::seconds(10);bool reported=false;
    while(!claim(section,me)) {
        bucket.freed.wait_for(guard,std::chrono::milliseconds(100));
        if(azimuth::runStopping()) {
            __atomic_fetch_sub(waitersOf(section),1,__ATOMIC_SEQ_CST);
            guard.unlock();azimuth::leaveIfStopping();guard.lock();
            __atomic_fetch_add(waitersOf(section),1,__ATOMIC_SEQ_CST);
        }
        if(!reported&&std::chrono::steady_clock::now()>=reportAt) {
            reported=true;
            if(sectionReports++<60) {
                char text[160];
                std::snprintf(text,sizeof text,"Secao critica %p ocupada: a thread %u espera ha 10 s; dona thread %llu (recursao %d)",
                              section,uint32_t(me),static_cast<unsigned long long>(__atomic_load_n(ownerOf(section),__ATOMIC_SEQ_CST)),*recursionOf(section));
                guard.unlock();log(std::string(text)+"; chamado de"+stackCallers());guard.lock();
            }
        }
    }
    __atomic_fetch_sub(waitersOf(section),1,__ATOMIC_SEQ_CST);
    guard.unlock();
    entered(section);
}
extern "C" void win_LeaveCriticalSection(void* section) {
    uint64_t me=win_GetCurrentThreadId();
    uint64_t owner=__atomic_load_n(ownerOf(section),__ATOMIC_SEQ_CST);
    if(!owner||*recursionOf(section)<=0)return;
    if(owner!=me) {
        static std::atomic<unsigned> foreign{0};
        if(foreign++<10) {
            char text[128];
            std::snprintf(text,sizeof text,"LeaveCriticalSection de %p pela thread %u, dona %llu (permitido no Windows)",section,uint32_t(me),static_cast<unsigned long long>(owner));
            log(text);
        }
    }
    if(--*recursionOf(section)>0)return;
    write<int32_t>(static_cast<uint8_t*>(section)+CsLockCount,-1);
    __atomic_store_n(ownerOf(section),0,__ATOMIC_SEQ_CST);
    if(__atomic_load_n(waitersOf(section),__ATOMIC_SEQ_CST)) {
        auto& bucket=bucketOf(section);
        std::lock_guard<std::mutex> guard(bucket.lock);
        bucket.freed.notify_all();
    }
}
extern "C" void win_DeleteCriticalSection(void* section){std::memset(section,0,40);}
// SRWLOCK is one pointer-sized word, zero when initialized. Since title 1.14
// that word is the lock (bit 0: held exclusively; the readers counted in steps
// of 2), with no kernel object: a pthread rwlock per lock, never freed since
// SRW locks have no delete, drew on libkernel's internal pool like the
// critical sections above. Waiters sleep on the same shared buckets.
namespace {
uint64_t* srwWord(void* lock){return static_cast<uint64_t*>(lock);}
bool tryExclusive(void* lock) {
    uint64_t none=0;
    return __atomic_compare_exchange_n(srwWord(lock),&none,1,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
bool tryShared(void* lock) {
    uint64_t value=__atomic_load_n(srwWord(lock),__ATOMIC_SEQ_CST);
    while(!(value&1))
        if(__atomic_compare_exchange_n(srwWord(lock),&value,value+2,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return true;
    return false;
}
template<class Attempt>void srwWait(void* lock,Attempt attempt) {
    for(int spin=0;spin<64;++spin){if(attempt())return;__builtin_ia32_pause();}
    auto& bucket=bucketOf(lock);
    std::unique_lock<std::mutex> guard(bucket.lock);
    ++bucket.sleepers;
    while(!attempt()) {
        bucket.freed.wait_for(guard,std::chrono::milliseconds(100));
        if(azimuth::runStopping()){--bucket.sleepers;guard.unlock();azimuth::leaveIfStopping();guard.lock();++bucket.sleepers;}
    }
    --bucket.sleepers;
}
void srwWake(void* lock) {
    auto& bucket=bucketOf(lock);
    if(!bucket.sleepers.load())return;
    std::lock_guard<std::mutex> guard(bucket.lock);
    bucket.freed.notify_all();
}
}
extern "C" void win_AcquireSRWLockExclusive(void* lock){srwWait(lock,[&]{return tryExclusive(lock);});}
extern "C" void win_ReleaseSRWLockExclusive(void* lock){__atomic_store_n(srwWord(lock),0,__ATOMIC_SEQ_CST);srwWake(lock);}
extern "C" void win_AcquireSRWLockShared(void* lock){srwWait(lock,[&]{return tryShared(lock);});}
extern "C" void win_ReleaseSRWLockShared(void* lock) {
    if(__atomic_sub_fetch(srwWord(lock),2,__ATOMIC_SEQ_CST)==0)srwWake(lock);
}
extern "C" uint8_t win_TryAcquireSRWLockExclusive(void* lock){return tryExclusive(lock);}
extern "C" void win_InitializeSListHead(void* head){std::memset(head,0,16);}
// Interlocked singly linked lists: SLIST_HEADER holds the first entry at 0 and
// the depth at 8; an entry's first 8 bytes point to the next. One lock for all.
namespace {
std::mutex slistLock;
}
extern "C" void* win_InterlockedPushEntrySList(void* head,void* entry) {
    std::lock_guard<std::mutex> guard(slistLock);
    auto h=static_cast<uint64_t*>(head);void* first=reinterpret_cast<void*>(h[0]);
    std::memcpy(entry,&first,8);h[0]=reinterpret_cast<uint64_t>(entry);h[1]=(h[1]&0xffff)+1;
    return first;
}
extern "C" void* win_InterlockedPopEntrySList(void* head) {
    std::lock_guard<std::mutex> guard(slistLock);
    auto h=static_cast<uint64_t*>(head);void* first=reinterpret_cast<void*>(h[0]);
    if(first){uint64_t next;std::memcpy(&next,first,8);h[0]=next;h[1]=(h[1]&0xffff)-1;}
    return first;
}
extern "C" void* win_InterlockedFlushSList(void* head) {
    std::lock_guard<std::mutex> guard(slistLock);
    auto h=static_cast<uint64_t*>(head);void* first=reinterpret_cast<void*>(h[0]);h[0]=0;h[1]=0;
    return first;
}
extern "C" uint16_t win_QueryDepthSList(void* head){std::lock_guard<std::mutex> guard(slistLock);return uint16_t(static_cast<uint64_t*>(head)[1]);}

// ---- Fiber and thread local storage ------------------------------------
// TLS: the TEB's 64 TlsSlots. FLS: a per-thread array the TEB's FlsData
// points to. The destructor callbacks are kept for thread exit.
namespace {
constexpr uint32_t TlsCount=64,FlsCount=128,OutOfIndexes=0xffffffff;
std::mutex slotsLock;
bool tlsUsed[TlsCount],flsUsed[FlsCount];
void* flsCallbacks[FlsCount];
void** flsData(bool create) {
    auto data=read<void**>(teb()+TebFlsData);
    // The host's allocator, not malloc: title 0.33's thread 280 got no FLS block,
    // FlsSetValue failed, and the UCRT's per-thread data (__acrt_getptd) aborted.
    if(!data&&create){data=static_cast<void**>(azimuth::heapAlloc(FlsCount*sizeof(void*),16,true));write(teb()+TebFlsData,data);}
    return data;
}
}
extern "C" uint32_t win_FlsAlloc(void* callback) {
    std::lock_guard<std::mutex> lock(slotsLock);
    for(uint32_t n=1;n<FlsCount;++n)if(!flsUsed[n]){flsUsed[n]=true;flsCallbacks[n]=callback;return n;}
    setLastError(ErrorNotEnoughMemory);return OutOfIndexes;
}
extern "C" int32_t win_FlsFree(uint32_t index) {
    std::lock_guard<std::mutex> lock(slotsLock);
    if(index>=FlsCount||!flsUsed[index]){setLastError(ErrorInvalidParameter);return 0;}
    flsUsed[index]=false;flsCallbacks[index]=nullptr;
    if(auto data=flsData(false))data[index]=nullptr;
    return 1;
}
extern "C" void* win_FlsGetValue(uint32_t index) {
    if(index>=FlsCount||!flsUsed[index]){setLastError(ErrorInvalidParameter);return nullptr;}
    auto data=flsData(false);return data?data[index]:nullptr;
}
extern "C" int32_t win_FlsSetValue(uint32_t index,void* value) {
    if(index>=FlsCount||!flsUsed[index]){setLastError(ErrorInvalidParameter);return 0;}
    auto data=flsData(true);if(!data){setLastError(ErrorNotEnoughMemory);return 0;}
    data[index]=value;return 1;
}
// On an engine thread's way out: each FLS slot with a destructor and a value
// gets it called, as Windows does at thread exit.
namespace azimuth::win32 {
void runFlsCallbacks() {
    auto data=flsData(false);if(!data)return;
    for(uint32_t n=1;n<FlsCount;++n) {
        void* callback;{std::lock_guard<std::mutex> lock(slotsLock);callback=flsUsed[n]?flsCallbacks[n]:nullptr;}
        void* value=data[n];
        if(callback&&value){data[n]=nullptr;guestCallFlsCallback(callback,value);}
    }
    write<void**>(teb()+TebFlsData,nullptr);
    azimuth::heapFree(data);
}
}
extern "C" uint32_t win_TlsAlloc(void) {
    std::lock_guard<std::mutex> lock(slotsLock);
    for(uint32_t n=0;n<TlsCount;++n)if(!tlsUsed[n]){tlsUsed[n]=true;return n;}
    setLastError(ErrorNotEnoughMemory);return OutOfIndexes;
}
extern "C" int32_t win_TlsFree(uint32_t index) {
    std::lock_guard<std::mutex> lock(slotsLock);
    if(index>=TlsCount||!tlsUsed[index]){setLastError(ErrorInvalidParameter);return 0;}
    tlsUsed[index]=false;write<void*>(teb()+TebTlsSlots+index*8,nullptr);return 1;
}
extern "C" void* win_TlsGetValue(uint32_t index) {
    if(index>=TlsCount){setLastError(ErrorInvalidParameter);return nullptr;}
    setLastError(ErrorSuccess);return read<void*>(teb()+TebTlsSlots+index*8);
}
extern "C" int32_t win_TlsSetValue(uint32_t index,void* value) {
    if(index>=TlsCount){setLastError(ErrorInvalidParameter);return 0;}
    write(teb()+TebTlsSlots+index*8,value);return 1;
}

// ---- Heap ---------------------------------------------------------------
// One process heap over malloc. Each block carries its size in a 16-byte
// header, keeping the 16-byte alignment Windows heap blocks have.
namespace {
constexpr uint32_t HeapZeroMemory=0x8,HeapReallocInPlaceOnly=0x10;
constexpr size_t Header=16;
char processHeap;
uint8_t* blockHeader(const void* block){return static_cast<uint8_t*>(const_cast<void*>(block))-Header;}
void* allocate(uint64_t size,bool zero) {
    void* raw=size>uint64_t(1)<<40?nullptr:azimuth::heapAlloc(size+Header,16,false);
    if(!raw){setLastError(ErrorNotEnoughMemory);return nullptr;}
    write<uint64_t>(static_cast<uint8_t*>(raw),size);
    auto block=static_cast<uint8_t*>(raw)+Header;if(zero)std::memset(block,0,size);
    return block;
}
}
extern "C" void* win_GetProcessHeap(void){return &processHeap;}
// HeapCompatibilityInformation (0): 2, the low-fragmentation heap, as on Windows 10.
extern "C" int32_t win_HeapQueryInformation(void*,uint32_t kind,void* info,uint64_t size,uint64_t* returned) {
    if(kind!=0||!info||size<4){setLastError(ErrorInvalidParameter);return 0;}
    uint32_t value=2;std::memcpy(info,&value,4);if(returned)*returned=4;return 1;
}
extern "C" int32_t win_HeapValidate(void*,uint32_t,const void*){return 1;}
extern "C" uint64_t win_HeapCompact(void*,uint32_t){return 1u<<20;}
extern "C" int32_t win_HeapWalk(void*,void*){setLastError(259);return 0;}   // ERROR_NO_MORE_ITEMS
// Code addresses on the calling thread's stack (the engine, the game and the
// DLLs it maps, not the host's own text), for the log.
std::string stackCallers() {
    auto stack=static_cast<const uint64_t*>(__builtin_frame_address(0));
    // Only up to the thread's host frame (title 0.84: past it, a new thread's
    // stack ended and this report faulted).
    uintptr_t top=azimuth::currentStackTop();
    auto inside=[&](unsigned n){return top&&reinterpret_cast<uintptr_t>(stack+n+1)<=top;};
    std::string out;unsigned found=0;
    for(unsigned n=0;n<256&&found<8&&inside(n);++n) {
        uint64_t v=stack[n];
        if(v>=0x100000000ull&&v<0x300000000ull&&!(v>=0x100400000ull&&v<0x100600000ull)){char text[24];std::snprintf(text,sizeof text," 0x%llx",(unsigned long long)v);out+=text;++found;}
    }
    return out;
}
extern "C" void* win_HeapAlloc(void*,uint32_t flags,uint64_t size) {
    auto block=allocate(size,flags&HeapZeroMemory);
    // Large blocks, with who asked (title 0.81: the game's pool of 10-byte
    // records wrote far past the table it had been given).
    if(size>=(1u<<20)) {
        static std::atomic<unsigned> logged{0};
        if(logged++<200)log("HeapAlloc de "+std::to_string(size)+" bytes = "+std::to_string(reinterpret_cast<uint64_t>(block))+"; chamado de"+stackCallers());
    }
    return block;
}
extern "C" int32_t win_HeapFree(void*,uint32_t,void* block){if(block)azimuth::heapFree(blockHeader(block));return 1;}
extern "C" uint64_t win_HeapSize(void*,uint32_t,const void* block) {
    if(!block){setLastError(ErrorInvalidParameter);return ~uint64_t(0);}
    return read<uint64_t>(blockHeader(block));
}
extern "C" void* win_HeapReAlloc(void* heap,uint32_t flags,void* block,uint64_t size) {
    if(!block)return win_HeapAlloc(heap,flags,size);
    uint64_t old=read<uint64_t>(blockHeader(block));
    if(size<=old&&(flags&HeapReallocInPlaceOnly)){write<uint64_t>(blockHeader(block),size);return block;}
    if(flags&HeapReallocInPlaceOnly){setLastError(ErrorNotEnoughMemory);return nullptr;}
    auto moved=allocate(size,false);if(!moved)return nullptr;
    std::memcpy(moved,block,size<old?size:old);
    if(size>old&&(flags&HeapZeroMemory))std::memset(static_cast<uint8_t*>(moved)+old,0,size-old);
    azimuth::heapFree(blockHeader(block));
    return moved;
}
extern "C" void* win_LocalFree(void* block){if(block)azimuth::heapFree(blockHeader(block));return nullptr;}

// ---- Startup data -------------------------------------------------------
extern "C" void win_GetStartupInfoW(void* info){std::memset(info,0,104);write<uint32_t>(static_cast<uint8_t*>(info),104);}
namespace {
std::string commandA;std::u16string commandW;std::once_flag commandOnce;
void prepareCommand(){std::call_once(commandOnce,[]{commandA=process().commandLine;commandW=utf16(commandA);});}
// A GUI process environment: no console, a minimal set of variables.
const char16_t environment[]=u"SystemRoot=C:\\Windows\0\0";
}
extern "C" char* win_GetCommandLineA(void){prepareCommand();return commandA.data();}
extern "C" uint16_t* win_GetCommandLineW(void){prepareCommand();return reinterpret_cast<uint16_t*>(commandW.data());}
extern "C" uint16_t* win_GetEnvironmentStringsW(void){return reinterpret_cast<uint16_t*>(const_cast<char16_t*>(environment));}
extern "C" int32_t win_FreeEnvironmentStringsW(uint16_t*){return 1;}
// A GUI process has no standard handles.
extern "C" void* win_GetStdHandle(uint32_t){return nullptr;}
extern "C" int32_t win_SetStdHandle(uint32_t,void*){return 1;}

// ---- Code pages ---------------------------------------------------------
// ANSI is Windows-1252 (Western European Windows), OEM is 437; UTF-8 and
// ASCII are also understood.
namespace {
constexpr uint32_t CpAcp=0,CpOemcp=1,Cp437=437,Cp1252=1252,CpAscii=20127,CpUtf8=65001,MbErrInvalidChars=8;
const uint16_t cp1252High[32]={0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
                               0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178};
uint32_t resolve(uint32_t codePage){return codePage==CpAcp?Cp1252:codePage==CpOemcp?Cp437:codePage;}
bool known(uint32_t codePage){codePage=resolve(codePage);return codePage==Cp1252||codePage==Cp437||codePage==CpAscii||codePage==CpUtf8;}
// Decodes into UTF-16; false for invalid UTF-8 when strict.
bool decode(uint32_t codePage,const uint8_t* text,size_t count,bool strict,std::u16string& out) {
    if(codePage!=CpUtf8) {
        for(size_t n=0;n<count;++n) {
            uint8_t c=text[n];
            out+=char16_t(codePage==Cp1252&&c>=0x80&&c<0xa0?cp1252High[c-0x80]:codePage==CpAscii&&c>=0x80?0xfffd:c);
        }
        return true;
    }
    for(size_t n=0;n<count;) {
        uint8_t c=text[n];uint32_t extra=c<0x80?0:(c&0xe0)==0xc0?1:(c&0xf0)==0xe0?2:(c&0xf8)==0xf0?3:4;
        uint32_t value=extra==0?c:extra==1?c&0x1f:extra==2?c&0xf:c&7;
        bool valid=extra<4&&n+extra<count;
        for(uint32_t k=1;valid&&k<=extra;++k){uint8_t d=text[n+k];if((d&0xc0)!=0x80)valid=false;else value=(value<<6)|(d&0x3f);}
        if(valid)valid=!(extra==1&&value<0x80)&&!(extra==2&&(value<0x800||(value>=0xd800&&value<0xe000)))&&!(extra==3&&(value<0x10000||value>0x10ffff));
        if(!valid){if(strict)return false;out+=char16_t(0xfffd);++n;continue;}
        n+=extra+1;
        if(value>=0x10000){value-=0x10000;out+=char16_t(0xd800+(value>>10));out+=char16_t(0xdc00+(value&0x3ff));}
        else out+=char16_t(value);
    }
    return true;
}
}
extern "C" uint32_t win_GetACP(void){return Cp1252;}
extern "C" uint32_t win_GetOEMCP(void){return Cp437;}
extern "C" int32_t win_IsValidCodePage(uint32_t codePage){return codePage!=CpAcp&&codePage!=CpOemcp&&known(codePage);}
// CPINFO: MaxCharSize, DefaultChar[2], LeadByte[12].
extern "C" int32_t win_GetCPInfo(uint32_t codePage,void* info) {
    if(!known(codePage)||!info){setLastError(ErrorInvalidParameter);return 0;}
    auto p=static_cast<uint8_t*>(info);std::memset(p,0,20);
    write<uint32_t>(p,resolve(codePage)==CpUtf8?4:1);p[4]='?';
    return 1;
}
extern "C" int32_t win_MultiByteToWideChar(uint32_t codePage,uint32_t flags,const char* source,int32_t sourceLength,uint16_t* target,int32_t targetLength) {
    if(!source||!known(codePage)||targetLength<0||(targetLength&&!target)){setLastError(ErrorInvalidParameter);return 0;}
    size_t count=sourceLength<0?std::strlen(source)+1:size_t(sourceLength);
    std::u16string out;
    if(!decode(resolve(codePage),reinterpret_cast<const uint8_t*>(source),count,flags&MbErrInvalidChars,out)){setLastError(ErrorNoUnicodeTranslation);return 0;}
    if(!targetLength)return int32_t(out.size());
    if(out.size()>size_t(targetLength)){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(target,out.data(),out.size()*2);return int32_t(out.size());
}
extern "C" int32_t win_WideCharToMultiByte(uint32_t codePage,uint32_t,const uint16_t* source,int32_t sourceLength,char* target,int32_t targetLength,
                                           const char* defaultChar,int32_t* usedDefault) {
    if(!source||!known(codePage)||targetLength<0||(targetLength&&!target)){setLastError(ErrorInvalidParameter);return 0;}
    size_t count=sourceLength<0?length(source)+1:size_t(sourceLength);
    std::string out;bool substituted=false;auto page=resolve(codePage);
    if(page==CpUtf8)out=utf8(source,count);
    else for(size_t n=0;n<count;++n) {
        uint16_t c=source[n];int mapped=-1;
        if(c<0x80||(c>=0xa0&&c<0x100&&page!=CpAscii))mapped=c;
        else if(page==Cp1252)for(int k=0;k<32;++k)if(cp1252High[k]==c&&c!=0xfffd)mapped=0x80+k;
        if(mapped<0){mapped=uint8_t(defaultChar?*defaultChar:'?');substituted=true;}
        out+=char(mapped);
    }
    if(usedDefault)*usedDefault=substituted;
    if(!targetLength)return int32_t(out.size());
    if(out.size()>size_t(targetLength)){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(target,out.data(),out.size());return int32_t(out.size());
}

// ---- Modules ------------------------------------------------------------
// The engine is the only image. A library the host implements answers
// GetModuleHandle with a handle of its own, so GetProcAddress finds its
// functions in the export table; anything else is reported and refused.
namespace {
struct Library {std::string name;};
std::vector<Library>& libraries() {
    static std::vector<Library> list=[] {
        std::vector<Library> names;
        for(auto e=guestExports;e->name;++e) {
            bool seen=false;for(auto const& l:names)if(l.name==e->library)seen=true;
            if(!seen)names.push_back({e->library});
        }
        return names;
    }();
    return list;
}
std::string lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
std::string baseName(std::string name) {
    name=lower(name);
    auto slash=name.find_last_of("\\/");if(slash!=std::string::npos)name=name.substr(slash+1);
    if(name.size()>4&&name.compare(name.size()-4,4,".dll")==0)name.resize(name.size()-4);
    // The api-ms-win-core API sets are served by KERNEL32, as on Windows.
    if(name.rfind("api-ms-win-core-",0)==0)name="kernel32";
    // The Universal CRT's API sets (FFmpeg's DLLs) are served by msvcrt's functions.
    if(name.rfind("api-ms-win-crt-",0)==0)name="msvcrt";
    // Library names that are not C identifiers (vulkan-1) are listed with '_'.
    for(auto& c:name)if(c=='-')c='_';
    return name;
}
Library* findLibrary(const std::string& name) {
    auto wanted=baseName(name);
    // KernelBase holds kernel32's functions on Windows 10; the engine asks it for VirtualAlloc2.
    if(wanted=="kernelbase")wanted="kernel32";
    for(auto& l:libraries())if(lower(l.name)==wanted)return &l;
    return nullptr;
}
}
using azimuth::findModule;using azimuth::loadModule;using azimuth::moduleContaining;
using azimuth::moduleExport;using azimuth::moduleWindowsPath;
// Libraries are the host's own (a Library handle) or DLLs the host loaded
// from files (their base address, as on Windows).
// A DLL loaded from a file comes first: the host d3d12 library serves only
// the engine's bound imports, vkd3d-proton's d3d12.dll everything else.
static void* moduleHandle(const std::string& name) {
    if(auto base=findModule(name))return reinterpret_cast<void*>(base);
    if(auto library=findLibrary(name))return library;
    return nullptr;
}
extern "C" void* win_GetModuleHandleW(const uint16_t* name) {
    if(!name)return reinterpret_cast<void*>(process().imageBase);
    auto text=utf8(name,length(name));
    if(auto handle=moduleHandle(text))return handle;
    log("GetModuleHandleW: "+text+" nao carregado");
    setLastError(ErrorModNotFound);return nullptr;
}
extern "C" void* win_GetModuleHandleA(const char* name) {
    if(!name)return reinterpret_cast<void*>(process().imageBase);
    if(auto handle=moduleHandle(name))return handle;
    log(std::string("GetModuleHandleA: ")+name+" nao carregado");
    setLastError(ErrorModNotFound);return nullptr;
}
extern "C" uint32_t win_GetModuleFileNameW(void* module,uint16_t* path,uint32_t size) {
    std::u16string full=process().modulePath;
    if(module&&reinterpret_cast<uint64_t>(module)!=process().imageBase) {
        auto dll=moduleWindowsPath(reinterpret_cast<uint64_t>(module));
        // A library the host implements is where Windows keeps its own.
        for(auto& l:libraries())if(&l==module){dll="C:\\Windows\\System32\\"+l.name+".dll";break;}
        if(dll.empty()){setLastError(ErrorModNotFound);return 0;}
        full=utf16(dll);
    }
    if(!size){setLastError(ErrorInsufficientBuffer);return 0;}
    size_t copied=full.size()<size?full.size():size-1;
    std::memcpy(path,full.data(),copied*2);path[copied]=0;
    if(copied<full.size()){setLastError(ErrorInsufficientBuffer);return size;}
    setLastError(ErrorSuccess);return uint32_t(copied);
}
extern "C" uint32_t win_GetModuleFileNameA(void* module,char* path,uint32_t size) {
    std::u16string wide(size_t(size?size:1),char16_t(0));
    uint32_t got=win_GetModuleFileNameW(module,reinterpret_cast<uint16_t*>(&wide[0]),size);
    if(!got&&size)return 0;
    // The paths are ASCII (E:\PC\FH2\..., C:\windows\system32\...).
    for(uint32_t n=0;n<got&&n<size;++n)path[n]=char(wide[n]);
    if(got<size)path[got]=0;
    return got;
}
extern "C" void* win_GetProcAddress(void* module,const char* name) {
    if(moduleContaining(reinterpret_cast<uint64_t>(module))) {
        if(auto address=moduleExport(reinterpret_cast<uint64_t>(module),name))return address;
        log("GetProcAddress: "+(reinterpret_cast<uintptr_t>(name)<0x10000?"#"+std::to_string(reinterpret_cast<uintptr_t>(name)):std::string(name))+
            " ausente em "+moduleWindowsPath(reinterpret_cast<uint64_t>(module)));
        setLastError(ErrorProcNotFound);return nullptr;
    }
    auto library=static_cast<Library*>(module);bool ours=false;
    for(auto& l:libraries())if(&l==library)ours=true;
    if(!ours||reinterpret_cast<uintptr_t>(name)<0x10000) {
        log("GetProcAddress em modulo ou ordinal nao suportado");
        setLastError(ErrorProcNotFound);return nullptr;
    }
    for(auto e=guestExports;e->name;++e)if(e->library==library->name&&std::strcmp(e->name,name)==0)return const_cast<void*>(e->address);
    // vulkan-1.dll exports every global command too, as the Windows loader does.
    if(library->name=="vulkan_1")if(void* command=win_vkGetInstanceProcAddr(nullptr,name))return command;
    log("GetProcAddress: "+library->name+"!"+name+" ainda sem implementacao");
    setLastError(ErrorProcNotFound);return nullptr;
}
// The engine stays mapped for the whole run and the host's libraries are not
// files, so reference counts have nothing to count.
extern "C" int32_t win_GetModuleHandleExW(uint32_t flags,const uint16_t* name,void** module) {
    constexpr uint32_t FromAddress=0x4;
    if(!module){setLastError(ErrorInvalidParameter);return 0;}
    *module=nullptr;
    if(flags&FromAddress) {
        auto address=reinterpret_cast<uint64_t>(name);
        if(address>=process().imageBase&&address-process().imageBase<process().imageSize){*module=reinterpret_cast<void*>(process().imageBase);return 1;}
        if(auto base=moduleContaining(address)){*module=reinterpret_cast<void*>(base);return 1;}
        // A library the host implements: its handle stands for its base address
        // (title 0.95: the engine loads windowscodecs.dll for the game and hands
        // it the module "containing" the handle it got).
        for(auto& l:libraries())if(reinterpret_cast<uint64_t>(&l)==address){*module=&l;return 1;}
        setLastError(ErrorModNotFound);return 0;
    }
    *module=win_GetModuleHandleW(name);
    return *module!=nullptr;
}
extern "C" int32_t win_FreeLibrary(void* module) {
    if(!module){setLastError(ErrorInvalidHandle);return 0;}
    return 1;
}
// Only the DLLs Modules.cpp knows are loaded from files, from its own folders,
// so the search policy only needs to be accepted.
extern "C" int32_t win_SetDefaultDllDirectories(uint32_t flags) {
    if(!flags){setLastError(ErrorInvalidParameter);return 0;}
    log("Politica de busca de DLL do motor: "+std::to_string(flags));
    return 1;
}
static void* loadLibrary(const std::string& text,const char* function) {
    if(auto base=loadModule(text))return reinterpret_cast<void*>(base);
    if(auto library=findLibrary(text))return library;
    log(std::string(function)+": "+text+" nao disponivel no host");
    setLastError(ErrorModNotFound);return nullptr;
}
extern "C" void* win_LoadLibraryExW(const uint16_t* name,void*,uint32_t){return loadLibrary(name?utf8(name,length(name)):std::string("(nulo)"),"LoadLibraryExW");}
extern "C" void* win_LoadLibraryW(const uint16_t* name){return loadLibrary(name?utf8(name,length(name)):std::string("(nulo)"),"LoadLibraryW");}
extern "C" void* win_LoadLibraryA(const char* name){return loadLibrary(name?name:"(nulo)","LoadLibraryA");}
extern "C" void* win_LoadLibraryExA(const char* name,void*,uint32_t){return loadLibrary(name?name:"(nulo)","LoadLibraryExA");}
extern "C" int32_t win_GetModuleHandleExA(uint32_t flags,const char* name,void** module) {
    constexpr uint32_t FromAddress=0x4;
    if(flags&FromAddress)return win_GetModuleHandleExW(flags,reinterpret_cast<const uint16_t*>(name),module);
    if(!module){setLastError(ErrorInvalidParameter);return 0;}
    *module=win_GetModuleHandleA(name);
    return *module!=nullptr;
}

// ---- For the DLLs the host loads (vkd3d-proton) -------------------------
extern "C" void win_InitializeCriticalSection(void* section){win_InitializeCriticalSectionEx(section,0,0);}
extern "C" int32_t win_TryEnterCriticalSection(void* section) {
    auto p=static_cast<uint8_t*>(section);
    (void)p;
    uint64_t me=win_GetCurrentThreadId();
    if(__atomic_load_n(ownerOf(section),__ATOMIC_SEQ_CST)==me){++*recursionOf(section);return 1;}
    if(!claim(section,me))return 0;
    entered(section);
    return 1;
}
extern "C" void win_InitializeSRWLock(void* lock){*static_cast<void**>(lock)=nullptr;}
extern "C" uint32_t win_GetEnvironmentVariableA(const char* name,char* buffer,uint32_t size) {
    constexpr uint32_t ErrorEnvvarNotFound=203;
    std::unique_lock<std::mutex> lock;
    for(auto& v:dllEnvironment(lock)) {
        if(v.first!=name)continue;
        if(v.second.size()>=size)return uint32_t(v.second.size()+1);
        std::memcpy(buffer,v.second.c_str(),v.second.size()+1);
        return uint32_t(v.second.size());
    }
    setLastError(ErrorEnvvarNotFound);return 0;
}
extern "C" int32_t win_SetEnvironmentVariableA(const char* name,const char* value) {
    std::unique_lock<std::mutex> lock;
    auto& values=dllEnvironment(lock);
    for(auto n=values.begin();n!=values.end();++n)if(n->first==name){values.erase(n);break;}
    if(value)values.emplace_back(name,value);
    return 1;
}
extern "C" uint32_t win_GetSystemDirectoryA(char* buffer,uint32_t size) {
    static const char directory[]="C:\\windows\\system32";
    if(size<sizeof directory)return sizeof directory;
    std::memcpy(buffer,directory,sizeof directory);
    return sizeof directory-1;
}
extern "C" int32_t win_IsDBCSLeadByteEx(uint32_t,uint8_t){return 0;}   // single-byte code pages only
extern "C" uint32_t win_FormatMessageA(uint32_t flags,const void*,uint32_t id,uint32_t,char* buffer,uint32_t size,void*) {
    constexpr uint32_t AllocateBuffer=0x100;
    if(flags&AllocateBuffer){setLastError(ErrorNotSupported);return 0;}
    auto text="Erro "+std::to_string(id)+"\r\n";
    if(!buffer||text.size()>=size){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(buffer,text.c_str(),text.size()+1);
    return uint32_t(text.size());
}
extern "C" int32_t win_ProcessIdToSessionId(uint32_t,uint32_t* session){if(session)*session=1;return 1;}
// The host's loader sends DLL_THREAD_ATTACH/DETACH to every module anyway: accepted.
extern "C" int32_t win_DisableThreadLibraryCalls(void*){return 1;}
// For the other files (Win32.h).
std::string azimuth::win32::codeCallers(){return stackCallers();}
// No console: FFmpeg's log asks for one to colour its lines.
extern "C" int32_t win_GetConsoleScreenBufferInfo(void*,void*){setLastError(ErrorInvalidHandle);return 0;}
extern "C" int32_t win_SetConsoleTextAttribute(void*,uint16_t){return 1;}
