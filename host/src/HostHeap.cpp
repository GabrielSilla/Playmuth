#include "HostHeap.h"
#include "GuestThreads.h"
#include "win32/Win32.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <algorithm>
#include <new>
#include <sys/mman.h>

#if !defined(__linux__)
extern "C" {
size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t searchStart,int64_t searchEnd,size_t length,size_t alignment,int type,int64_t* physical);
int sceKernelMapDirectMemory(void** address,size_t length,int protection,int flags,int64_t physical,size_t alignment);
int sceKernelMunmap(void* address,size_t length);
int sceKernelReleaseDirectMemory(int64_t physical,size_t length);
int sceKernelAvailableDirectMemorySize(int64_t searchStart,int64_t searchEnd,size_t alignment,int64_t* physical,size_t* size);
int sceKernelDirectMemoryQuery(int64_t offset,int flags,void* info,size_t infoSize);
}
#endif

namespace azimuth {
namespace {
constexpr size_t HeaderSize=32,PageSize=16384;
constexpr uint64_t MagicSystem=0xA21A07C0FFEE0001ull,MagicMapped=0xA21A07C0FFEE0002ull,MagicDirect=0xA21A07C0FFEE0003ull,
                   MagicArena=0xA21A07C0FFEE0004ull;
struct Header {uint64_t magic;void* base;size_t total,usable;};
static_assert(sizeof(Header)==HeaderSize,"Header");
std::atomic<size_t> fallbacks{0};
Header* headerOf(const void* block){return reinterpret_cast<Header*>(const_cast<uint8_t*>(static_cast<const uint8_t*>(block))-HeaderSize);}
#if !defined(__linux__)
// Where the host's memory is asked to go on the console: far above where the
// kernel puts mappings by default. The engine wrote a 50 MB table of 10-byte
// records through the arenas at 0x208000000-0x20c000000 (title 0.78); away
// from there, such a write cannot reach the host's own memory.
constexpr uintptr_t ArenaHint=0x6000000000,LargeHint=0x6800000000;
#endif
// Large blocks end where an inaccessible guard of GuardSize begins: a write
// past the end faults at once, in the writer, instead of landing in whatever
// the kernel mapped next (title 0.77: a sweep of 10-byte records ran through
// the arenas, which on the console sit right next to large blocks).
constexpr size_t GuardSize=64u<<10;
void* placeAtEnd(void* base,size_t total,size_t size,size_t alignment,uint64_t magic) {
    auto end=reinterpret_cast<uintptr_t>(base)+total-GuardSize;
    auto at=(end-size)&~(uintptr_t(alignment)-1);
    auto header=reinterpret_cast<Header*>(at-HeaderSize);
    header->magic=magic;header->base=base;header->total=total;header->usable=size;
    if(mprotect(reinterpret_cast<void*>(end),GuardSize,PROT_NONE)!=0) {
        static std::atomic<bool> said{false};
        if(!said.exchange(true)){char text[96];std::snprintf(text,sizeof text,"Alocador do host: guarda depois de bloco grande recusada (errno %d)",errno);win32::log(text);}
    }
    return reinterpret_cast<void*>(at);
}
void* place(void* base,size_t total,size_t size,size_t alignment,uint64_t magic) {
    auto at=(reinterpret_cast<uintptr_t>(base)+HeaderSize+alignment-1)&~(uintptr_t(alignment)-1);
    auto header=reinterpret_cast<Header*>(at-HeaderSize);
    header->magic=magic;header->base=base;header->total=total;header->usable=size;
    return reinterpret_cast<void*>(at);
}
std::atomic<size_t> mappedBytes{0};
// The first 300 large blocks, with the engine thread that asked (0: a host thread).
std::atomic<unsigned> reported{0};
void report(size_t total,const char* where) {
    if(reported.fetch_add(1)>=300&&total<(64u<<20))return;
    char text[160];std::snprintf(text,sizeof text,"Alocador do host: bloco de %zu KiB em %s pela thread %u (total grande %zu MiB)",
                                 total>>10,where,currentGuestThreadId(),mappedBytes.load()>>20);
    win32::log(text);
}
#if !defined(__linux__)
// The console title has 448 MiB of flexible memory (what mmap and malloc draw
// from) and the system ends it, with no signal, when that runs out (title
// 0.45: 215 MiB of large blocks within a second of the launcher, then the
// end). Large blocks come from direct memory, the console's big pool, mapped
// CPU read-write and write-back cached; the physical address is kept in the
// first 16 bytes of the mapping, in front of the header.
constexpr size_t DirectGranularity=64u<<10;
constexpr int DirectWriteBack=0,CpuReadWrite=0x03;
void* direct(size_t size,size_t alignment) {
    size_t total=(size+alignment+HeaderSize+16+DirectGranularity-1)/DirectGranularity*DirectGranularity+GuardSize;
    if(total<size)return nullptr;
    static const size_t pool=[]{
        size_t size=sceKernelGetDirectMemorySize();
        char text[96];std::snprintf(text,sizeof text,"Memoria direta do titulo: %zu MiB",size>>20);win32::log(text);
        return size;
    }();
    int64_t physical=0;
    if(int error=sceKernelAllocateDirectMemory(0,int64_t(pool),total,DirectGranularity,DirectWriteBack,&physical);error<0) {
        static std::atomic<unsigned> failures{0};
        if(failures++<10){char text[128];std::snprintf(text,sizeof text,"Alocador do host: memoria direta recusou %zu KiB (0x%x)",total>>10,unsigned(error));win32::log(text);}
        return nullptr;
    }
    void* base=reinterpret_cast<void*>(LargeHint);
    if(int error=sceKernelMapDirectMemory(&base,total,CpuReadWrite,0,physical,DirectGranularity);error<0) {
        char text[128];std::snprintf(text,sizeof text,"Alocador do host: mapear %zu KiB de memoria direta falhou (0x%x)",total>>10,unsigned(error));win32::log(text);
        sceKernelReleaseDirectMemory(physical,total);return nullptr;
    }
    std::memcpy(base,&physical,sizeof physical);
    auto block=placeAtEnd(base,total,size,alignment,MagicDirect);
    std::memset(block,0,size);   // as mmap's pages are: callers may rely on it
    mappedBytes.fetch_add(total);
    report(total,"memoria direta");
    return block;
}
#endif
void* mapped(size_t size,size_t alignment) {
#if !defined(__linux__)
    if(void* block=direct(size,alignment))return block;
#endif
    size_t total=(size+alignment+HeaderSize+PageSize-1)/PageSize*PageSize+GuardSize;
    if(total<size)return nullptr;
    void* base=mmap(nullptr,total,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(base==MAP_FAILED)return nullptr;
    mappedBytes.fetch_add(total);
    report(total,"mmap");
    return placeAtEnd(base,total,size,alignment,MagicMapped);   // the kernel zero-fills it
}

// Blocks under MappedFrom come from the host's own arenas, never from the
// title's malloc: that heap is small and fixed, the system's own code needs it
// (title 0.47: once the engine's small blocks had exhausted it, malloc refused
// 64 KiB and pthread_create no longer returned), and it is what the launcher
// froze on since title 0.35. Arenas are 64 MiB chunks of direct memory (mmap
// on Linux); a block's size is rounded up to one of 68 classes (16-byte steps
// to 256, then four per doubling) and a freed block goes to its class's list.
constexpr size_t ArenaChunk=64u<<20;
constexpr unsigned Classes=68;
// Shards: each thread uses one of 16 arenas, each with its own lock (title
// 0.49: the 15 threads compiling the launcher's pipelines spent their time in
// malloc and free, all waiting on one lock, and the launcher with them).
constexpr unsigned Shards=16;
unsigned classOf(size_t total) {
    if(total<=256)return unsigned((total+15)/16)-1;
    unsigned bits=63-unsigned(__builtin_clzll(total-1));
    return 16+(bits-8)*4+unsigned(((total-1)>>(bits-2))&3);
}
size_t classSize(unsigned index) {
    if(index<16)return size_t(index+1)*16;
    unsigned bits=8+(index-16)/4,step=(index-16)%4;
    return (size_t(1)<<bits)+(step+1)*((size_t(1)<<bits)/4);
}
// Freed blocks wait in a quarantine of QuarantineSize before they can be
// given out again, as freed memory does on Windows' heap in practice: a late
// write by the engine then lands in a block nobody uses yet.
constexpr unsigned QuarantineSize=4096;
struct Arena {
    std::mutex lock;
    void* lists[Classes]{};
    uint8_t* next=nullptr;uint8_t* end=nullptr;
    void* waiting[QuarantineSize]{};uint8_t waitingClass[QuarantineSize]{};unsigned waitingNext=0;
};
Arena arenas[Shards];
std::atomic<unsigned> chunks{0};
unsigned shardOfThread() {
    auto self=reinterpret_cast<uintptr_t>(pthread_self());
    return unsigned(((self>>4)*0x9E3779B97F4A7C15ull)>>60)%Shards;
}
void* newChunk() {
#if !defined(__linux__)
    static const size_t pool=sceKernelGetDirectMemorySize();
    int64_t physical=0;void* base=reinterpret_cast<void*>(ArenaHint);
    if(sceKernelAllocateDirectMemory(0,int64_t(pool),ArenaChunk,ArenaChunk,DirectWriteBack,&physical)>=0) {
        if(sceKernelMapDirectMemory(&base,ArenaChunk,CpuReadWrite,0,physical,ArenaChunk)>=0)return base;
        sceKernelReleaseDirectMemory(physical,ArenaChunk);
    }
#endif
    void* base2=mmap(nullptr,ArenaChunk,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    return base2==MAP_FAILED?nullptr:base2;
}
// A raw block of class `index` from the calling thread's shard.
// A freed block: MagicFree where a header's magic is, the next free block,
// and a check word tied to both addresses. A block written after it was
// freed fails the check (title 0.72: a corrupted list made the next
// allocation read 0x33c3095a6f80 and fault); its list is then dropped and the
// log names the block. A block freed twice still carries MagicFree.
constexpr uint64_t MagicFree=0xA21A07C0FFEE00F0ull,CheckKey=0x5bd1e9955bd1e995ull;
uint64_t checkOf(const void* raw,const void* next){return reinterpret_cast<uint64_t>(raw)^reinterpret_cast<uint64_t>(next)^CheckKey;}
std::atomic<unsigned> corruptions{0};
void* fromArena(size_t total,unsigned& index,unsigned& shard) {
    index=classOf(total);
    if(index>=Classes)return nullptr;
    size_t size=classSize(index);
    shard=shardOfThread();
    auto& a=arenas[shard];
    unsigned grown=0;void* raw=nullptr;void* broken=nullptr;int guardRefused=0;
    {
        std::lock_guard<std::mutex> guard(a.lock);
        if(void* head=a.lists[index]) {
            uint64_t words[3];std::memcpy(words,head,sizeof words);
            auto next=reinterpret_cast<void*>(words[1]);
            if(words[0]==MagicFree&&words[2]==checkOf(head,next)){a.lists[index]=next;raw=head;}
            else{broken=head;a.lists[index]=nullptr;}
        }
        if(!raw) {
            if(!a.next||size_t(a.end-a.next)<size) {
                auto chunk=static_cast<uint8_t*>(newChunk());
                if(!chunk)return nullptr;
                // Each arena ends in an inaccessible guard: a write running
                // through it (title 0.79: the engine's 10-byte records followed
                // the arenas wherever they were) faults in the writer.
                if(mprotect(chunk+ArenaChunk-GuardSize,GuardSize,PROT_NONE)!=0)guardRefused=errno;
                a.next=chunk;a.end=chunk+ArenaChunk-GuardSize;grown=chunks.fetch_add(1)+1;
                mappedBytes.fetch_add(ArenaChunk);
            }
            raw=a.next;a.next+=size;
        }
    }
    if(grown){char text[160];std::snprintf(text,sizeof text,"Alocador do host: arena %u de 64 MiB em %p%s",grown,static_cast<void*>(a.next-size),
                                         guardRefused?" (guarda recusada pelo kernel)":"");win32::log(text);}
    if(broken&&corruptions.fetch_add(1)<20) {
        // What was written there: its first 48 bytes, in hex and as text.
        uint8_t bytes[48];std::memcpy(bytes,broken,sizeof bytes);
        char hexText[3*48+1],ascii[49];
        for(unsigned n=0;n<48;++n){std::snprintf(hexText+3*n,4,"%02x ",bytes[n]);ascii[n]=bytes[n]>=32&&bytes[n]<127?char(bytes[n]):'.';}
        ascii[48]=0;
        char text[400];std::snprintf(text,sizeof text,"Alocador do host: bloco livre %p (classe de %zu bytes) foi escrito depois do free; lista descartada (thread %u): %s|%s|",
                                     broken,size,currentGuestThreadId(),hexText,ascii);
        win32::log(text);
    }
    return raw;
}
// False for a block already free (freed twice): it stays out of the list.
// The block enters the quarantine; the oldest one there goes to its list.
bool toArena(void* raw,unsigned index,unsigned shard) {
    auto& a=arenas[shard%Shards];
    std::lock_guard<std::mutex> guard(a.lock);
    uint64_t words[3];std::memcpy(words,raw,sizeof words);
    if(words[0]==MagicFree&&words[2]==checkOf(raw,reinterpret_cast<void*>(words[1])))return false;
    uint64_t marked[3]={MagicFree,0,checkOf(raw,nullptr)};
    std::memcpy(raw,marked,sizeof marked);
    auto slot=a.waitingNext;a.waitingNext=(slot+1)%QuarantineSize;
    void* oldest=a.waiting[slot];unsigned oldestClass=a.waitingClass[slot];
    a.waiting[slot]=raw;a.waitingClass[slot]=uint8_t(index);
    if(oldest) {
        void* next=a.lists[oldestClass];
        uint64_t fresh[3]={MagicFree,reinterpret_cast<uint64_t>(next),checkOf(oldest,next)};
        std::memcpy(oldest,fresh,sizeof fresh);a.lists[oldestClass]=oldest;
    }
    return true;
}
}
void* heapAlloc(size_t size,size_t alignment,bool zero) noexcept {
    if(alignment<16)alignment=16;
    if(alignment>4096||(alignment&(alignment-1))||size>(size_t(1)<<44))return nullptr;
    if(size>=MappedFrom)return mapped(size,alignment);
    size_t total=size+alignment+HeaderSize;
    unsigned index=0,shard=0;
    if(void* raw=fromArena(total,index,shard)) {
        void* block=place(raw,classSize(index),size,alignment,MagicArena);
        headerOf(block)->total=index|(size_t(shard)<<8);   // class and shard, for heapFree
        if(zero)std::memset(block,0,size);
        return block;
    }
    fallbacks.fetch_add(1);
    return mapped(size,alignment);
}
void heapFree(void* block) noexcept {
    if(!block)return;
    auto header=headerOf(block);
    if(header->magic==MagicMapped){mappedBytes.fetch_sub(header->total);munmap(header->base,header->total);}
#if !defined(__linux__)
    else if(header->magic==MagicDirect) {
        int64_t physical;std::memcpy(&physical,header->base,sizeof physical);
        size_t total=header->total;void* base=header->base;
        mappedBytes.fetch_sub(total);
        // The physical memory goes back only once unmapped: released while still
        // mapped, it could come back as another block that this old address
        // would then alias.
        if(int error=sceKernelMunmap(base,total);error<0) {
            if(corruptions.fetch_add(1)<20){char text[128];std::snprintf(text,sizeof text,"Alocador do host: sceKernelMunmap de %p falhou (0x%x); memoria mantida",base,unsigned(error));win32::log(text);}
        }else if(int released=sceKernelReleaseDirectMemory(physical,total);released<0) {
            if(corruptions.fetch_add(1)<20){char text[128];std::snprintf(text,sizeof text,"Alocador do host: sceKernelReleaseDirectMemory falhou (0x%x)",unsigned(released));win32::log(text);}
        }
    }
#endif
    else if(header->magic==MagicArena) {
        if(!toArena(header->base,unsigned(header->total&0xff),unsigned(header->total>>8))&&corruptions.fetch_add(1)<20) {
            char text[128];std::snprintf(text,sizeof text,"Alocador do host: bloco %p liberado duas vezes (thread %u)",block,currentGuestThreadId());
            win32::log(text);
        }
    }
    else if(header->magic==MagicFree) {
        if(corruptions.fetch_add(1)<20){char text[128];std::snprintf(text,sizeof text,"Alocador do host: bloco %p liberado duas vezes (thread %u)",block,currentGuestThreadId());win32::log(text);}
    }
    else if(header->magic==MagicSystem)std::free(header->base);
    // Not a block this allocator gave out (no magic in front of it). Nothing of
    // the host uses the title's malloc any more, so this is a pointer freed
    // with the wrong function (a HeapAlloc block given to msvcrt's free, or
    // the other way round): handing it to the title's free wrote that heap's
    // bookkeeping into the arenas. It is logged and left alone.
    else if(corruptions.fetch_add(1)<20) {
        char text[160];std::snprintf(text,sizeof text,"Alocador do host: free de %p, que nao e inicio de bloco (marca %llx), ignorado (thread %u)",
                                     block,static_cast<unsigned long long>(header->magic),currentGuestThreadId());
        win32::log(text);
    }
}
size_t heapUsable(const void* block) noexcept {
    if(!block)return 0;
    auto header=headerOf(block);
    return header->magic==MagicMapped||header->magic==MagicSystem||header->magic==MagicDirect||header->magic==MagicArena?header->usable:0;
}
size_t heapMappedBytes() noexcept{return mappedBytes.load();}
size_t directMemoryFree() noexcept {
#if defined(__linux__)
    return 0;
#else
    int64_t physical=0;size_t size=0;
    if(sceKernelAvailableDirectMemorySize(0,int64_t(sceKernelGetDirectMemorySize()),64u<<10,&physical,&size)<0)return 0;
    return size;
#endif
}
size_t directMemoryUsed() noexcept {
#if defined(__linux__)
    return 0;
#else
    // Walks the allocated ranges: SCE_KERNEL_DMQ_FIND_NEXT (1) gives the
    // range at or after the offset.
    struct {int64_t start,end;int32_t type;int32_t pad;} info{};
    size_t used=0;int64_t at=0;const int64_t pool=int64_t(sceKernelGetDirectMemorySize());
    // Bounded: it runs on the watchdog thread only, and must stay cheap.
    for(unsigned n=0;n<8192&&at<pool;++n) {
        if(sceKernelDirectMemoryQuery(at,1,&info,sizeof info)!=0||info.end<=info.start||info.end<=at)break;
        used+=size_t(info.end-std::max(info.start,at));at=info.end;
    }
    return used;
#endif
}
size_t heapFallbacks() noexcept {return fallbacks.load();}
}

// Every C++ allocation of the host goes through heapAlloc: the replaceable
// forms of <new>, all of them, so none of libc++'s own is linked.
namespace {
void* get(size_t size,size_t alignment) {
    void* block=azimuth::heapAlloc(size?size:1,alignment,false);
    if(!block)throw std::bad_alloc();
    return block;
}
void* getNothrow(size_t size,size_t alignment) noexcept {return azimuth::heapAlloc(size?size:1,alignment,false);}
}
void* operator new(size_t size){return get(size,16);}
void* operator new[](size_t size){return get(size,16);}
void* operator new(size_t size,const std::nothrow_t&) noexcept{return getNothrow(size,16);}
void* operator new[](size_t size,const std::nothrow_t&) noexcept{return getNothrow(size,16);}
void* operator new(size_t size,std::align_val_t alignment){return get(size,size_t(alignment));}
void* operator new[](size_t size,std::align_val_t alignment){return get(size,size_t(alignment));}
void* operator new(size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept{return getNothrow(size,size_t(alignment));}
void* operator new[](size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept{return getNothrow(size,size_t(alignment));}
void operator delete(void* block) noexcept{azimuth::heapFree(block);}
void operator delete[](void* block) noexcept{azimuth::heapFree(block);}
void operator delete(void* block,size_t) noexcept{azimuth::heapFree(block);}
void operator delete[](void* block,size_t) noexcept{azimuth::heapFree(block);}
void operator delete(void* block,const std::nothrow_t&) noexcept{azimuth::heapFree(block);}
void operator delete[](void* block,const std::nothrow_t&) noexcept{azimuth::heapFree(block);}
void operator delete(void* block,std::align_val_t) noexcept{azimuth::heapFree(block);}
void operator delete[](void* block,std::align_val_t) noexcept{azimuth::heapFree(block);}
void operator delete(void* block,size_t,std::align_val_t) noexcept{azimuth::heapFree(block);}
void operator delete[](void* block,size_t,std::align_val_t) noexcept{azimuth::heapFree(block);}
void operator delete(void* block,std::align_val_t,const std::nothrow_t&) noexcept{azimuth::heapFree(block);}
void operator delete[](void* block,std::align_val_t,const std::nothrow_t&) noexcept{azimuth::heapFree(block);}
