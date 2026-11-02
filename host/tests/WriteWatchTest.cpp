#include "../src/Teb.h"
#include "../src/Platform.h"
#include "../src/GuestThreads.h"
#include "../src/win32/Win32.h"
#include "../src/win32/WriteWatch.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <cerrno>
#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT
namespace azimuth::win32 {
void log(const std::string& text){std::printf("%s\n",text.c_str());}
void stopGuest(const std::string& text){std::fprintf(stderr,"STOP %s\n",text.c_str());std::exit(2);}
}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
// Simulate a kernel rejecting a combined protection request; the fallback
// must still perform each block and preserve the next reset epoch.
std::atomic<bool> rejectProtectionRanges{false};
std::atomic<unsigned> nativeProtectionCalls{0};
extern "C" int __real_mprotect(void*,size_t,int);
extern "C" int __wrap_mprotect(void* p,size_t size,int protection){
    ++nativeProtectionCalls;
    if(rejectProtectionRanges.load()&&size>azimuth::platform::protectionGranularity()){errno=ENOMEM;return -1;}
    return __real_mprotect(p,size,protection);
}
int main(){
    azimuth::ProcessBlock process(0);azimuth::ThreadBlock thread(process,nullptr,nullptr,256);
    CHECK(!thread.enter());azimuth::installFaultHandlers(2);
    constexpr uint64_t Size=65536;
    auto p=static_cast<uint8_t*>(win_VirtualAlloc(nullptr,Size,0x203000,4));CHECK(p);
    void* pages[16];uint64_t count=16;uint32_t gran=0;
    auto query=[&](uint32_t flags){count=16;return win_GetWriteWatch(flags,p,Size,pages,&count,&gran);};
    CHECK(query(0)==0&&count==0&&gran==4096);
    p[7]=23;p[7]=0; // Restoring bytes must not hide a write.
    CHECK(query(0)==0&&count>0&&pages[0]==p);
    CHECK(query(1)==0&&count>0);CHECK(query(0)==0&&count==0);
    p[32768]=1;CHECK(query(0)==0&&count>0);
    CHECK(win_ResetWriteWatch(p,Size)==0);CHECK(query(0)==0&&count==0);
    // The kernel cannot copy directly into a read-protected watched page.
    // The host I/O bridge must turn copyout into ordinary tracked stores.
    auto input=std::tmpfile();CHECK(input);
    CHECK(std::fwrite("tracked",1,7,input)==7);CHECK(!std::fflush(input));
    CHECK(azimuth::win32::watchedPread(fileno(input),p,7,0)==7);
    CHECK(!std::memcmp(p,"tracked",7));CHECK(query(1)==0&&count>0);
    std::rewind(input);CHECK(win_fread(p,1,7,input)==7);
    CHECK(query(1)==0&&count>0);
    CHECK(lseek(fileno(input),0,SEEK_SET)==0);CHECK(win__read(fileno(input),p,7)==7);
    CHECK(query(1)==0&&count>0);CHECK(!std::fclose(input));
    // Guest protections remain authoritative; a read-only page is not a
    // recoverable tracking fault. Test the dispatcher without issuing SIGSEGV.
    uint32_t old=0;CHECK(win_VirtualProtect(p,16384,2,&old));
    CHECK(!azimuth::win32::watchFault(reinterpret_cast<uint64_t>(p)));
    CHECK(win_VirtualProtect(p,16384,4,&old));p[3]=4;
    CHECK(query(1)==0&&count>0);
    CHECK(win_VirtualFree(p,16384,0x4000));
    CHECK(win_VirtualAlloc(p,16384,0x1000,4)==p);
    CHECK(query(0)==0&&count==0);p[0]=5;CHECK(query(1)==0&&count>0);
    // Queries/reset race with a writer. A final write after reset must always
    // be recorded. Handshakes make each tested epoch unambiguous.
    std::atomic<unsigned> request{0},done{0};
    std::thread writer([&]{for(unsigned i=1;i<=1000;++i){while(request.load()!=i)std::this_thread::yield();p[20000]=uint8_t(i);done=i;}});
    for(unsigned i=1;i<=1000;++i){
        CHECK(win_ResetWriteWatch(p,Size)==0);request=i;
        while(done.load()!=i){CHECK(query(0)==0);}
        CHECK(query(0)==0&&count>0);
    }
    writer.join();
    std::atomic<bool> stop{false};
    std::thread racingWriter([&]{while(!stop.load())__atomic_fetch_add(p+40000,uint8_t(1),__ATOMIC_SEQ_CST);});
    for(unsigned i=0;i<1000;++i)CHECK(query(1)==0);
    stop=true;racingWriter.join();
    CHECK(win_ResetWriteWatch(p,Size)==0);p[40000]=42;
    CHECK(query(1)==0&&count>0);CHECK(query(0)==0&&count==0);
    p[0]=1;p[32768]=2;count=1;
    CHECK(win_GetWriteWatch(1,p,Size,pages,&count,&gran)==234&&count==1);
    CHECK(query(0)==0&&count>0); // Unreported pages were not cleared.
    CHECK(win_ResetWriteWatch(p,Size)==0);
    CHECK(win_GetWriteWatch(2,p,Size,pages,&count,&gran)==87);
    CHECK(win_VirtualFree(p,0,0x8000));
    CHECK(!azimuth::win32::watchFault(reinterpret_cast<uint64_t>(p)));
    constexpr size_t Payload=(4u<<20)+123;
    std::vector<uint8_t> source(Payload);for(size_t i=0;i<Payload;++i)source[i]=uint8_t(i*37+i/257);
    auto large=static_cast<uint8_t*>(win_VirtualAlloc(nullptr,Payload,0x203000,4));CHECK(large);
    auto file=std::tmpfile();CHECK(file);CHECK(std::fwrite(source.data(),1,Payload,file)==Payload);CHECK(!std::fflush(file));
    auto fill=[&](){size_t total=0,calls=0;while(total<Payload){auto got=azimuth::win32::watchedPread(fileno(file),large+total,Payload-total,off_t(total));CHECK(got>0);total+=size_t(got);++calls;}CHECK(!std::memcmp(large,source.data(),Payload));return calls;};
    (void)azimuth::win32::watchProtectionReport();(void)azimuth::win32::watchStageReport();
    auto calls=fill();std::printf("Leitura rastreada de %zu bytes em %zu chamadas pread\n",Payload,calls);
    std::printf("%s\n",azimuth::win32::watchProtectionReport().c_str());
    std::printf("%s\n",azimuth::win32::watchStageReport().c_str());
    // The engine seals/unseals large allocations through VirtualProtect.
    // Both directions must use bounded ranges, keep dirty history and data,
    // and leave sealed pages to the real VEH rather than tracking recovery.
    auto nativeBlocks=(Payload+azimuth::platform::protectionGranularity()-1)/azimuth::platform::protectionGranularity();
    auto expectedRanges=(nativeBlocks+azimuth::win32::WatchProtectionBatch-1)/azimuth::win32::WatchProtectionBatch;
    nativeProtectionCalls=0;
    CHECK(win_VirtualProtect(large,Payload,2,&old));
    CHECK(nativeProtectionCalls==expectedRanges);
    for(size_t at=0;at<Payload;at+=azimuth::platform::protectionGranularity())
        CHECK(!azimuth::win32::watchFault(reinterpret_cast<uint64_t>(large+at)));
    nativeProtectionCalls=0;
    CHECK(win_VirtualProtect(large,Payload,4,&old));
    CHECK(nativeProtectionCalls==expectedRanges);
    std::printf("VirtualProtect de %zu blocos: %u chamadas por transicao (esperado %zu)\n",nativeBlocks,nativeProtectionCalls.load(),expectedRanges);
    CHECK(!std::memcmp(large,source.data(),Payload));
    rejectProtectionRanges=true;
    CHECK(win_VirtualProtect(large,Payload,2,&old));
    CHECK(!azimuth::win32::watchFault(reinterpret_cast<uint64_t>(large)));
    CHECK(win_VirtualProtect(large,Payload,4,&old));
    rejectProtectionRanges=false;
    CHECK(!std::memcmp(large,source.data(),Payload));
    std::vector<void*> modified((Payload+4095)/4096);uint64_t capacity=modified.size();
    CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity==modified.size());
    // Mixed requested protections at both batch boundaries are not merged.
    for(size_t at : {size_t(31)*azimuth::platform::protectionGranularity(),size_t(32)*azimuth::platform::protectionGranularity()}){
        CHECK(win_VirtualProtect(large+at,4096,2,&old));
        CHECK(!azimuth::win32::watchFault(reinterpret_cast<uint64_t>(large+at)));
        CHECK(win_VirtualProtect(large+at,4096,4,&old));
    }
    capacity=1;
    CHECK(win_GetWriteWatch(1,large,Payload,modified.data(),&capacity,&gran)==234&&capacity==1&&modified[0]==large);
    capacity=modified.size();
    CHECK(!win_GetWriteWatch(1,large,Payload,modified.data(),&capacity,&gran)&&capacity==modified.size()-1);
    rejectProtectionRanges=true;fill();CHECK(!win_ResetWriteWatch(large,Payload));rejectProtectionRanges=false;
    capacity=modified.size();CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity==0);
    CHECK(azimuth::win32::watchedPread(fileno(file),large,10,Payload)==0);
    errno=0;CHECK(azimuth::win32::watchedPread(-1,large,10,0)==-1&&errno==EBADF);
    capacity=modified.size();CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity==0);
    std::rewind(file);size_t total=0;
    while(total<Payload){auto got=win__read(fileno(file),large+total,uint32_t(Payload-total));CHECK(got>0);total+=size_t(got);}
    CHECK(!std::memcmp(large,source.data(),Payload));
    CHECK(!win_ResetWriteWatch(large,Payload));std::rewind(file);
    CHECK(win_fread(large,7,(Payload+6)/7,file)==Payload/7);
    CHECK(std::feof(file)&&ftello(file)==off_t(Payload)&&!std::memcmp(large,source.data(),Payload));
    // Reprotect concurrently with the stores made by the large I/O copy.
    std::atomic<bool> beginReset{false};
    std::thread resetting([&]{while(!beginReset.load())std::this_thread::yield();for(unsigned i=0;i<16;++i)CHECK(!win_ResetWriteWatch(large,Payload));});
    beginReset=true;fill();resetting.join();
    CHECK(!win_ResetWriteWatch(large,Payload));large[Payload-1]=22;
    capacity=modified.size();CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity>0);
    // CRT bulk writes use the same guarded preparation as file copies.
    CHECK(!win_ResetWriteWatch(large,Payload));nativeProtectionCalls=0;
    CHECK(win_memcpy(large,source.data(),Payload)==large);
    CHECK(nativeProtectionCalls==expectedRanges);
    CHECK(!std::memcmp(large,source.data(),Payload));
    std::printf("CRT memcpy de %zu blocos: %u chamadas mprotect\n",nativeBlocks,nativeProtectionCalls.load());
    capacity=modified.size();CHECK(!win_GetWriteWatch(1,large,Payload,modified.data(),&capacity,&gran)&&capacity==modified.size());
    nativeProtectionCalls=0;CHECK(win_memset(large,0x6d,Payload)==large);
    CHECK(nativeProtectionCalls==expectedRanges);
    for(size_t i=0;i<Payload;++i)CHECK(large[i]==0x6d);
    CHECK(win_memcpy(large,source.data(),Payload)==large);
    auto expected=source;
    CHECK(!win_ResetWriteWatch(large,Payload));
    std::memmove(expected.data()+7,expected.data(),Payload-7);
    CHECK(win_memmove(large+7,large,Payload-7)==large+7);
    CHECK(!std::memcmp(large,expected.data(),Payload));
    CHECK(!win_ResetWriteWatch(large,Payload));
    std::memmove(expected.data(),expected.data()+13,Payload-13);
    CHECK(win_memmove(large,large+13,Payload-13)==large);
    CHECK(!std::memcmp(large,expected.data(),Payload));
    // Partial/unaligned stores retain bytes on both sides; only intersecting
    // native blocks may be conservatively reported dirty.
    CHECK(!win_ResetWriteWatch(large,Payload));
    size_t begin=azimuth::platform::protectionGranularity()-17,length=16384+13;
    std::memset(expected.data()+begin,0x93,length);
    CHECK(win_memset(large+begin,0x93,length)==large+begin);
    CHECK(!std::memcmp(large,expected.data(),Payload));
    capacity=modified.size();CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity>0);
    auto unit=azimuth::platform::protectionGranularity();
    for(size_t i=0;i<capacity;++i){auto offset=static_cast<uint8_t*>(modified[i])-large;CHECK(size_t(offset)>=begin/unit*unit&&size_t(offset)<(begin+length+unit-1)/unit*unit);}
    CHECK(!win_ResetWriteWatch(large,Payload));
    CHECK(win_memmove(large,large,Payload)==large);CHECK(win_memcpy(large,large,0)==large);CHECK(win_memset(large,3,0)==large);
    capacity=modified.size();CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity==0);
    rejectProtectionRanges=true;CHECK(win_memcpy(large,source.data(),Payload)==large);rejectProtectionRanges=false;
    CHECK(!std::memcmp(large,source.data(),Payload));
    std::thread bulkReset([&]{for(unsigned i=0;i<64;++i)CHECK(!win_ResetWriteWatch(large,Payload));});
    for(unsigned i=0;i<16;++i){win_memset(large,0x55,Payload);win_memcpy(large,source.data(),Payload);win_memmove(large+3,large,Payload-3);}
    bulkReset.join();CHECK(!win_ResetWriteWatch(large,Payload));
    CHECK(win_memcpy(large,source.data(),Payload)==large);CHECK(!std::memcmp(large,source.data(),Payload));
    capacity=modified.size();CHECK(!win_GetWriteWatch(0,large,Payload,modified.data(),&capacity,&gran)&&capacity==modified.size());
    std::printf("%s\n",azimuth::win32::bulkWriteReport().c_str());
    CHECK(!std::fclose(file));CHECK(win_VirtualFree(large,0,0x8000));
    std::printf("%s\n",azimuth::win32::watchedIoReport().c_str());
    std::puts("OK: write watch, restored values, reset epochs, protections, decommit and 1000 concurrent writes");
}
