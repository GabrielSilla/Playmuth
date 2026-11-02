#include "WriteWatch.h"
#include "WatchedIo.h"
#include "Win32.h"
#include "../HostHeap.h"
#include <cerrno>
#include <cstdio>
#include "../Platform.h"
#include <atomic>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

namespace azimuth::win32 {
namespace {
// No allocation, mutex, guest-memory access or logging under a block guard.
// Thus a synchronous write fault cannot interrupt its own guard holder.
// Keys survive release; reusing an address reuses its entry. Exhaustion is
// reported rather than silently disabling tracking.
constexpr unsigned Slots=1u<<19;
struct Entry {
    std::atomic<uint64_t> key{0};
    std::atomic_flag busy=ATOMIC_FLAG_INIT;
    int protection=0,applied=-1;
    unsigned writable=0,committed=0,dirty=0;
};
Entry entries[Slots];
std::atomic<uint64_t> protectionCalls{0},protectionSkipped{0},protectionBatches{0},protectionBatchBlocks{0};
struct Stage {
    std::atomic<uint64_t> calls{0},nanos{0},maximum{0};
    void record(int64_t begin){
        auto end=fastMonotonicNs();uint64_t ns=end>begin?uint64_t(end-begin):0;
        calls.fetch_add(1,std::memory_order_relaxed);nanos.fetch_add(ns,std::memory_order_relaxed);
        auto old=maximum.load(std::memory_order_relaxed);while(old<ns&&!maximum.compare_exchange_weak(old,ns,std::memory_order_relaxed)){}
    }
    std::string report(const char* name){
        auto n=calls.exchange(0),total=nanos.exchange(0),peak=maximum.exchange(0);char text[160];
        std::snprintf(text,sizeof text,"%s %llu ops, %.2f ms, max %.2f ms",name,static_cast<unsigned long long>(n),double(total)/1e6,double(peak)/1e6);return text;
    }
};
Stage kernelStage,faultStage,queryStage,resetStage,prepareStage;
struct TimedStage {Stage& stage;int64_t begin=fastMonotonicNs();explicit TimedStage(Stage& s):stage(s){}~TimedStage(){stage.record(begin);}};
bool kernelProtect(uint64_t base,uint64_t size,int protection){
    auto began=fastMonotonicNs();protectionCalls.fetch_add(1,std::memory_order_relaxed);
    int result=mprotect(reinterpret_cast<void*>(base),size,protection),saved=errno;
    kernelStage.record(began);errno=saved;return result==0;
}
static_assert(std::atomic<uint64_t>::is_always_lock_free,"signal-safe keys");
Entry* find(uint64_t block,bool create=false){
    unsigned slot=unsigned((block>>12)*0x9e3779b97f4a7c15ull>>(64-19));
    for(unsigned n=0;n<Slots;++n,slot=(slot+1)&(Slots-1)){
        auto& e=entries[slot];auto key=e.key.load(std::memory_order_acquire);
        if(key==block)return &e;
        if(!key){
            if(!create)return nullptr;
            // Creation is serialized by Memory.cpp's region mutex.
            e.key.store(block,std::memory_order_release);return &e;
        }
    }
    return nullptr;
}
struct Guard {
    Entry& e;
    explicit Guard(Entry& value):e(value){while(e.busy.test_and_set(std::memory_order_acquire))__builtin_ia32_pause();}
    ~Guard(){e.busy.clear(std::memory_order_release);}
};
uint64_t blockSize(){return azimuth::platform::protectionGranularity();}
bool setProtection(Entry& e,uint64_t b,uint64_t size,int protection){
    if(e.applied==protection){protectionSkipped.fetch_add(1,std::memory_order_relaxed);return true;}
    if(!kernelProtect(b,size,protection)){e.applied=-1;return false;}
    e.applied=protection;return true;
}
bool apply(Entry& e,uint64_t b,uint64_t size){
    int protection=e.protection;
    if((e.dirty&e.writable)!=e.writable)protection&=~PROT_WRITE;
    return setProtection(e,b,size,protection);
}
unsigned maskFor(uint64_t b,uint64_t size,uint64_t from,uint64_t to){
    unsigned mask=0;
    for(unsigned i=0;i<size/4096;++i)if(b+i*4096>=from&&b+i*4096<to)mask|=1u<<i;
    return mask;
}
constexpr unsigned BatchBlocks=WatchProtectionBatch;
struct Batch {
    uint64_t base,unit;unsigned count;Entry* entries[BatchBlocks]{};
    Batch(uint64_t b,uint64_t to):base(b),unit(blockSize()),count(unsigned(std::min<uint64_t>(BatchBlocks,(to-b+unit-1)/unit))){
        for(unsigned i=0;i<count;++i)if((entries[i]=find(base+i*unit)))
            while(entries[i]->busy.test_and_set(std::memory_order_acquire))__builtin_ia32_pause();
    }
    ~Batch(){for(unsigned i=count;i>0;--i)if(entries[i-1])entries[i-1]->busy.clear(std::memory_order_release);}
    bool complete() const{for(unsigned i=0;i<count;++i)if(!entries[i])return false;return true;}
    // -1 leaves an entry unchanged. A rejected run falls back to individual
    // blocks; all metadata remains guarded until the kernel work is complete.
    bool protect(const int* wanted){
        for(unsigned i=0;i<count;){
            if(!entries[i]||wanted[i]<0){++i;continue;}
            if(entries[i]->applied==wanted[i]){protectionSkipped.fetch_add(1,std::memory_order_relaxed);++i;continue;}
            unsigned end=i+1;
            while(end<count&&entries[end]&&wanted[end]==wanted[i]&&entries[end]->applied!=wanted[end])++end;
            if(kernelProtect(base+i*unit,(end-i)*unit,wanted[i])){
                for(unsigned k=i;k<end;++k)entries[k]->applied=wanted[i];
                if(end-i>1){protectionBatches.fetch_add(1,std::memory_order_relaxed);protectionBatchBlocks.fetch_add(end-i,std::memory_order_relaxed);}
            }else{
                for(unsigned k=i;k<end;++k)entries[k]->applied=-1;
                for(unsigned k=i;k<end;++k)if(!setProtection(*entries[k],base+k*unit,unit,wanted[k]))return false;
            }
            i=end;
        }
        return true;
    }
};
void freezeWanted(const Batch& batch,int* wanted){
    for(unsigned i=0;i<batch.count;++i){auto& e=*batch.entries[i];wanted[i]=e.writable&&(e.dirty&e.writable)==e.writable?e.protection&~PROT_WRITE:-1;}
}
void restoreWanted(const Batch& batch,int* wanted){
    for(unsigned i=0;i<batch.count;++i){auto& e=*batch.entries[i];wanted[i]=e.writable&&(e.dirty&e.writable)==e.writable?e.protection:-1;}
}

}
bool watchPrepare(uint64_t base,uint64_t size){
    (void)fastMonotonicNs(); // Initialize the clock before taking a block guard.
    auto unit=blockSize();
    for(auto b=base;b<base+size;b+=unit){auto e=find(b,true);if(!e)return false;Guard g(*e);e->protection=0;e->applied=-1;e->writable=e->committed=e->dirty=0;}
    return true;
}
bool watchProtectRange(uint64_t base,const WatchProtection* protections,unsigned count){
    if(!count||count>BatchBlocks)return false;
    auto unit=blockSize();Batch batch(base,base+count*unit);
    if(!batch.complete())return false;
    int wanted[BatchBlocks];
    for(unsigned i=0;i<count;++i){
        auto& e=*batch.entries[i];auto& next=protections[i];
        // Changed backing invalidates the cache. A permission change alone
        // preserves dirty history for pages which remain committed.
        if(next.committed!=e.committed)e.applied=-1;
        e.dirty&=next.committed&e.committed;
        e.protection=next.protection;e.writable=next.writable;e.committed=next.committed;
        wanted[i]=e.protection;
        if((e.dirty&e.writable)!=e.writable)wanted[i]&=~PROT_WRITE;
    }
    return batch.protect(wanted);
}
bool watchProtect(uint64_t b,uint64_t size,int protection,unsigned writable,unsigned committed){
    if(size!=blockSize())return false;
    WatchProtection next{protection,writable,committed};
    return watchProtectRange(b,&next,1);
}
void watchForget(uint64_t base,uint64_t size){
    auto unit=blockSize();
    for(auto b=base;b<base+size;b+=unit)if(auto e=find(b)){Guard g(*e);e->protection=0;e->applied=-1;e->writable=e->committed=e->dirty=0;}
}
bool watchFault(uint64_t address){
    auto began=fastMonotonicNs();
    auto unit=blockSize(),b=address/unit*unit;auto e=find(b);if(!e)return false;Guard g(*e);
    unsigned bit=1u<<unsigned((address-b)/4096);
    if(!(e->writable&bit))return false; // Real PAGE_READONLY/NOACCESS fault.
    // One physical block can contain four Windows pages on PS5. Reporting
    // all writable pages in that block is conservative and loses no write.
    e->dirty|=e->writable;
    bool result=apply(*e,b,unit);faultStage.record(began);return result;
}
uint32_t watchQuery(uint64_t base,uint64_t size,void** addresses,uint64_t* count,bool reset){
    TimedStage timing(queryStage);
    auto unit=blockSize(),from=base/4096*4096,to=(base+size+4095)/4096*4096;
    if(unit>16384)return 87;
    uint64_t capacity=*count,used=0;uint32_t error=0;
    for(auto b=from/unit*unit;b<to;){
        // Only stack-owned output is touched under the guards. Caller buffers
        // can themselves fault, invoke the guest VEH or reenter this API.
        void* output[BatchBlocks*4];unsigned returned[BatchBlocks]{},written=0,blocks=0;
        {
            Batch batch(b,to);blocks=batch.count;
            if(!batch.complete())error=87;
            int wanted[BatchBlocks];
            if(!error&&reset){freezeWanted(batch,wanted);if(!batch.protect(wanted))error=87;}
            if(!error){
                for(unsigned k=0;k<blocks&&!error;++k){
                    auto& e=*batch.entries[k];auto address=b+k*unit;
                    unsigned dirty=e.dirty&maskFor(address,unit,from,to);
                    for(unsigned page=0;page<unit/4096;++page)if(dirty&(1u<<page)){
                        if(used+written>=capacity){error=234;break;}
                        returned[k]|=1u<<page;output[written++]=reinterpret_cast<void*>(address+page*4096);
                    }
                }
                if(reset){
                    // Kernel protections were changed BEFORE clearing history.
                    // A later writer faults, waits for these guards, then enters
                    // the next epoch. Never clear a dirty bit after unlocking.
                    for(unsigned k=0;k<blocks;++k)batch.entries[k]->dirty&=~returned[k];
                    restoreWanted(batch,wanted);if(!batch.protect(wanted))error=87;
                }
            }
        }
        for(unsigned i=0;i<written;++i)addresses[used++]=output[i];
        if(error)break;b+=blocks*unit;
    }
    *count=used;return error;
}
bool watchReset(uint64_t base,uint64_t size){
    TimedStage timing(resetStage);
    auto unit=blockSize(),from=base/4096*4096,to=(base+size+4095)/4096*4096;
    for(auto b=from/unit*unit;b<to;){
        Batch batch(b,to);if(!batch.complete())return false;int wanted[BatchBlocks];
        freezeWanted(batch,wanted);if(!batch.protect(wanted))return false;
        for(unsigned i=0;i<batch.count;++i)batch.entries[i]->dirty&=~maskFor(b+i*unit,unit,from,to);
        restoreWanted(batch,wanted);if(!batch.protect(wanted))return false;
        b+=batch.count*unit;
    }
    return true;
}
static void prepareWatchedWrite(void* destination,size_t size){
    if(!size)return;
    auto began=fastMonotonicNs(),unit=int64_t(blockSize());
    auto from=reinterpret_cast<uint64_t>(destination),to=from+size;
    if(to>=from){
        for(auto b=from/uint64_t(unit)*uint64_t(unit);b<to;){
            Batch batch(b,to);int wanted[BatchBlocks];
            for(unsigned i=0;i<batch.count;++i){
                wanted[i]=-1;auto e=batch.entries[i];if(!e)continue;
                unsigned touched=maskFor(b+i*unit,unit,from/4096*4096,(to+4095)/4096*4096);
                // Sealed/no-access pages are NOT opened here. A store to
                // those still invokes the guest's unseal VEH during memcpy.
                if(touched&&(e->writable&touched)==touched&&(e->committed&touched)==touched){
                    e->dirty|=e->writable;wanted[i]=e->protection;
                }
            }
            // Failure leaves the ordinary fault path available; never pretend
            // the destination copy completed or suppress a real access fault.
            (void)batch.protect(wanted);b+=batch.count*uint64_t(unit);
        }
    }
    prepareStage.record(began);
}
void copyToWatched(void* destination,const void* source,size_t size){
    prepareWatchedWrite(destination,size);
    // No guard is held while storing into guest memory. A concurrent reset
    // may have rearmed a block already: the normal fault path tracks it again.
    std::memcpy(destination,source,size);
}
bool watchedBuffer(const void* buffer,size_t size){
    auto from=reinterpret_cast<uint64_t>(buffer),unit=blockSize();
    if(from+size<from)return false;
    for(auto b=from/unit*unit;b<from+size;b+=unit)if(auto e=find(b)){Guard g(*e);if(e->committed)return true;}
    return false;
}
namespace {
struct BulkWrites {
    std::atomic<uint64_t> calls{0},bytes{0};
    void prepare(void* to,size_t size){
        // Small host/guest writes keep their original fast path. Preparing
        // only the actual destination range cannot open a sealed page.
        if(size<16384||!watchedBuffer(to,size))return;
        calls.fetch_add(1,std::memory_order_relaxed);bytes.fetch_add(size,std::memory_order_relaxed);
        prepareWatchedWrite(to,size);
    }
    std::string report(const char* name){
        return std::string(name)+" "+std::to_string(calls.exchange(0))+" ops, "+std::to_string(bytes.exchange(0))+" bytes";
    }
};
BulkWrites bulkCopy,bulkMove,bulkSet;
}
void* trackedMemcpy(void* to,const void* from,size_t size){
    if(to==from||!size)return to;
    bulkCopy.prepare(to,size);return std::memcpy(to,from,size);
}
void* trackedMemmove(void* to,const void* from,size_t size){
    if(to==from||!size)return to;
    bulkMove.prepare(to,size);return std::memmove(to,from,size);
}
void* trackedMemset(void* to,int value,size_t size){
    if(!size)return to;
    bulkSet.prepare(to,size);return std::memset(to,value,size);
}
std::string bulkWriteReport(){
    return "Escritas CRT rastreadas: "+bulkCopy.report("memcpy")+" | "+bulkMove.report("memmove")+" | "+bulkSet.report("memset")+" (~10 s)";
}
struct WatchedIoSlot {char* bytes;bool used;WatchedIoSlot* next;};
namespace {
constexpr size_t IoCapacity=256u<<10;
constexpr unsigned MaxIoSlots=4;
struct IoPool {
    WatchedIoSlot* first=nullptr;unsigned count=0;
    ~IoPool(){while(first){auto next=first->next;azimuth::heapFree(first->bytes);azimuth::heapFree(first);first=next;}}
};
thread_local IoPool ioPool;
std::atomic<uint64_t> ioCalls{0},ioStdio{0},ioBytes{0},ioReadNs{0},ioCopyNs{0},ioReadMax{0},ioCopyMax{0},ioFallbacks{0};
void maximum(std::atomic<uint64_t>& counter,uint64_t value){auto old=counter.load(std::memory_order_relaxed);while(old<value&&!counter.compare_exchange_weak(old,value,std::memory_order_relaxed)){}}
}
WatchedIoBuffer::WatchedIoBuffer() noexcept {
    for(auto p=ioPool.first;p;p=p->next)if(!p->used){slot=p;break;}
    if(!slot&&ioPool.count<MaxIoSlots){
        auto bytes=static_cast<char*>(azimuth::heapAlloc(IoCapacity,64,false));
        auto node=bytes?static_cast<WatchedIoSlot*>(azimuth::heapAlloc(sizeof(WatchedIoSlot),16,false)):nullptr;
        if(node){*node={bytes,false,ioPool.first};ioPool.first=node;++ioPool.count;slot=node;}
        else if(bytes)azimuth::heapFree(bytes);
    }
    if(slot)slot->used=true;else ioFallbacks.fetch_add(1,std::memory_order_relaxed);
}
WatchedIoBuffer::~WatchedIoBuffer(){if(slot)slot->used=false;}
char* WatchedIoBuffer::data() noexcept{return slot?slot->bytes:fallback;}
size_t WatchedIoBuffer::capacity() const noexcept{return slot?IoCapacity:sizeof fallback;}
void recordWatchedIo(uint64_t bytes,uint64_t readNs,uint64_t copyNs,bool stdio){
    ioCalls.fetch_add(1,std::memory_order_relaxed);if(stdio)ioStdio.fetch_add(1,std::memory_order_relaxed);
    ioBytes.fetch_add(bytes,std::memory_order_relaxed);ioReadNs.fetch_add(readNs,std::memory_order_relaxed);ioCopyNs.fetch_add(copyNs,std::memory_order_relaxed);
    maximum(ioReadMax,readNs);maximum(ioCopyMax,copyNs);
}
ssize_t watchedPread(int fd,void* buffer,size_t size,off_t offset){
    if(!watchedBuffer(buffer,size))return ::pread(fd,buffer,size,offset);
    // Kernel copyout cannot deliver a user-mode tracking fault. Keep the
    // copy in user mode, so resets and the guest's unseal VEH remain valid.
    WatchedIoBuffer bounce;
    auto began=fastMonotonicNs();
    auto got=::pread(fd,bounce.data(),std::min(size,bounce.capacity()),offset);int saved=errno;
    auto readDone=fastMonotonicNs();
    if(got>0)copyToWatched(buffer,bounce.data(),size_t(got));
    auto copied=fastMonotonicNs();
    recordWatchedIo(got>0?uint64_t(got):0,uint64_t(readDone-began),uint64_t(copied-readDone),false);
    errno=saved;return got;
}
ssize_t watchedRead(int fd,void* buffer,size_t size){
    if(!watchedBuffer(buffer,size))return ::read(fd,buffer,size);
    WatchedIoBuffer bounce;
    auto began=fastMonotonicNs();
    auto got=::read(fd,bounce.data(),std::min(size,bounce.capacity()));int saved=errno;
    auto readDone=fastMonotonicNs();
    if(got>0)copyToWatched(buffer,bounce.data(),size_t(got));
    auto copied=fastMonotonicNs();
    recordWatchedIo(got>0?uint64_t(got):0,uint64_t(readDone-began),uint64_t(copied-readDone),false);
    errno=saved;return got;
}
std::string watchedIoReport(){
    auto calls=ioCalls.exchange(0),stdio=ioStdio.exchange(0),bytes=ioBytes.exchange(0);
    auto reading=ioReadNs.exchange(0),copying=ioCopyNs.exchange(0),maxRead=ioReadMax.exchange(0),maxCopy=ioCopyMax.exchange(0),fallbacks=ioFallbacks.exchange(0);
    char text[360];
    std::snprintf(text,sizeof text,"I/O rastreado: %llu operacoes (%llu fread), %llu bytes, leitura %.2f ms (max %.2f), copia %.2f ms (max %.2f), buffer reduzido %llu (~10 s)",
        static_cast<unsigned long long>(calls),static_cast<unsigned long long>(stdio),static_cast<unsigned long long>(bytes),double(reading)/1e6,double(maxRead)/1e6,double(copying)/1e6,double(maxCopy)/1e6,static_cast<unsigned long long>(fallbacks));
    return text;
}

std::string watchStageReport(){
    return "Etapas WriteWatch: "+kernelStage.report("mprotect")+" | "+faultStage.report("fault")+" | "+queryStage.report("consulta")+" | "+resetStage.report("reset")+" | "+prepareStage.report("preparo")+
        " | lotes "+std::to_string(protectionBatches.exchange(0))+", blocos agrupados "+std::to_string(protectionBatchBlocks.exchange(0))+" (~10 s)";
}
std::string watchProtectionReport(){
    return "Protecao WriteWatch: "+std::to_string(protectionCalls.exchange(0))+" chamadas mprotect, "+std::to_string(protectionSkipped.exchange(0))+" reaplicacoes evitadas (~10 s)";
}

}
