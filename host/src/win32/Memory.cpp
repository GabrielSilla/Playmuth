// VirtualAlloc and its family over mmap: reservations are PROT_NONE
// mappings aligned to Windows' 64 KiB allocation granularity, committing a
// page gives it its protection, decommitting maps fresh PROT_NONE pages over
// it (committed pages start zeroed, as on Windows). The table of regions
// answers VirtualQuery and VirtualProtect for this memory; other addresses
// (the engine image, stacks, the host heap) are a diagnostic stop until the
// engine needs them.
//
// Windows 10's placeholders (title 0.65): the engine reserves its graphics
// heap as a placeholder, splits it, fills parts with private memory
// (VirtualAlloc2 MEM_REPLACE_PLACEHOLDER) and maps a shared section, its
// ESRAM, into others (MapViewOfFile3). A placeholder is a region with no
// pages; a section is memory that can be mapped at several addresses: direct
// memory on the console, a memfd on Linux.
//
// Large reservations on the console (the engine's graphics heap is 20 GiB,
// title 0.65) reserve address space only: an anonymous mapping that size does
// not fit the title's 448 MiB of flexible memory, and what the game commits
// must come from direct memory anyway. Committing there maps fresh direct
// memory, 16 KiB blocks at a time; decommitting and releasing give it back.
#include "Win32.h"
#include "WriteWatch.h"
#include "DirectMemoryRelease.h"
namespace azimuth {uintptr_t currentStackTop();uint32_t currentGuestThreadId();}   // GuestThreads.h (its PeImage Section clashes with ours)
#include "../HostHeap.h"
#include "../Modules.h"
#include "../Platform.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#if !defined(__linux__)
extern "C" {
size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t searchStart,int64_t searchEnd,size_t length,size_t alignment,int type,int64_t* physical);
int sceKernelMapDirectMemory(void** address,size_t length,int protection,int flags,int64_t physical,size_t alignment);
int sceKernelReleaseDirectMemory(int64_t physical,size_t length);
int sceKernelAvailableDirectMemorySize(int64_t searchStart,int64_t searchEnd,size_t alignment,int64_t* physical,size_t* size);
int sceKernelReserveVirtualRange(void** address,size_t length,int flags,size_t alignment);
int sceKernelMunmap(void* address,size_t length);
}
#endif

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

// The calls that repeat while the game streams its world log their first 300
// and then one in 1000 (title 1.16): each line used to cost a disk sync and a
// stack scan on the engine thread.
static bool often(std::atomic<unsigned>& count){unsigned n=++count;return n<=300||n%1000==0;}
namespace {
// Slow API events are written without allocating or logging on the caller
// (VirtualProtect may be invoked by a guest VEH). The watchdog drains them.
struct SlowMemoryEvent {const char* api;uint64_t begin,end,address,size;uint32_t thread;};
struct SlowMemorySlot {std::atomic<unsigned> state{0};SlowMemoryEvent event{};};
SlowMemorySlot slowMemory[1024];
std::atomic<uint64_t> slowMemoryNext{0},slowMemoryDropped{0};
struct SlowMemoryCall {
    const char* api;uint64_t address,size;int64_t begin=fastMonotonicNs();
    SlowMemoryCall(const char* name,void* at,uint64_t bytes):api(name),address(reinterpret_cast<uint64_t>(at)),size(bytes){}
    ~SlowMemoryCall(){
        auto end=fastMonotonicNs();if(end<=begin||end-begin<2000000)return;
        auto& slot=slowMemory[slowMemoryNext.fetch_add(1,std::memory_order_relaxed)%1024];
        unsigned expected=0;
        if(!slot.state.compare_exchange_strong(expected,1,std::memory_order_acquire)){
            slowMemoryDropped.fetch_add(1,std::memory_order_relaxed);return;
        }
        slot.event={api,uint64_t(begin),uint64_t(end),address,size,azimuth::currentGuestThreadId()};
        slot.state.store(2,std::memory_order_release);
    }
};
std::atomic<uint64_t> directUnmapCalls{0},directReleaseCalls{0},directReserveCalls{0},directReleasedBytes{0},directReleaseRuns{0};
constexpr uint64_t Page=4096,Granularity=65536;
// Where the host heap maps its arenas and large blocks (HostHeap.cpp's hints).
constexpr uint64_t HostHeapFrom=0x6000000000,HostHeapTo=0x7000000000;
enum : uint32_t {
    MemCoalescePlaceholders=0x1,MemPreservePlaceholder=0x2,MemCommit=0x1000,MemReserve=0x2000,MemDecommit=0x4000,
    MemReplacePlaceholder=0x4000,MemRelease=0x8000,MemFree=0x10000,MemPrivate=0x20000,MemMapped=0x40000,
    MemReservePlaceholder=0x40000,MemTopDown=0x100000,MemLargePages=0x20000000
};
enum : uint32_t {
    PageNoAccess=0x01,PageReadOnly=0x02,PageReadWrite=0x04,PageExecute=0x10,PageExecuteRead=0x20,PageExecuteReadWrite=0x40,
    PageModifiers=0x700   // PAGE_GUARD, PAGE_NOCACHE, PAGE_WRITECOMBINE: caching hints the host does not apply
};
constexpr uint32_t ErrorInvalidAddress=487;
// MEMORY_BASIC_INFORMATION (x64).
struct BasicInformation {uint64_t base,allocationBase;uint32_t allocationProtect;uint16_t partition;uint16_t pad;uint64_t size;uint32_t state,protect,type,pad2;};
static_assert(sizeof(BasicInformation)==48);

std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
[[noreturn]] void unsupported(const std::string& what){stopGuest("KERNEL32 ainda sem adaptacao para este uso: "+what);}
uint64_t down(uint64_t v,uint64_t unit){return v/unit*unit;}
uint64_t up(uint64_t v,uint64_t unit){return (v+unit-1)/unit*unit;}
// The host protection of a Windows one; -1 for one the host does not map.
int hostProtection(uint32_t protect) {
    switch(protect&~PageModifiers) {
    case PageNoAccess:return PROT_NONE;
    case PageReadOnly:return PROT_READ;
    case PageReadWrite:return PROT_READ|PROT_WRITE;
    case PageExecute:case PageExecuteRead:return PROT_READ|PROT_EXEC;
    case PageExecuteReadWrite:return PROT_READ|PROT_WRITE|PROT_EXEC;
    }
    return -1;
}

// A pagefile-backed section (CreateFileMapping with no file): shared memory
// that views map at any address.
struct Section:Object {
    uint64_t size=0;uint32_t protect=0;
    int fd=-1;int64_t physical=-1;
    ~Section() override {
        if(fd>=0)close(fd);
#if !defined(__linux__)
        if(physical>=0)sceKernelReleaseDirectMemory(physical,size);
#endif
    }
};

// One reservation; pages[n] is the protection of a committed page, 0 when
// the page is only reserved. A placeholder has no pages; a view is a section
// mapped there.
struct Region {
    uint64_t size;uint32_t allocationProtect;std::vector<uint32_t> pages;
    bool placeholder=false;std::shared_ptr<Section> view;
    bool writeWatch=false;
    bool addressOnly=false;   // reserved address space, committed with direct memory (console)
};
std::mutex lock;
std::map<uint64_t,Region> regions;
// The region holding [address, address+size), under the lock.
std::map<uint64_t,Region>::iterator holding(uint64_t address,uint64_t size) {
    auto found=regions.upper_bound(address);
    if(found==regions.begin())return regions.end();
    --found;
    if(address+size>found->first+found->second.size)return regions.end();
    return found;
}
// The size a reservation is mapped with: the kernel maps and unmaps whole
// blocks (16 KiB on the console), so trimming the end of a reservation that
// stops inside one unmapped the block holding its last pages (title 0.61: the
// engine copying the game's image faulted there, on an unmapped address).
uint64_t mapped(uint64_t size){auto block=azimuth::platform::protectionGranularity();return (size+block-1)/block*block;}
// A PROT_NONE mapping of `size` bytes at `at`, or (at 0) anywhere aligned to
// `alignment` (at least the allocation granularity).
uint64_t mapReserved(uint64_t at,uint64_t size,uint64_t alignment=Granularity) {
    size=mapped(size);
    if(at) {
        void* p=mmap(reinterpret_cast<void*>(at),size,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
        if(p==MAP_FAILED)return 0;
        if(reinterpret_cast<uint64_t>(p)!=at){munmap(p,size);return 0;}
        return at;
    }
    if(alignment<Granularity)alignment=Granularity;
    void* p=mmap(nullptr,size+alignment,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(p==MAP_FAILED)return 0;
    auto start=reinterpret_cast<uint64_t>(p),base=up(start,alignment);
    if(base>start)munmap(p,base-start);
    if(base+size<start+size+alignment)munmap(reinterpret_cast<void*>(base+size),start+size+alignment-(base+size));
    return base;
}
#if !defined(__linux__)
constexpr uint64_t AddressOnlyFrom=1ull<<30,Block=16384;
constexpr int MapFixed=0x10;
// A title maps at fixed addresses only inside its map area (the SDK's
// SCE_KERNEL_APP_MAP_AREA_START/END_ADDR). Asking the kernel for a fixed
// reservation far outside it (the engine's 0x60000000000, title 0.66) was
// followed by the console switching off: such requests fail here instead.
constexpr uint64_t AppAreaStart=0x1000000000,AppAreaEnd=0xfc00000000;
// Address space only: at `at`, or anywhere in [lowest, highest] aligned to `alignment`.
uint64_t reserveAddresses(uint64_t at,uint64_t size,uint64_t alignment,uint64_t lowest=0,uint64_t highest=0) {
    if(alignment<Granularity)alignment=Granularity;
    if(at&&(at<AppAreaStart||at+mapped(size)>AppAreaEnd)) {
        log("Reserva de "+std::to_string(size>>20)+" MiB em "+hex(at)+" fora da area de mapeamento do titulo ("+hex(AppAreaStart)+"-"+hex(AppAreaEnd)+")");
        return 0;
    }
    // A range asked for is kept to its part inside the title's area; one wholly
    // outside it (the engine's 2-4 TiB, title 1.01) is dropped, as the anonymous
    // mappings before it dropped every range: the engine takes what it gets.
    if(!at&&(lowest||highest)) {
        uint64_t top=highest?highest:~0ull;
        if(top<AppAreaStart||lowest>=AppAreaEnd) {
            static bool said=false;
            if(!said){said=true;log("Reserva com faixa "+hex(lowest)+"-"+hex(highest)+" fora da area do titulo: colocada em qualquer lugar da area");}
            lowest=0;highest=0;
        }else{lowest=std::max(lowest,AppAreaStart);highest=std::min(top,AppAreaEnd-1);}
    }
    // Anywhere: the kernel refused a 4 GiB alignment (title 0.67), so the range
    // is reserved larger with its own block alignment and trimmed to the aligned part.
    uint64_t hint=at?at:up(std::max(lowest,AppAreaStart),alignment);
    uint64_t length=at?mapped(size):mapped(size)+alignment;
    void* address=reinterpret_cast<void*>(hint);
    int result=sceKernelReserveVirtualRange(&address,length,at?MapFixed:0,Block);
    if(result<0){log("sceKernelReserveVirtualRange de "+std::to_string(length>>20)+" MiB falhou ("+hex(uint32_t(result))+")");return 0;}
    auto base=reinterpret_cast<uint64_t>(address);
    if(!at) {
        auto start=base;base=up(start,alignment);
        if(base>start)sceKernelMunmap(reinterpret_cast<void*>(start),base-start);
        if(start+length>base+mapped(size))sceKernelMunmap(reinterpret_cast<void*>(base+mapped(size)),start+length-(base+mapped(size)));
        address=reinterpret_cast<void*>(base);
    }
    if((at&&base!=at)||(!at&&(base<lowest||(highest&&base+size-1>highest)))) {
        log("Reserva de "+std::to_string(size>>20)+" MiB caiu em "+hex(base)+", fora do pedido");
        sceKernelMunmap(address,mapped(size));return 0;
    }
    return base;
}
// The direct memory behind each committed block of address-only regions.
std::map<uint64_t,DirectBlock> backing;
uint64_t nextDirectAllocation=1;
// Blocks moved to flexible memory to be executable (toFlexible).
std::set<uint64_t> flexibleBlocks;
bool backed(uint64_t block){return backing.count(block)||flexibleBlocks.count(block);}
// Maps [b, b+size) onto direct memory at `physical`, zeroed, read-write.
bool mapBacking(uint64_t b,uint64_t size,int64_t physical) {
    sceKernelMunmap(reinterpret_cast<void*>(b),size);
    void* address=reinterpret_cast<void*>(b);
    if(sceKernelMapDirectMemory(&address,size,0x3,MapFixed,physical,Block)<0||address!=reinterpret_cast<void*>(b)) {
        sceKernelReleaseDirectMemory(physical,size);reserveAddresses(b,size,Block);
        log("Commit: mapear memoria direta em "+hex(b)+" falhou");return false;
    }
    std::memset(address,0,size);
    if(size>=(64u<<20))log("Commit com memoria direta: "+std::to_string(size>>20)+" MiB mapeados e zerados em "+hex(b));
    auto allocation=nextDirectAllocation++;
    for(auto at=b;at<b+size;at+=Block)backing[at]={physical+int64_t(at-b),allocation};
    return true;
}
std::atomic<uint64_t> piecedCommits{0},commitPieces{0};
// Gives every block of [from, to) direct memory, zeroed, read-write. A run
// takes one physical allocation when a free run is that large; otherwise it
// is filled from several smaller ones (title 1.40: after ~10 minutes in the
// game at 4K the largest free run fell to 3 MiB, a commit asked for more as
// one piece, VirtualAlloc returned NULL and the game wrote through it). The
// blocks of each piece keep their own allocation, as unback expects.
bool back(uint64_t from,uint64_t to) {
    const int64_t pool=int64_t(sceKernelGetDirectMemorySize());
    for(auto b=down(from,Block);b<up(to,Block);) {
        if(backed(b)){b+=Block;continue;}
        auto end=b;while(end<up(to,Block)&&!backed(end))end+=Block;
        int64_t physical=0;
        static std::atomic<unsigned> commits{0};
        if(end-b>=(1u<<20)&&often(commits))log("Commit com memoria direta: "+std::to_string((end-b)>>10)+" KiB em "+hex(b)+"; maior trecho livre "+
                               std::to_string(azimuth::directMemoryFree()>>20)+" MiB");
        if(sceKernelAllocateDirectMemory(0,pool,end-b,Block,0,&physical)>=0) {
            if(!mapBacking(b,end-b,physical))return false;
            b=end;continue;
        }
        unsigned pieces=0;
        while(b<end) {
            // Another thread may take the run between the query and the
            // allocation: asked again a few times.
            int64_t start=0;size_t largest=0;uint64_t piece=0;bool got=false;
            for(int attempt=0;attempt<4&&!got;++attempt) {
                if(sceKernelAvailableDirectMemorySize(0,pool,Block,&start,&largest)<0)largest=0;
                piece=std::min<uint64_t>(end-b,largest/Block*Block);
                if(!piece)break;
                got=sceKernelAllocateDirectMemory(0,pool,piece,Block,0,&physical)>=0;
            }
            if(!got) {
                log("Commit: memoria direta esgotada ("+std::to_string((end-b)>>10)+" KiB pedidos em "+hex(b)+", maior trecho livre "+
                    std::to_string(largest>>10)+" KiB, em uso "+std::to_string(azimuth::directMemoryUsed()>>20)+" MiB)");
                return false;
            }
            if(!mapBacking(b,piece,physical))return false;
            b+=piece;++pieces;
        }
        commitPieces.fetch_add(pieces,std::memory_order_relaxed);
        if(piecedCommits.fetch_add(1,std::memory_order_relaxed)%64==0)
            log("Commit em "+std::to_string(pieces)+" pedacos de memoria direta (sem trecho contiguo do tamanho pedido); "+
                std::to_string(piecedCommits.load())+" commits assim ate agora");
    }
    return true;
}
// Gives back the direct memory of the blocks wholly inside [from, to) and
// reserves their addresses again.
struct DirectReleaseOps {
    bool unmap(uint64_t base,uint64_t size,bool flexible){
        directUnmapCalls.fetch_add(1,std::memory_order_relaxed);
        return flexible?munmap(reinterpret_cast<void*>(base),size)==0:sceKernelMunmap(reinterpret_cast<void*>(base),size)==0;
    }
    bool release(int64_t physical,uint64_t size){
        directReleaseCalls.fetch_add(1,std::memory_order_relaxed);
        if(sceKernelReleaseDirectMemory(physical,size)!=0)return false;
        directReleasedBytes.fetch_add(size,std::memory_order_relaxed);
        if(size>Block)directReleaseRuns.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    bool reserve(uint64_t base,uint64_t size){
        directReserveCalls.fetch_add(1,std::memory_order_relaxed);
        return reserveAddresses(base,size,Block)==base;
    }
};
void unback(uint64_t from,uint64_t to,bool preserve=true) {
    DirectReleaseOps ops;
    if(!releaseDirectBacking(backing,flexibleBlocks,from,to,Block,preserve,ops))
        unsupported("liberar memoria direta falhou; execucao interrompida para nao continuar com mapeamentos parciais");
}
// Code in direct memory: a block the kernel will not make executable moves,
// with its bytes, to flexible memory at the same address (title 1.01: the
// engine maps the game's image with VirtualAlloc and then makes its code
// executable). Only code pages take flexible memory this way.
bool toFlexible(uint64_t b) {
    auto found=backing.find(b);if(found==backing.end())return false;
    std::vector<uint8_t> bytes(Block);
    mprotect(reinterpret_cast<void*>(b),Block,PROT_READ|PROT_WRITE);
    std::memcpy(bytes.data(),reinterpret_cast<const void*>(b),Block);
    sceKernelMunmap(reinterpret_cast<void*>(b),Block);sceKernelReleaseDirectMemory(found->second.physical,Block);backing.erase(found);
    void* p=mmap(reinterpret_cast<void*>(b),Block,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE|MAP_FIXED,-1,0);
    if(p!=reinterpret_cast<void*>(b)) {
        log("Bloco executavel em "+hex(b)+": memoria flexivel no mesmo endereco recusada");
        if(p!=MAP_FAILED)munmap(p,Block);
        reserveAddresses(b,Block,Block);return false;
    }
    std::memcpy(p,bytes.data(),Block);
    flexibleBlocks.insert(b);
    static unsigned moved=0;
    if(++moved==1||moved%256==0)log("Blocos executaveis passados para memoria flexivel: "+std::to_string(moved)+" (o ultimo em "+hex(b)+")");
    return true;
}
#endif
// Fresh PROT_NONE anonymous pages over [from, to), whatever was mapped there
// (in an address-only region: the memory given back, the addresses reserved again).
bool clear(uint64_t from,uint64_t to,bool addressOnly=false) {
#if !defined(__linux__)
    if(addressOnly){unback(from,to);return true;}
#endif
    (void)addressOnly;
    void* p=mmap(reinterpret_cast<void*>(from),to-from,PROT_NONE,MAP_ANON|MAP_PRIVATE|MAP_FIXED,-1,0);
    return p!=MAP_FAILED;
}
namespace {
std::atomic<uint64_t> protectionBlocks{0},protectionCalls{0};
}
// One syscall for a contiguous run; preserve the old per-block fallbacks if
// the kernel refuses the combined request (notably executable direct memory).
bool protectRun(Region& region,uint64_t from,uint64_t to,int prot,uint64_t block){
    protectionBlocks.fetch_add((to-from)/block,std::memory_order_relaxed);
    protectionCalls.fetch_add(1,std::memory_order_relaxed);
    if(mprotect(reinterpret_cast<void*>(from),to-from,prot)==0)return true;
    for(auto b=from;b<to;b+=block){
        protectionCalls.fetch_add(1,std::memory_order_relaxed);
        if(mprotect(reinterpret_cast<void*>(b),block,prot)==0)continue;
#if !defined(__linux__)
        if(region.addressOnly&&(prot&PROT_EXEC)&&backing.count(b)&&toFlexible(b)){
            protectionCalls.fetch_add(1,std::memory_order_relaxed);
            if(mprotect(reinterpret_cast<void*>(b),block,prot)==0)continue;
        }
#else
        (void)region;
#endif
        if((prot&PROT_WRITE)&&(prot&PROT_EXEC)){
            static bool said=false;
            if(!said){said=true;log("VirtualProtect: bloco gravavel e executavel recusado em "+hex(b)+"; fica executavel");}
            protectionCalls.fetch_add(1,std::memory_order_relaxed);
            if(mprotect(reinterpret_cast<void*>(b),block,PROT_READ|PROT_EXEC)==0)continue;
        }
        return false;
    }
    return true;
}
// Gives [from, to) the protection `protect`. The kernel protects whole blocks
// (16 KiB on the console) and the last protection set on any page of a block
// wins for all of it (title 0.20), so each block touched gets the union of
// its pages' protections (title 0.63: the end of xg_x.dll's code shared a
// block with its data, which made it non-executable). A block that would be
// writable and executable at once is asked so; if the kernel refuses, it is
// left executable and the log says so.
bool commit(Region& region,uint64_t base,uint64_t from,uint64_t to,uint32_t protect) {
#if !defined(__linux__)
    if(region.addressOnly&&hostProtection(protect)>0&&!back(from,to))return false;
#endif
    for(auto at=from;at<to;at+=Page)region.pages[(at-base)/Page]=protect;
    uint64_t block=azimuth::platform::protectionGranularity();
    uint64_t first=base+(from-base)/block*block,last=base+(to-base+block-1)/block*block;
    uint64_t watchFrom=0;unsigned watchCount=0;
    WatchProtection watchChanges[WatchProtectionBatch];
    auto flushWatch=[&](){if(!watchCount)return true;bool ok=watchProtectRange(watchFrom,watchChanges,watchCount);watchCount=0;return ok;};
    uint64_t runFrom=0,runTo=0;int runProt=0;
    auto flush=[&](){if(runFrom==runTo)return true;bool ok=protectRun(region,runFrom,runTo,runProt,block);runFrom=runTo=0;return ok;};
    for(auto b=first;b<last;b+=block) {
#if !defined(__linux__)
        if(region.addressOnly&&!backed(b))continue;   // only reserved: nothing to protect
#endif
        int prot=PROT_NONE;unsigned writable=0,committed=0;
        for(auto page=b;page<b+block&&page<base+region.size;page+=Page) {
            int one=hostProtection(region.pages[(page-base)/Page]);
            if(one>0)prot|=one;
            if(region.pages[(page-base)/Page])committed|=1u<<unsigned((page-b)/Page);
            if(one>0&&(one&PROT_WRITE))writable|=1u<<unsigned((page-b)/Page);
        }
        if(region.writeWatch){
            if(watchCount&&(watchFrom+watchCount*block!=b||watchCount==WatchProtectionBatch)&&!flushWatch())return false;
            if(!watchCount)watchFrom=b;
            watchChanges[watchCount++]={prot,writable,committed};
            continue;
        }
        if(runFrom!=runTo&&(runTo!=b||runProt!=prot)&&!flush())return false;
        if(runFrom==runTo){runFrom=b;runProt=prot;}
        runTo=b+block;
    }
    return flushWatch()&&flush();
}
bool decommit(Region& region,uint64_t base,uint64_t from,uint64_t to) {
    if(region.writeWatch&&!commit(region,base,from,to,0))return false;
    if(region.addressOnly) {
#if !defined(__linux__)
        unback(from,to);
#endif
        return commit(region,base,from,to,0);
    }
    if(!clear(from,to))return false;
    for(auto at=from;at<to;at+=Page)region.pages[(at-base)/Page]=0;
    return true;
}
Region newRegion(uint64_t size,uint32_t protect,bool placeholder,bool addressOnly=false) {
    Region region{size,protect,std::vector<uint32_t>(size/Page,0),placeholder,nullptr,false,addressOnly};return region;
}
// Unmaps a region's addresses for good (MEM_RELEASE).
void release(uint64_t base,const Region& region) {
    if(region.writeWatch)watchForget(base,region.size);
#if !defined(__linux__)
    if(region.addressOnly){unback(base,base+mapped(region.size),false);return;}
#endif
    munmap(reinterpret_cast<void*>(base),mapped(region.size));
}
// Splits the placeholder holding [from, to) so that exactly that range is one.
bool splitPlaceholder(uint64_t from,uint64_t to) {
    auto found=holding(from,to-from);
    if(found==regions.end()||!found->second.placeholder)return false;
    auto base=found->first,end=base+found->second.size;bool addressOnly=found->second.addressOnly;
    regions.erase(found);
    if(from>base)regions[base]=newRegion(from-base,PageNoAccess,true,addressOnly);
    regions[from]=newRegion(to-from,PageNoAccess,true,addressOnly);
    if(end>to)regions[to]=newRegion(end-to,PageNoAccess,true,addressOnly);
    return true;
}
// Joins the adjacent placeholders that cover [from, to) into one.
bool coalescePlaceholders(uint64_t from,uint64_t to) {
    auto found=regions.find(from);
    uint64_t at=from;
    while(at<to){auto r=regions.find(at);if(r==regions.end()||!r->second.placeholder)return false;at+=r->second.size;}
    if(at!=to)return false;
    bool addressOnly=found->second.addressOnly;
    for(at=from;at<to;){auto r=regions.find(at);at+=r->second.size;regions.erase(r);}
    regions[from]=newRegion(to-from,PageNoAccess,true,addressOnly);
    return true;
}

// The address requirements of VirtualAlloc2/MapViewOfFile3's extended
// parameters (MemExtendedParameterAddressRequirements, type 1):
// MEM_ADDRESS_REQUIREMENTS { LowestStartingAddress, HighestEndingAddress, Alignment }.
struct Requirements {uint64_t lowest=0,highest=0,alignment=0;};
Requirements requested(const void* parameters,uint32_t count) {
    Requirements r;
    auto p=static_cast<const uint8_t*>(parameters);
    for(uint32_t n=0;p&&n<count;++n) {
        uint64_t type,value;std::memcpy(&type,p+n*16,8);std::memcpy(&value,p+n*16+8,8);
        if((type&0xff)==1&&value)std::memcpy(&r,reinterpret_cast<const void*>(value),sizeof r);
    }
    return r;
}
// The engine's return addresses on the calling thread's stack, for the log.
std::string engineCallers() {
    auto stack=static_cast<const uint64_t*>(__builtin_frame_address(0));
    // Only up to the thread's host frame (title 0.84: past it, a new thread's
    // stack ended and this report faulted).
    uintptr_t top=azimuth::currentStackTop();
    auto inside=[&](unsigned n){return top&&reinterpret_cast<uintptr_t>(stack+n+1)<=top;};
    uint64_t base=process().imageBase,size=process().imageSize;
    std::string out;unsigned found=0;
    for(unsigned n=0;n<512&&found<6&&inside(n);++n)
        if(stack[n]>=base&&stack[n]-base<size){out+=" engine+"+hex(stack[n]-base).substr(2);++found;}
    return out;
}

// Maps `size` bytes of `section` from `offset` at `at` (replacing what is there).
bool mapView(Section& section,uint64_t offset,uint64_t at,uint64_t size,uint32_t protect) {
    int prot=hostProtection(protect);if(prot<0)prot=PROT_READ|PROT_WRITE;
#if defined(__linux__)
    void* p=mmap(reinterpret_cast<void*>(at),size,prot,MAP_SHARED|MAP_FIXED,section.fd,off_t(offset));
    return p!=MAP_FAILED;
#else
    constexpr int CpuRead=0x1,CpuWrite=0x2,CpuExec=0x4,MapFixed=0x10;
    int sce=((prot&PROT_READ)?CpuRead:0)|((prot&PROT_WRITE)?CpuWrite:0)|((prot&PROT_EXEC)?CpuExec:0);
    munmap(reinterpret_cast<void*>(at),mapped(size));
    void* address=reinterpret_cast<void*>(at);
    int result=sceKernelMapDirectMemory(&address,size,sce,MapFixed,section.physical+int64_t(offset),Granularity);
    if(result<0||address!=reinterpret_cast<void*>(at)){log("MapViewOfFile3: sceKernelMapDirectMemory falhou ("+hex(uint32_t(result))+") em "+hex(at));clear(at,at+size);return false;}
    return true;
#endif
}
}

namespace azimuth::win32 {
// CreateFileMapping with INVALID_HANDLE_VALUE (Files.cpp).
void* createSection(uint64_t size,uint32_t protect) {
    if(!size){setLastError(ErrorInvalidParameter);return nullptr;}
    auto section=std::make_shared<Section>();
    section->size=up(size,Granularity);section->protect=protect;
#if defined(__linux__)
    section->fd=int(syscall(319,"azimuth-section",0));   // memfd_create
    if(section->fd<0||ftruncate(section->fd,off_t(section->size))!=0){setLastError(ErrorNotEnoughMemory);return nullptr;}
#else
    int64_t physical=0;
    if(sceKernelAllocateDirectMemory(0,int64_t(sceKernelGetDirectMemorySize()),section->size,Granularity,0,&physical)<0){setLastError(ErrorNotEnoughMemory);return nullptr;}
    section->physical=physical;
    // Fresh direct memory is not cleared: map it once to zero it, as Windows' sections start.
    void* address=nullptr;
    if(sceKernelMapDirectMemory(&address,section->size,0x3,0,physical,Granularity)>=0){std::memset(address,0,section->size);munmap(address,section->size);}
#endif
    log("Secao de memoria compartilhada: "+std::to_string(section->size>>10)+" KiB");
    return addHandle(section);
}
// MapViewOfFile on a section: a view anywhere. Null when `handle` is not a section.
void* mapSectionView(void* handle,uint64_t offset,uint64_t size,uint32_t access) {
    auto section=std::dynamic_pointer_cast<Section>(findHandle(handle));
    if(!section)return nullptr;
    if(!size)size=section->size-offset;
    if(offset+size>section->size||offset%Granularity){setLastError(ErrorInvalidParameter);return nullptr;}
    std::lock_guard<std::mutex> guard(lock);
    auto total=up(size,Page);
    auto base=mapReserved(0,total);
    if(!base){setLastError(ErrorNotEnoughMemory);return nullptr;}
    uint32_t protect=(access&0x2)?PageReadWrite:PageReadOnly;   // FILE_MAP_WRITE
    if(!mapView(*section,offset,base,total,protect)){munmap(reinterpret_cast<void*>(base),mapped(total));setLastError(ErrorNotEnoughMemory);return nullptr;}
    auto& region=regions[base]=newRegion(total,protect,false);
    region.view=section;
    for(auto& p:region.pages)p=protect;
    return reinterpret_cast<void*>(base);
}
// UnmapViewOfFile(2) on a section view; false when `view` is not one.
bool unmapSectionView(const void* view,bool preservePlaceholder) {
    std::lock_guard<std::mutex> guard(lock);
    auto found=regions.find(reinterpret_cast<uint64_t>(view));
    if(found==regions.end()||!found->second.view)return false;
    auto base=found->first;auto size=found->second.size;bool addressOnly=found->second.addressOnly;
    if(preservePlaceholder) {
#if !defined(__linux__)
        if(addressOnly){sceKernelMunmap(reinterpret_cast<void*>(base),mapped(size));reserveAddresses(base,size,Block);}
        else
#endif
        clear(base,base+size);
        found->second=newRegion(size,PageNoAccess,true,addressOnly);
    }else {
        munmap(reinterpret_cast<void*>(base),mapped(size));regions.erase(found);
    }
    return true;
}
}

extern "C" void* win_VirtualAlloc(void* address,uint64_t size,uint32_t type,uint32_t protect) {
    return win_VirtualAlloc2(nullptr,address,size,type,protect,nullptr,0);
}
// VirtualAlloc2 (Windows 10): VirtualAlloc for the current process, plus
// placeholders and an alignment requirement.
static void* virtualAlloc2(void* process,void* address,uint64_t size,uint32_t type,uint32_t protect,void* parameters,uint32_t count) {
    if(process&&reinterpret_cast<intptr_t>(process)!=-1)unsupported("VirtualAlloc2 para outro processo");
    // MEM_WRITE_WATCH uses physical-block write faults, preserving guest protections.
    if(type&~(MemCommit|MemReserve|MemTopDown|MemReservePlaceholder|MemReplacePlaceholder|MemLargePages|0x200000u))unsupported("VirtualAlloc com tipo "+hex(type));
    if(hostProtection(protect)<0)unsupported("VirtualAlloc com protecao "+hex(protect));
    if(!size||!(type&(MemCommit|MemReserve))){setLastError(ErrorInvalidParameter);return nullptr;}
    auto at=reinterpret_cast<uint64_t>(address);
    std::lock_guard<std::mutex> guard(lock);
    if(type&MemReplacePlaceholder) {
        // A placeholder becomes private memory: exactly one, whole.
        auto found=regions.find(at);
        if(found==regions.end()||!found->second.placeholder||found->second.size!=up(size,Page)){setLastError(ErrorInvalidAddress);return nullptr;}
        auto& region=found->second;
        static std::atomic<unsigned> occupied{0};
        if(region.size>=(1u<<20)&&often(occupied))log("VirtualAlloc2 ocupa placeholder de "+std::to_string(region.size>>10)+" KiB em "+hex(at)+
                                     (type&MemCommit?" com commit":"")+", protecao "+hex(protect)+"; chamado de"+engineCallers());
        region.placeholder=false;region.allocationProtect=protect;
        if(type&0x200000u){if(!watchPrepare(at,region.size)){setLastError(ErrorNotEnoughMemory);return nullptr;}region.writeWatch=true;}
        if((type&MemCommit)&&!commit(region,at,at,at+region.size,protect)){setLastError(ErrorNotEnoughMemory);return nullptr;}
        return address;
    }
    if(type&MemReserve) {
        bool placeholder=type&MemReservePlaceholder;
        auto base=down(at,Granularity),total=up(at+size,Page)-base;
        if(at&&holding(base,1)!=regions.end()){setLastError(ErrorInvalidAddress);return nullptr;}
        bool addressOnly=false;
        auto requirements=requested(parameters,count);
#if !defined(__linux__)
        // The game's memory manager reserves its regions at the Xbox's fixed
        // addresses (0x60000000000, 0x800000000), outside a title's map area.
        // Refused, its pools came out with no memory and their set-up loop ran
        // through the host's heap (title 0.82). Such a reservation goes
        // anywhere in the area instead, keeping the alignment the address had
        // (up to 4 GiB): the game keeps the address VirtualAlloc returns.
        // Smaller ones (the game image at 0x140000000) fail as before: the
        // engine has its own fallback for those.
        if(at&&total>=AddressOnlyFrom&&(base<AppAreaStart||base+mapped(total)>AppAreaEnd)) {
            uint64_t keep=base&(~base+1);
            if(!keep||keep>(4ull<<30))keep=4ull<<30;
            if(keep>requirements.alignment)requirements.alignment=keep;
            log("Reserva em "+hex(at)+" fora da area do titulo: realocada (alinhamento "+hex(requirements.alignment)+")");
            at=0;base=0;total=up(size,Page);
        }
#endif
#if !defined(__linux__)
        // Every reservation is address space in the title's area with direct
        // memory behind its commits (title 1.01): anonymous mappings come from
        // a small window and flexible memory, and the game's many small
        // reservations (with a 4 GiB-1 TiB range) ran out of it, after which
        // the engine fell back to 1 GiB blocks until direct memory ran out.
        addressOnly=true;
        if(addressOnly)base=reserveAddresses(base,total,requirements.alignment,requirements.lowest,requirements.highest);
        else
#endif
        base=mapReserved(base,total,at?Granularity:requirements.alignment);
        if(!base) {
            log("VirtualAlloc2 de "+std::to_string(total>>10)+" KiB"+(at?" em "+hex(at):std::string())+" falhou, tipo "+hex(type)+
                (requirements.highest?", faixa "+hex(requirements.lowest)+"-"+hex(requirements.highest)+" alinhamento "+hex(requirements.alignment):std::string())+
                "; chamado de"+engineCallers());
            setLastError(at?ErrorInvalidAddress:ErrorNotEnoughMemory);return nullptr;
        }
        static std::atomic<unsigned> reserved{0};
        if((total>=(1u<<20)||at)&&often(reserved))log(std::string(placeholder?"VirtualAlloc2 placeholder ":"VirtualAlloc reserva ")+std::to_string(total>>10)+" KiB em "+hex(base)+
                                   (at?" (pedido em "+hex(at)+")":"")+(type&MemCommit?" com commit":"")+", protecao "+hex(protect));
        auto& region=regions[base]=newRegion(total,protect,placeholder,addressOnly);
        if(type&0x200000u){
            if(!watchPrepare(base,total)){release(base,region);regions.erase(base);setLastError(ErrorNotEnoughMemory);return nullptr;}
            region.writeWatch=true;
        }
        if((type&MemCommit)&&!placeholder&&!commit(region,base,base,base+total,protect)) {
            release(base,region);regions.erase(base);
            setLastError(ErrorNotEnoughMemory);return nullptr;
        }
        return reinterpret_cast<void*>(base);
    }
    // MEM_COMMIT inside an existing reservation.
    auto from=down(at,Page),to=up(at+size,Page);
    auto found=at?holding(from,to-from):regions.end();
    if(found==regions.end()||found->second.placeholder){setLastError(ErrorInvalidAddress);return nullptr;}
    if(!commit(found->second,found->first,from,to,protect)){setLastError(errorFromErrno(errno));return nullptr;}
    static std::atomic<unsigned> committed{0};
    if(to-from>=(1u<<20)&&often(committed))log("VirtualAlloc commit de "+std::to_string((to-from)>>10)+" KiB em "+hex(from)+", protecao "+hex(protect));
    return reinterpret_cast<void*>(from);
}
static int32_t virtualFree(void* address,uint64_t size,uint32_t type) {
    auto at=reinterpret_cast<uint64_t>(address);
    std::lock_guard<std::mutex> guard(lock);
    if((type&(MemPreservePlaceholder|MemCoalescePlaceholders))&&size>=(1u<<20))
        log("VirtualFree "+hex(type)+" de "+std::to_string(size>>10)+" KiB em "+hex(at));
    if(type==(MemRelease|MemPreservePlaceholder)) {
        // A part of a placeholder becomes a placeholder of its own; private
        // memory that replaced a placeholder becomes one again.
        auto found=regions.find(at);
        if(found!=regions.end()&&!found->second.placeholder&&!found->second.view&&(!size||size==found->second.size)) {
            auto bytes=found->second.size;bool addressOnly=found->second.addressOnly;
            if(found->second.writeWatch)watchForget(at,bytes);
            if(!clear(at,at+bytes,addressOnly)){setLastError(errorFromErrno(errno));return 0;}
            found->second=newRegion(bytes,PageNoAccess,true,addressOnly);return 1;
        }
        // At page granularity, as Windows splits (title 0.93: the engine split
        // its graphics placeholder leaving 16 KiB at the end).
        if(!size||at%Page||size%Page||!splitPlaceholder(at,at+size)){setLastError(ErrorInvalidParameter);return 0;}
        return 1;
    }
    if(type==(MemRelease|MemCoalescePlaceholders)) {
        if(!size||!coalescePlaceholders(at,at+size)){setLastError(ErrorInvalidParameter);return 0;}
        return 1;
    }
    if(type!=MemRelease&&type!=MemDecommit)unsupported("VirtualFree com tipo "+hex(type));
    if(type==MemRelease) {
        auto found=regions.find(at);
        if(size||found==regions.end()||found->second.view){setLastError(ErrorInvalidParameter);return 0;}
        static std::atomic<unsigned> freed{0};
        if(found->second.size>=(1u<<20)&&often(freed))log("VirtualFree libera "+std::to_string(found->second.size>>10)+" KiB em "+hex(at));
        release(at,found->second);regions.erase(found);
        return 1;
    }
    auto found=holding(at,1);
    if(found==regions.end()||found->second.placeholder){setLastError(ErrorInvalidAddress);return 0;}
    uint64_t from=down(at,Page),to=size?up(at+size,Page):found->first+found->second.size;
    if(!size&&at!=found->first){setLastError(ErrorInvalidParameter);return 0;}
    if(to>found->first+found->second.size){setLastError(ErrorInvalidAddress);return 0;}
    if(!decommit(found->second,found->first,from,to)){setLastError(errorFromErrno(errno));return 0;}
    return 1;
}
// MapViewOfFile3 (Windows 10): a section view at an address, usually
// replacing a placeholder exactly.
static void* mapViewOfFile3(void* handle,void* process,void* address,uint64_t offset,uint64_t size,uint32_t type,uint32_t protect) {
    if(process&&reinterpret_cast<intptr_t>(process)!=-1)unsupported("MapViewOfFile3 para outro processo");
    auto section=std::dynamic_pointer_cast<Section>(findHandle(handle));
    if(!section)unsupported("MapViewOfFile3 de um mapeamento que nao e secao de memoria");
    if(!address)return mapSectionView(handle,offset,size,hostProtection(protect)&PROT_WRITE?0x2:0x4);
    auto at=reinterpret_cast<uint64_t>(address);
    if(!size)size=section->size-offset;
    if(offset+size>section->size){setLastError(ErrorInvalidParameter);return nullptr;}
    std::lock_guard<std::mutex> guard(lock);
    auto total=up(size,Page);
    auto found=regions.find(at);
    if(type&MemReplacePlaceholder) {
        if(found==regions.end()||!found->second.placeholder||found->second.size!=total){setLastError(ErrorInvalidAddress);return nullptr;}
    }else if(holding(at,1)!=regions.end()){setLastError(ErrorInvalidAddress);return nullptr;}
    bool addressOnly=found!=regions.end()&&found->second.addressOnly;
    static std::atomic<unsigned> views{0};
    if(often(views))log("MapViewOfFile3: "+std::to_string(total>>10)+" KiB da secao em "+hex(at)+(type&MemReplacePlaceholder?" (placeholder)":""));
    if(!mapView(*section,offset,at,total,protect)){setLastError(ErrorNotEnoughMemory);return nullptr;}
    auto& region=regions[at]=newRegion(total,protect,false,addressOnly);
    region.view=section;
    for(auto& p:region.pages)p=protect;
    return address;
}
extern "C" int32_t win_UnmapViewOfFile2(void* process,const void* view,uint32_t flags) {
    if(process&&reinterpret_cast<intptr_t>(process)!=-1)unsupported("UnmapViewOfFile2 para outro processo");
    if(unmapSectionView(view,flags&MemPreservePlaceholder))return 1;
    return win_UnmapViewOfFile(view);
}
// VirtualProtect on the engine's or a DLL's image (title 1.49): the 1.0.2
// engine makes a buffer in its .rdata writable, fills it with the game's TLS
// template and gives it back its protection. The kernel protects whole blocks
// (platform::protectionGranularity), so a block keeps the rights its sections
// were mapped with and gains the ones asked for; it never becomes writable and
// executable unless it already was. The protection asked for each page is
// kept, for the old protection the next call reports.
static std::mutex imageProtectionLock;
static std::map<uint64_t,uint32_t> imageProtections;
static unsigned imageRights(uint64_t base,uint64_t from,uint64_t to) {
    auto header=reinterpret_cast<const uint8_t*>(base);
    uint32_t pe,headerSize;uint16_t count,optional;
    std::memcpy(&pe,header+0x3c,4);std::memcpy(&count,header+pe+6,2);std::memcpy(&optional,header+pe+20,2);std::memcpy(&headerSize,header+pe+24+60,4);
    unsigned rights=from-base<up(headerSize,Page)?PROT_READ:0;
    for(unsigned n=0;n<count;++n) {
        auto section=header+pe+24+optional+40*n;
        uint32_t virtualSize,rva,fileSize,characteristics;
        std::memcpy(&virtualSize,section+8,4);std::memcpy(&rva,section+12,4);std::memcpy(&fileSize,section+16,4);std::memcpy(&characteristics,section+36,4);
        uint64_t start=base+rva,end=base+up(rva+uint64_t(std::max(virtualSize,fileSize)),Page);
        if(end<=from||start>=to)continue;
        if(characteristics&0x40000000)rights|=PROT_READ;
        if(characteristics&0x80000000)rights|=PROT_READ|PROT_WRITE;
        if(characteristics&0x20000000)rights|=PROT_READ|PROT_EXEC;
    }
    return rights;
}
static uint32_t windowsProtection(unsigned rights) {
    if(rights&PROT_EXEC)return rights&PROT_WRITE?PageExecuteReadWrite:PageExecuteRead;
    if(rights&PROT_WRITE)return PageReadWrite;
    return rights&PROT_READ?PageReadOnly:PageNoAccess;
}
static int32_t imageProtect(uint64_t base,uint64_t from,uint64_t to,uint32_t protect,uint32_t* oldProtect) {
    std::lock_guard<std::mutex> guard(imageProtectionLock);
    auto known=imageProtections.find(from);
    *oldProtect=known!=imageProtections.end()?known->second:windowsProtection(imageRights(base,from,from+Page));
    uint64_t block=azimuth::platform::protectionGranularity();
    for(auto at=down(from-base,block)+base;at<to;at+=block) {
        unsigned mapped=imageRights(base,at,at+block);
        unsigned rights=mapped|unsigned(hostProtection(protect));
        if((rights&(PROT_WRITE|PROT_EXEC))==(PROT_WRITE|PROT_EXEC)&&(mapped&(PROT_WRITE|PROT_EXEC))!=(PROT_WRITE|PROT_EXEC))
            unsupported("VirtualProtect deixaria a imagem gravavel e executavel em "+hex(at));
        if(mprotect(reinterpret_cast<void*>(at),block,int(rights))){setLastError(errorFromErrno(errno));return 0;}
    }
    for(auto page=from;page<to;page+=Page)imageProtections[page]=protect;
    return 1;
}
static int32_t virtualProtect(void* address,uint64_t size,uint32_t protect,uint32_t* oldProtect) {
    if(hostProtection(protect)<0)unsupported("VirtualProtect com protecao "+hex(protect));
    if(!oldProtect||!size){setLastError(ErrorInvalidParameter);return 0;}
    auto at=reinterpret_cast<uint64_t>(address);auto from=down(at,Page),to=up(at+size,Page);
    std::unique_lock<std::mutex> guard(lock);
    auto found=holding(from,to-from);
    if(found==regions.end()) {
        guard.unlock();
        uint64_t image=0;
        if(at>=process().imageBase&&to-process().imageBase<=up(process().imageSize,Page))image=process().imageBase;
        else if(auto module=azimuth::moduleContaining(at);module&&azimuth::moduleContaining(to-1)==module)image=module;
        if(image)return imageProtect(image,from,to,protect,oldProtect);
        unsupported("VirtualProtect fora da memoria do VirtualAlloc: "+hex(at));
    }
    auto& region=found->second;
    for(auto page=from;page<to;page+=Page)if(!region.pages[(page-found->first)/Page]){setLastError(ErrorInvalidAddress);return 0;}
    auto old=region.pages[(from-found->first)/Page];
    if(!commit(region,found->first,from,to,protect)){setLastError(errorFromErrno(errno));return 0;}
    *oldProtect=old;return 1;
}
extern "C" uint64_t win_VirtualQuery(const void* address,void* buffer,uint64_t bufferSize) {
    if(!buffer||bufferSize<sizeof(BasicInformation)){setLastError(ErrorInvalidParameter);return 0;}
    auto at=reinterpret_cast<uint64_t>(address);
    std::unique_lock<std::mutex> guard(lock);
    auto found=holding(at,1);
    if(found==regions.end()) {
        // Not VirtualAlloc's: answered as Windows would (title 0.96: an engine
        // worker queried a block the engine had just released).
        constexpr uint32_t MemImage=0x1000000;
        auto info=static_cast<BasicInformation*>(buffer);
        auto page=down(at,Page);
        uint64_t moduleBase=0;
        if(at>=process().imageBase&&at-process().imageBase<process().imageSize)moduleBase=process().imageBase;
        else moduleBase=azimuth::moduleContaining(at);
        if(moduleBase) {
            // The engine or a DLL the host mapped: committed image pages.
            *info={page,moduleBase,PageExecuteReadWrite,0,0,Page,MemCommit,PageExecuteReadWrite,MemImage,0};
            return sizeof(BasicInformation);
        }
        if(at>=HostHeapFrom&&at<HostHeapTo) {
            // The host's heap (HeapAlloc and the DLLs' malloc): committed private memory.
            *info={page,page,PageReadWrite,0,0,Page,MemCommit,PageReadWrite,MemPrivate,0};
            return sizeof(BasicInformation);
        }
        // Free: up to the next region VirtualAlloc made.
        auto next=regions.upper_bound(at);
        uint64_t end=next==regions.end()?up(at+1,Granularity):next->first;
        *info={page,0,0,0,0,end-page,MemFree,PageNoAccess,0,0};
        static std::atomic<unsigned> said{0};
        if(said++<10)log("VirtualQuery de "+hex(at)+": memoria livre ("+std::to_string((end-page)>>10)+" KiB ate a proxima regiao)");
        return sizeof(BasicInformation);
    }
    auto const& region=found->second;auto base=found->first;
    auto first=(down(at,Page)-base)/Page,last=first;
    while(last+1<region.pages.size()&&region.pages[last+1]==region.pages[first])++last;
    auto protect=region.pages[first];
    *static_cast<BasicInformation*>(buffer)={base+first*Page,base,region.allocationProtect,0,0,(last-first+1)*Page,
                                             protect?MemCommit:MemReserve,protect,region.view?MemMapped:MemPrivate,0};
    return sizeof(BasicInformation);
}

// ---- The exported forms: each failure, and each call in an address-only
// region, goes to the log with the engine's return addresses (title 0.70:
// the engine died right after splitting its graphics heap, logging nothing).
namespace {
std::atomic<unsigned> reported{0};
bool addressOnlyAt(uint64_t address) {
    std::lock_guard<std::mutex> guard(lock);
    auto found=holding(address,1);
    return found!=regions.end()&&found->second.addressOnly;
}
void trace(const std::string& call,bool failed,uint64_t address,bool known=false) {
    if(!failed&&!known&&!addressOnlyAt(address))return;
    if(reported.fetch_add(1)>=200)return;
    log(call+(failed?" FALHOU (erro "+std::to_string(win_GetLastError())+")":" ok")+"; chamado de"+engineCallers());
}
}
extern "C" void* win_VirtualAlloc2(void* process,void* address,uint64_t size,uint32_t type,uint32_t protect,void* parameters,uint32_t count) {
    SlowMemoryCall timing("VirtualAlloc2",address,size);
    auto result=virtualAlloc2(process,address,size,type,protect,parameters,count);
    auto at=reinterpret_cast<uint64_t>(result?result:address);
    trace("VirtualAlloc2("+hex(reinterpret_cast<uint64_t>(address))+", "+hex(size)+", tipo "+hex(type)+", protecao "+hex(protect)+") = "+hex(reinterpret_cast<uint64_t>(result)),!result,at);
    return result;
}
extern "C" int32_t win_VirtualFree(void* address,uint64_t size,uint32_t type) {
    SlowMemoryCall timing("VirtualFree",address,size);
    auto at=reinterpret_cast<uint64_t>(address);bool inside=addressOnlyAt(at);
    auto result=virtualFree(address,size,type);
    trace("VirtualFree("+hex(at)+", "+hex(size)+", tipo "+hex(type)+")",!result,at,inside);
    return result;
}
extern "C" void* win_MapViewOfFile3(void* handle,void* process,void* address,uint64_t offset,uint64_t size,uint32_t type,uint32_t protect,void*,uint32_t) {
    auto result=mapViewOfFile3(handle,process,address,offset,size,type,protect);
    trace("MapViewOfFile3("+hex(reinterpret_cast<uint64_t>(address))+", offset "+hex(offset)+", "+hex(size)+", tipo "+hex(type)+") = "+hex(reinterpret_cast<uint64_t>(result)),
          !result,reinterpret_cast<uint64_t>(address));
    return result;
}
extern "C" int32_t win_VirtualProtect(void* address,uint64_t size,uint32_t protect,uint32_t* oldProtect) {
    SlowMemoryCall timing("VirtualProtect",address,size);
    auto result=virtualProtect(address,size,protect,oldProtect);
    trace("VirtualProtect("+hex(reinterpret_cast<uint64_t>(address))+", "+hex(size)+", "+hex(protect)+")",!result,reinterpret_cast<uint64_t>(address));
    return result;
}


// Region validation and lifetime serialization stay outside the fault handler.
namespace azimuth::win32 {
uint32_t queryWriteWatch(uint32_t flags,void* base,uint64_t size,void** addresses,uint64_t* count,uint32_t* granularity){
    auto at=reinterpret_cast<uint64_t>(base);
    if(flags&~1u||!at||!size||at+size<at||!addresses||!count||!granularity||!*count)return 87;
    std::lock_guard<std::mutex> guard(lock);
    auto found=holding(at,size);
    if(found==regions.end()||!found->second.writeWatch)return 87;
    *granularity=4096;
    return watchQuery(at,size,addresses,count,flags&1);
}
uint32_t resetWriteWatch(void* base,uint64_t size){
    auto at=reinterpret_cast<uint64_t>(base);
    if(!at||!size||at+size<at)return 87;
    std::lock_guard<std::mutex> guard(lock);auto found=holding(at,size);
    if(found==regions.end()||!found->second.writeWatch)return 87;
    return watchReset(at,size)?0:87;
}
}

std::string azimuth::win32::memoryProtectionReport(){
    return "Protecao sem WriteWatch: "+std::to_string(protectionBlocks.exchange(0))+" blocos, "+std::to_string(protectionCalls.exchange(0))+" chamadas mprotect (~10 s)";
}

std::vector<std::string> azimuth::win32::memorySlowEvents(){
    std::vector<std::string> lines;
    for(auto& slot:slowMemory){
        unsigned expected=2;
        if(!slot.state.compare_exchange_strong(expected,1,std::memory_order_acquire))continue;
        auto e=slot.event;slot.state.store(0,std::memory_order_release);
        lines.push_back(std::string("Memoria lenta: ")+e.api+", thread "+std::to_string(e.thread)+
            ", inicio mono ns "+std::to_string(e.begin)+", fim mono ns "+std::to_string(e.end)+
            ", duracao ms "+std::to_string(double(e.end-e.begin)/1e6)+", endereco "+hex(e.address)+", bytes pedidos "+std::to_string(e.size));
    }
    auto lost=slowMemoryDropped.exchange(0);
    if(lost)lines.push_back("Memoria lenta: "+std::to_string(lost)+" eventos descartados por buffer cheio (~10 s)");
    return lines;
}

std::string azimuth::win32::directReleaseReport(){
    return "Liberacao direta: "+std::to_string(directUnmapCalls.exchange(0))+" unmap, "+std::to_string(directReleaseCalls.exchange(0))+
        " release, "+std::to_string(directReserveCalls.exchange(0))+" reservas, "+std::to_string(directReleasedBytes.exchange(0))+
        " bytes liberados, "+std::to_string(directReleaseRuns.exchange(0))+" releases agrupados (~10 s)";
}
