// The Vulkan driver for the host's vulkan-1.dll (see Radv.h).
#include "Radv.h"
#include "../Sha256.h"
#include "../win32/Win32.h"
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <sys/stat.h>
#if defined(__linux__)
#include <dlfcn.h>
#endif

using azimuth::win32::log;

namespace {
std::string hex(uint64_t value){char text[24];std::snprintf(text,sizeof text,"0x%llx",(unsigned long long)value);return text;}

#if defined(AZIMUTH_TITLE) && !defined(__linux__)
extern "C" {
struct SceKernelModuleSegmentInfo {uint64_t address;uint32_t size;int32_t prot;};
struct SceKernelModuleInfo {uint64_t size;char name[256];SceKernelModuleSegmentInfo segments[4];uint32_t segmentCount;uint8_t fingerprint[20];};
static_assert(sizeof(SceKernelModuleInfo)==0x160,"SceKernelModuleInfo");
int sceKernelLoadStartModule(const char* path,size_t argc,const void* argv,uint32_t flags,void* option,int* result);
int sceKernelDlsym(int module,const char* name,void** address);
int sceKernelGetModuleInfo(int handle,SceKernelModuleInfo* info);
int sceSysmoduleIsLoadedInternal(unsigned int id);
int sceSysmoduleLoadModuleInternal(unsigned int id);
}

// The one RADV build whose layout the host knows (Mesa 26.2.0, Vulkan
// 1.4.354, first taken from Playden's wine kit), checked by size and SHA-256.
// Since title 1.27 it ships in the title's own folder (radv/), so no other
// program's install is needed: Playden 0.8.0 moved its files and the title
// stopped finding the driver.
constexpr const char* Candidates[]={
    "/app0/radv/libvulkan.prx",
    "/system_ex/app/FAKE78002/radv/libvulkan.prx",
    "/data/homebrew/FAKE78002/radv/libvulkan.prx",
    "/data/azimuth-ps5/radv/libvulkan.prx",
};
constexpr long long KnownSize=24941932;
constexpr uint8_t KnownSha256[32]={
    0xdb,0x21,0xb3,0x94,0xc7,0x9a,0xf0,0xfa,0xb4,0x80,0x72,0xa7,0xa8,0x6e,0x2f,0x25,
    0x60,0x9f,0x2d,0x12,0x25,0x52,0x83,0x18,0x10,0xa4,0x7b,0xad,0x0c,0x64,0x1b,0x94};
// The title's sceKernelLoadStartModule runs only the module's init, which for
// this build is module+0x0 (DT_INIT is 0): "xor eax,eax; ret". RADV's
// constructors run from e_entry, guarded by a flag INIT_ARRAY[1] also sets.
// Without them ACO's opcode tables (.bss, filled by INIT_ARRAY[4]) stay zero
// and every shader is emitted with opcode 0 (titles 0.13-0.14).
constexpr uint64_t Entry=0x4ef0,ConstructorsRan=0x17f3278,OpcodeTables=0x17fa088;

bool knownBuild(const char* path) {
    std::FILE* file=std::fopen(path,"rb");
    if(!file){log("RADV: "+std::string(path)+" nao abriu");return false;}
    azimuth::Hash hash(azimuth::Hash::Kind::Sha256);
    static uint8_t buffer[1<<20];size_t got;long long total=0;
    while((got=std::fread(buffer,1,sizeof buffer,file))>0){hash.add(buffer,got);total+=got;}
    std::fclose(file);
    auto digest=hash.finish();
    bool same=total==KnownSize;
    for(size_t n=0;same&&n<32;++n)same=digest[n]==KnownSha256[n];
    if(!same)log("RADV: "+std::string(path)+" ("+std::to_string(total)+" bytes) nao e o build conhecido; o host so conhece o db21b394...");
    return same;
}

void* loadRadv() {
    const char* path=nullptr;struct stat st;
    for(auto candidate:Candidates)if(stat(candidate,&st)==0){path=candidate;break;}
    if(!path){log("RADV: libvulkan.prx nao encontrado na pasta do titulo (radv/) nem em /data/azimuth-ps5/radv");return nullptr;}
    if(!knownBuild(path))return nullptr;
    // RADV's system dependencies (ps5-payload-sdk v0.45 IDs). Already loaded
    // in every title run so far; never unloaded.
    static const struct {unsigned id;const char* name;} dependencies[]={
        {0x80000080u,"libSceAgcDriver"},{0x80000094u,"libSceAgc"},{0x80000022u,"libSceVideoOut"}};
    for(auto& d:dependencies) {
        if(sceSysmoduleIsLoadedInternal(d.id)==0)continue;
        int error=sceSysmoduleLoadModuleInternal(d.id);
        log("RADV: dependencia "+std::string(d.name)+" carregada pelo sysmodule: "+hex(unsigned(error)));
        if(error)return nullptr;
    }
    int result=0,handle=sceKernelLoadStartModule(path,0,nullptr,0,nullptr,&result);
    log("RADV: sceKernelLoadStartModule "+std::string(path)+": id "+std::to_string(handle)+", entrada "+std::to_string(result));
    if(handle<0||result)return nullptr;
    SceKernelModuleInfo info{};info.size=sizeof info;
    if(int error=sceKernelGetModuleInfo(handle,&info)){log("RADV: sceKernelGetModuleInfo "+hex(unsigned(error)));return nullptr;}
    uint64_t base=info.segments[0].address;
    auto flag=reinterpret_cast<volatile const uint32_t*>(base+ConstructorsRan);
    auto opcodes=reinterpret_cast<volatile const uint32_t*>(base+OpcodeTables);
    if(!*flag) {
        auto entry=reinterpret_cast<int(*)(size_t,const void*)>(base+Entry);
        int code=entry(0,nullptr);
        log("RADV: construtores executados pela entrada "+hex(base+Entry)+" (retorno "+std::to_string(code)+")");
    }
    if(!*flag||!opcodes[0]) {
        log("RADV: construtores nao confirmados (flag "+std::to_string(*flag)+", tabelas "+hex(opcodes[0])+")");
        return nullptr;
    }
    void* proc=nullptr;
    if(sceKernelDlsym(handle,"vkGetInstanceProcAddr",&proc)||!proc){log("RADV: vkGetInstanceProcAddr ausente");return nullptr;}
    // The ICD interface the probe negotiated (version 5) before any Vulkan call.
    void* negotiate=nullptr;
    if(!sceKernelDlsym(handle,"vk_icdNegotiateLoaderICDInterfaceVersion",&negotiate)&&negotiate) {
        uint32_t version=5;int32_t code=reinterpret_cast<int32_t(*)(uint32_t*)>(negotiate)(&version);
        log("RADV: interface de ICD versao "+std::to_string(version)+" (resultado "+std::to_string(code)+")");
    }
    // Never unloaded: RADV registers atexit handlers that need its code (0.14).
    log("RADV pronto: base "+hex(base)+", vkGetInstanceProcAddr "+hex(reinterpret_cast<uint64_t>(proc)));
    return proc;
}
#elif defined(__linux__)
void* loadRadv() {
    void* loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
    if(!loader){log(std::string("Vulkan: dlopen libvulkan.so.1 falhou: ")+dlerror());return nullptr;}
    void* proc=dlsym(loader,"vkGetInstanceProcAddr");
    log("Vulkan do Linux: vkGetInstanceProcAddr "+hex(reinterpret_cast<uint64_t>(proc)));
    return proc;
}
#else
void* loadRadv() {
    // GPU probe 0.8: a payload maps RADV's code but cannot execute it.
    log("RADV: so executa num titulo; este host e um payload, sem Vulkan");
    return nullptr;
}
#endif
}

namespace azimuth::gpu {
void* driverInstanceProcAddr() {
    static std::once_flag once;static void* proc=nullptr;
    std::call_once(once,[]{proc=loadRadv();});
    return proc;
}
}
