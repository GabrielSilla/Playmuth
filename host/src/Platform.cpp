#include "Platform.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#if defined(__linux__)
#include <asm/prctl.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/cpuset.h>
#ifndef SYS_sysarch
#define SYS_sysarch 165
#endif
#define AMD64_GET_GSBASE 130
#define AMD64_SET_GSBASE 131
extern "C" {
int sysarch(int,void*);
typedef struct {char reserved[45];char message[3075];} notify_request_t;
int sceKernelSendNotificationRequest(int device,notify_request_t* request,size_t size,int blocking);
int sceKernelConfiguredFlexibleMemorySize(size_t* size);
int sceKernelAvailableFlexibleMemorySize(size_t* size);
}
#endif

namespace azimuth::platform {
void* reserve(uint64_t preferred,size_t size) {
    void* at=mmap(reinterpret_cast<void*>(preferred),size,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(at==MAP_FAILED)throw std::runtime_error("mmap de "+std::to_string(size)+" bytes falhou: errno "+std::to_string(errno));
    return at;
}
void protect(void* at,size_t size,unsigned access) {
    int flags=PROT_NONE;
    if(access&Read)flags|=PROT_READ;
    if(access&Write)flags|=PROT_WRITE;
    if(access&Execute)flags|=PROT_EXEC;
    if(mprotect(at,size,flags))throw std::runtime_error("mprotect falhou: errno "+std::to_string(errno));
}
void release(void* at,size_t size) noexcept {if(at)munmap(at,size);}
int readable(const std::string& path) noexcept {
    FILE* file=std::fopen(path.c_str(),"rb");
    if(!file)return errno?errno:ENOENT;
    std::fclose(file);
    return 0;
}
size_t protectionGranularity() noexcept {
#if defined(__linux__)
    return std::getenv("AZIMUTH_BLOCK16K")?16384:4096;
#else
    return 16384;
#endif
}
int setGsBase(void* base) noexcept {
#if defined(__linux__)
    return syscall(SYS_arch_prctl,ARCH_SET_GS,reinterpret_cast<unsigned long>(base))==0?0:errno;
#else
    void* value=base;
#if defined(AZIMUTH_TITLE)
    return sysarch(AMD64_SET_GSBASE,&value)==0?0:errno;
#else
    return syscall(SYS_sysarch,AMD64_SET_GSBASE,&value)==0?0:errno;
#endif
#endif
}
int getGsBase(void** base) noexcept {
#if defined(__linux__)
    unsigned long value=0;
    int result=syscall(SYS_arch_prctl,ARCH_GET_GS,&value)==0?0:errno;
    *base=reinterpret_cast<void*>(value);return result;
#else
#if defined(AZIMUTH_TITLE)
    return sysarch(AMD64_GET_GSBASE,base)==0?0:errno;
#else
    return syscall(SYS_sysarch,AMD64_GET_GSBASE,base)==0?0:errno;
#endif
#endif
}
std::string reportDir() {
#if defined(__linux__)
    return ".";
#else
    mkdir("/data/azimuth-ps5",0777);
    return "/data/azimuth-ps5";
#endif
}
void notify(const std::string& text) noexcept {
#if !defined(__linux__)
    notify_request_t request;std::memset(&request,0,sizeof request);
    std::strncpy(request.message,text.c_str(),sizeof request.message-1);
    sceKernelSendNotificationRequest(0,&request,sizeof request,0);
#else
    (void)text;
#endif
}
const char* name() noexcept {
#if defined(__linux__)
    return "linux";
#else
    return "ps5";
#endif
}
std::string shareCpu(bool raise) {
#if defined(__linux__)
    (void)raise;return "";
#else
    char text[256];
    int policy=-1;sched_param parameter{};
    int got=pthread_getschedparam(pthread_self(),&policy,&parameter);
    cpuset_t own,process;CPU_ZERO(&own);CPU_ZERO(&process);
    int ownResult=cpuset_getaffinity(CPU_LEVEL_WHICH,CPU_WHICH_TID,-1,sizeof own,&own);
    int processResult=cpuset_getaffinity(CPU_LEVEL_CPUSET,CPU_WHICH_PID,-1,sizeof process,&process);
    auto mask=[](const cpuset_t& set){uint64_t m=0;for(int c=0;c<64&&c<CPU_SETSIZE;++c)if(CPU_ISSET(c,&set))m|=uint64_t(1)<<c;return m;};
    int affinity=-1;
    if(processResult==0&&mask(process)!=mask(own))affinity=cpuset_setaffinity(CPU_LEVEL_WHICH,CPU_WHICH_TID,-1,sizeof process,&process)==0?0:errno;
    // Every thread starts from the title's own priority (that of the first
    // caller), not from its creator's: a thread a raised one creates is not raised.
    static const int base=got==0?parameter.sched_priority:700;
    sched_param wanted=parameter;wanted.sched_priority=base;
    if(raise) {
        int highest=sched_get_priority_min(SCHED_RR);   // the console counts down: the minimum is the highest
        wanted.sched_priority=base-RaisedBy;
        if(highest>=0&&wanted.sched_priority<highest)wanted.sched_priority=highest;
    }
    int changed=got==0?pthread_setschedparam(pthread_self(),SCHED_RR,&wanted):got;
    int nowPolicy=-1;sched_param now{};pthread_getschedparam(pthread_self(),&nowPolicy,&now);
    std::snprintf(text,sizeof text,"politica %d prioridade %d (erro %d) -> politica %d prioridade %d (erro %d); afinidade 0x%llx (erro %d), processo 0x%llx (erro %d), ajuste %d",
                  policy,parameter.sched_priority,got,nowPolicy,now.sched_priority,changed,
                  static_cast<unsigned long long>(mask(own)),ownResult==0?0:errno,static_cast<unsigned long long>(mask(process)),processResult==0?0:errno,affinity);
    return text;
#endif
}
bool flexibleMemory(uint64_t& configured,uint64_t& available) noexcept {
#if defined(__linux__)
    configured=available=0;return false;
#else
    size_t total=0,free=0;
    if(sceKernelConfiguredFlexibleMemorySize(&total)<0||sceKernelAvailableFlexibleMemorySize(&free)<0)return false;
    configured=total;available=free;return true;
#endif
}
}
