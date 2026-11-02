// Windows 10's placeholders and section views (win32/Memory.cpp) in the order
// the engine uses them for its graphics heap and ESRAM (title 0.65): reserve a
// placeholder, split it, put private memory and a shared section in parts,
// map the section twice, unmap keeping the placeholder, coalesce, release.
#include "../src/win32/Win32.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <csignal>
#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT
namespace azimuth::win32 {
void log(const std::string& text){std::printf("log: %s\n",text.c_str());}
void stopGuest(const std::string& reason){std::printf("STOP: %s\n",reason.c_str());std::exit(2);}
}
#define CHECK(x) do{if(!(x)){std::printf("FALHOU: %s (linha %d)\n",#x,__LINE__);return 1;}}while(0)
int main() {
    const uint64_t MiB=1<<20;
    // 64 MiB placeholder
    auto heap=static_cast<uint8_t*>(win_VirtualAlloc2(nullptr,nullptr,64*MiB,0x2000|0x40000,0x01,nullptr,0));
    CHECK(heap);
    // split off 8 MiB at +16 MiB and 32 MiB at +32 MiB
    CHECK(win_VirtualFree(heap+16*MiB,8*MiB,0x8000|0x2));
    CHECK(win_VirtualFree(heap+32*MiB,32*MiB,0x8000|0x2));
    // private memory replaces the 8 MiB placeholder
    auto priv=static_cast<uint8_t*>(win_VirtualAlloc2(nullptr,heap+16*MiB,8*MiB,0x1000|0x2000|0x4000,0x04,nullptr,0));
    CHECK(priv==heap+16*MiB);
    std::memset(priv,0x5a,8*MiB);CHECK(priv[8*MiB-1]==0x5a);
    // ESRAM: a 32 MiB section mapped into the 32 MiB placeholder and also anywhere
    auto section=win_CreateFileMappingW(reinterpret_cast<void*>(~uintptr_t(0)),nullptr,0x04,0,32*MiB,nullptr);
    CHECK(section);
    auto view=static_cast<uint8_t*>(win_MapViewOfFile3(section,nullptr,heap+32*MiB,0,32*MiB,0x4000,0x04,nullptr,0));
    CHECK(view==heap+32*MiB);
    auto other=static_cast<uint8_t*>(win_MapViewOfFile(section,0x2|0x4,0,0,0));
    CHECK(other);
    CHECK(view[123]==0);
    view[123]=77;view[32*MiB-1]=99;
    CHECK(other[123]==77&&other[32*MiB-1]==99);
    // unmap preserving the placeholder, then map again
    CHECK(win_UnmapViewOfFile2(nullptr,view,0x2));
    view=static_cast<uint8_t*>(win_MapViewOfFile3(section,nullptr,heap+32*MiB,0,32*MiB,0x4000,0x04,nullptr,0));
    CHECK(view&&view[123]==77);
    CHECK(win_UnmapViewOfFile2(nullptr,view,0x2));
    CHECK(win_UnmapViewOfFile(other));
    // private memory back to a placeholder, then coalesce everything and release
    CHECK(win_VirtualFree(priv,0,0x8000|0x2));
    CHECK(win_VirtualFree(heap,64*MiB,0x8000|0x1));
    CHECK(win_VirtualFree(heap,0,0x8000));
    // aligned reservation
    uint64_t requirements[3]={0,0,4*MiB};uint64_t parameter[2]={1,reinterpret_cast<uint64_t>(requirements)};
    auto aligned=reinterpret_cast<uint64_t>(win_VirtualAlloc2(nullptr,nullptr,8*MiB,0x2000|0x1000,0x04|0x400,parameter,1));
    CHECK(aligned&&aligned%(4*MiB)==0);
    // a placeholder split at 16 KiB (title 0.93), and VirtualQuery of released memory (title 0.96)
    auto split=static_cast<uint8_t*>(win_VirtualAlloc2(nullptr,nullptr,4*MiB,0x2000|0x40000,0x01,nullptr,0));
    CHECK(split&&win_VirtualFree(split+MiB,3*MiB-16384,0x8000|0x2));
    auto freed=static_cast<uint8_t*>(win_VirtualAlloc(nullptr,2*MiB,0x3000,0x04));
    CHECK(freed&&win_VirtualFree(freed,0,0x8000));
    uint64_t info[6]={};
    CHECK(win_VirtualQuery(freed+0x8000,info,sizeof info)==48&&uint32_t(info[4])==0x10000);   // State MEM_FREE
    // Uniform runs can be batched, but splitting the middle must preserve
    // both read-only edges (including with the console's 16 KiB blocks).
    auto protectedRange=static_cast<uint8_t*>(win_VirtualAlloc(nullptr,65536,0x3000,4));
    CHECK(protectedRange);uint32_t old=0;
    CHECK(win_VirtualProtect(protectedRange,65536,2,&old)&&old==4);
    CHECK(win_VirtualProtect(protectedRange+16384,16384,4,&old)&&old==2);
    protectedRange[16384]=77;CHECK(protectedRange[16384]==77);
    for(auto offset:{0,32768,49152}){
        auto child=fork();CHECK(child>=0);
        if(!child){rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);static_cast<volatile uint8_t*>(protectedRange)[offset]=1;_exit(0);}
        int status=0;CHECK(waitpid(child,&status,0)==child);
        CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGSEGV);
    }
    CHECK(win_VirtualProtect(protectedRange,65536,4,&old));protectedRange[0]=2;protectedRange[65535]=3;
    CHECK(win_VirtualFree(protectedRange,0,0x8000));
    std::printf("%s\n",azimuth::win32::memoryProtectionReport().c_str());
    std::printf("OK: placeholders, secoes, alinhamento e protecoes mistas\n");
    return 0;
}
