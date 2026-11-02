// Azimuth_PS5 GPU probe (phase 3, step 0): can this project's own process
// reach the PS5's GPU through RADV? Playden does it inside a title; the host
// is a payload. The probe loads the PS5 build of RADV (libvulkan.prx, which
// sits on libSceAgc/libSceAgcDriver), creates a Vulkan instance and device,
// and has the GPU fill a buffer that the CPU then reads back. Each step is
// written to the report and synced to disk before the next one, so a step
// that hangs the console still leaves how far it got.
//
// REPORT_DIR/gpu-stop containing "dependencias", "carregado" (RADV mapped,
// its entry not run), "modulo" or "instancia" ends
// the probe after that stage, before anything touches the GPU driver further;
// "falha" ends it with a deliberate fault, to check the fault report;
// "falha-syscall" raises SIGSYS to check that diagnostic path without GPU work;
// "sem-calibracao" skips the calibration faults; "pilha-alternativa" adds
// the calibration with the handler on its own stack.
#if defined(__linux__)
#define _GNU_SOURCE   // REG_RIP
#endif
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdarg.h>
#include <ucontext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <dlfcn.h>
#define PLATFORM "linux"
#define REPORT_DIR "."
#else
#if defined(AZIMUTH_TITLE)
#define PLATFORM "ps5-title"
#else
#define PLATFORM "ps5"
#endif
#define REPORT_DIR "/data/azimuth-ps5"
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
typedef struct {char reserved[45];char message[3075];} notify_request_t;
int sceKernelSendNotificationRequest(int device,notify_request_t* request,size_t size,int blocking);
int sceKernelLoadStartModule(const char* path,size_t argc,const void* argv,uint32_t flags,void* option,int* result);
int sceKernelStopUnloadModule(int module,size_t argc,const void* argv,uint32_t flags,void* option,int* result);
int sceKernelDlsym(int module,const char* name,void** address);
#if defined(AZIMUTH_TITLE)
int sceKernelGetModuleInfoFromAddr(const void* address,uint32_t flags,void* info);
int __cxa_atexit(void (*callback)(void*),void* argument,void* dso);
int sceSysmoduleIsLoadedInternal(unsigned int id);
int sceSysmoduleLoadModuleInternal(unsigned int id);
int sceSysmoduleUnloadModuleInternal(unsigned int id);
static unsigned int ownedSysmodules[3];
static size_t ownedSysmoduleCount;
#endif
#endif

// ---- The few Vulkan declarations the probe needs (Vulkan 1.0 core layouts) ----
typedef int32_t VkResult;
typedef struct VkHandle_T* VkInstance;
typedef struct VkHandle_T* VkPhysicalDevice;
typedef struct VkHandle_T* VkDevice;
typedef struct VkHandle_T* VkQueue;
typedef struct VkHandle_T* VkCommandBuffer;
typedef uint64_t VkBuffer,VkDeviceMemory,VkCommandPool,VkFence;
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance,const char*);
typedef VkResult (*PFN_negotiate)(uint32_t*);
typedef struct {int32_t sType;const void* pNext;const char* appName;uint32_t appVersion;const char* engineName;uint32_t engineVersion;uint32_t apiVersion;} VkApplicationInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;const VkApplicationInfo* app;uint32_t layerCount;const char* const* layers;uint32_t extensionCount;const char* const* extensions;} VkInstanceCreateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;uint32_t family;uint32_t count;const float* priorities;} VkDeviceQueueCreateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;uint32_t queueInfoCount;const VkDeviceQueueCreateInfo* queueInfos;uint32_t layerCount;const char* const* layers;uint32_t extensionCount;const char* const* extensions;const void* features;} VkDeviceCreateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t waitCount;const void* waits;const void* waitStages;uint32_t commandBufferCount;const VkCommandBuffer* commandBuffers;uint32_t signalCount;const void* signals;} VkSubmitInfo;
typedef struct {int32_t sType;const void* pNext;uint64_t size;uint32_t typeIndex;} VkMemoryAllocateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;} VkFenceCreateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t srcAccess;uint32_t dstAccess;} VkMemoryBarrier;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;uint64_t size;uint32_t usage;uint32_t sharing;uint32_t familyCount;const uint32_t* families;} VkBufferCreateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;uint32_t family;} VkCommandPoolCreateInfo;
typedef struct {int32_t sType;const void* pNext;VkCommandPool pool;uint32_t level;uint32_t count;} VkCommandBufferAllocateInfo;
typedef struct {int32_t sType;const void* pNext;uint32_t flags;const void* inheritance;} VkCommandBufferBeginInfo;
typedef struct {uint32_t flags;uint32_t count;uint32_t timestampBits;uint32_t granularity[3];} VkQueueFamilyProperties;
typedef struct {uint64_t size;uint64_t alignment;uint32_t typeBits;} VkMemoryRequirements;
typedef struct {uint32_t flags;uint32_t heap;} VkMemoryType;
typedef struct {uint64_t size;uint32_t flags;} VkMemoryHeap;
typedef struct {uint32_t typeCount;VkMemoryType types[32];uint32_t heapCount;VkMemoryHeap heaps[16];} VkPhysicalDeviceMemoryProperties;
_Static_assert(sizeof(VkQueueFamilyProperties)==24,"layout");
_Static_assert(sizeof(VkPhysicalDeviceMemoryProperties)==520,"layout");
_Static_assert(sizeof(VkInstanceCreateInfo)==64&&sizeof(VkDeviceCreateInfo)==72&&sizeof(VkSubmitInfo)==72,"layout");
// VkPhysicalDeviceProperties is read by offset: apiVersion 0, driverVersion 4,
// vendorID 8, deviceID 12, deviceType 16, deviceName[256] 20.
enum {PropertiesSize=1024};

static FILE* report;
static int reportFd=-1;
static struct timespec started;
static double elapsed(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (now.tv_sec-started.tv_sec)*1e3+(now.tv_nsec-started.tv_nsec)/1e6;
}
static pthread_mutex_t noteLock=PTHREAD_MUTEX_INITIALIZER;
static void note(const char* format,...) {
    char line[768];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
    pthread_mutex_lock(&noteLock);
    printf("%s\n",line);fflush(stdout);
    if(report){fprintf(report,"%8.1f ms  %s\n",elapsed(),line);fflush(report);fsync(reportFd);}
    pthread_mutex_unlock(&noteLock);
}

// The step in progress: a watchdog thread reports it every 2 s, so a step
// that hangs is told apart from one that kills the process. A step stuck
// past its limit ends the process with _exit: a probe left alive holds the
// GPU driver (0.7 hung loading libSceAgcDriver after 0.6 had crashed with
// it loaded), and stopping a payload from outside did not end 0.4.
enum {StageLimitSeconds=30};
static _Atomic(const char*) stage="inicio";
static _Atomic int stageSerial;
static _Atomic int watchdogDone;
static _Atomic int watchdogReady;
static void faultLine(const char* format,...);
static void enter(const char* name){stage=name;++stageSerial;}
static void* watchdog(void* unused) {
    (void)unused;
    faultLine("  [watchdog] thread iniciou\n");
    watchdogReady=1;
    int serial=-1;double since=0;
    for(;;) {
        usleep(2000000);
        if(watchdogDone)return NULL;
        if(serial!=stageSerial){serial=stageSerial;since=elapsed();continue;}
        double seconds=(elapsed()-since)/1e3+2;
        // Do not take noteLock: a driver call or fault must not prevent
        // the independent heartbeat from reaching the file descriptor.
        faultLine("  [watchdog] ainda em \"%s\" ha %.0f s\n",stage,seconds);
        if(seconds>=StageLimitSeconds) {
            faultLine("Tempo esgotado (%d s) em \"%s\": encerrando o processo com _exit\n",StageLimitSeconds,stage);
            _exit(4);
        }
    }
    return NULL;
}
// The process's modules and their segments, to name the module and offset
// of a faulting address (PS5: sceKernelGetModuleList/Info).
typedef struct {char name[64];uint64_t start[4];uint64_t size[4];int prot[4];int count;} ModuleMap;
enum {MaxModules=96};
static ModuleMap modules[MaxModules];
static int moduleCount;
#if !defined(__linux__)
typedef struct {uint64_t address;uint32_t size;int32_t prot;} SceKernelModuleSegmentInfo;
typedef struct {uint64_t size;char name[256];SceKernelModuleSegmentInfo segments[4];uint32_t segmentCount;uint8_t fingerprint[20];} SceKernelModuleInfo;
_Static_assert(sizeof(SceKernelModuleInfo)==0x160,"SceKernelModuleInfo");
int sceKernelGetModuleList(int* handles,size_t count,size_t* actual);
int sceKernelGetModuleInfo(int handle,SceKernelModuleInfo* info);
#endif
// The payload's module list shows only eboot.bin (0.6), so the modules this
// probe loads are added by id.
static int knownHandles[8];
static size_t knownCount;
static void knowModule(int handle){if(knownCount<8)knownHandles[knownCount++]=handle;}
static int keptModule=-1;   // a started module unloadModules leaves loaded
static int readModules(void) {
    moduleCount=0;
#if !defined(__linux__)
    static int handles[MaxModules];size_t actual=0;
    int error=sceKernelGetModuleList(handles,MaxModules,&actual);
    if(error)actual=0;
    if(actual>MaxModules-8)actual=MaxModules-8;
    for(size_t k=0;k<knownCount;++k)handles[actual++]=knownHandles[k];
    for(size_t n=0;n<actual;++n) {
        static SceKernelModuleInfo info;memset(&info,0,sizeof info);info.size=sizeof info;
        if(sceKernelGetModuleInfo(handles[n],&info))continue;
        ModuleMap* m=&modules[moduleCount++];
        snprintf(m->name,sizeof m->name,"%s",info.name);
        m->count=info.segmentCount>4?4:(int)info.segmentCount;
        for(int s=0;s<m->count;++s){m->start[s]=info.segments[s].address;m->size[s]=info.segments[s].size;m->prot[s]=info.segments[s].prot;}
    }
    return error;
#else
    (void)knowModule;(void)keptModule;
    return 0;
#endif
}
// "modulo+0xoffset" when the address is inside a known module segment.
static const char* where(uint64_t address,char* text,size_t size) {
    text[0]=0;
    for(int n=0;n<moduleCount;++n)for(int s=0;s<modules[n].count;++s)
        if(address>=modules[n].start[s]&&address<modules[n].start[s]+modules[n].size[s]){
            snprintf(text,size," (%s seg%d+0x%llx, modulo+0x%llx)",modules[n].name,s,
                     (unsigned long long)(address-modules[n].start[s]),(unsigned long long)(address-modules[n].start[0]));
            return text;
        }
    return text;
}
static void writeModules(int fd) {
    char line[256];
    for(int n=0;n<moduleCount;++n)for(int s=0;s<modules[n].count;++s) {
        int k=snprintf(line,sizeof line,"    modulo %-28s seg%d 0x%012llx..0x%012llx prot 0x%x\n",modules[n].name,s,
                       (unsigned long long)modules[n].start[s],(unsigned long long)(modules[n].start[s]+modules[n].size[s]),modules[n].prot[s]);
        if(k>0){ssize_t w=write(fd,line,(size_t)k);(void)w;}
    }
}

// A fault anywhere in the process (RADV's start code included) is written
// to the report with the plain write() before the process ends: registers,
// the module map (RADV included, if it got mapped), and the top of the stack
// with each value that points into a module named.
static void faultLine(const char* format,...) {
    char line[320];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof line,format,args);va_end(args);
    if(reportFd>=0&&n>0){ssize_t w=write(reportFd,line,(size_t)(n<(int)sizeof line?n:(int)sizeof line-1));(void)w;fsync(reportFd);}
}
// Calibration: faults at known places (a store to address 0x10, a call
// through a null pointer) are caught and resumed by editing the saved
// context (siglongjmp only if the edit does not take), to see how this
// system reports rip and the fault address before trusting RADV's fault.
// Each step is marked in the report before it runs, so a calibration that
// kills the process still shows where.
static volatile int calibrating;   // the step; 2 is the null call, the others the store
static volatile unsigned long long calibrationRip,calibrationAddress,calibrationSp,calibrationReturn;
static volatile uintptr_t calibrationTarget=0x10;
extern const char calibrationStoreResume[] __attribute__((visibility("hidden")));
static __attribute__((noinline)) void calibrationStore(void) {
    __asm__ volatile("movl $1,(%0)\n\t.globl calibrationStoreResume\n\t.hidden calibrationStoreResume\ncalibrationStoreResume:"
                     ::"r"(calibrationTarget):"memory");
}
static void (*volatile calibrationNull)(void);
static __attribute__((noinline)) void calibrationCall(void){calibrationNull();__asm__ volatile("");}

// The saved registers in the handler's context. On the PS5 the SDK's
// ucontext_t did not match: 0.2 read rip 0 (it was r14) and 0.4's edit of
// mc_rip had no effect (the store faulted again forever). Registers are
// named in this order:
static const char* const names[]={"rip","rsp","rbp","rax","rbx","rcx","rdx","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15","rflags","trapno","err","addr"};
enum {RegisterCount=21,Rip=0,Rsp=1};
#if defined(__linux__)
#define SdkRipOffset (offsetof(ucontext_t,uc_mcontext.gregs)+REG_RIP*sizeof(greg_t))
static unsigned long long* savedRegister(void* context,int which) {
    static const int order[]={REG_RIP,REG_RSP,REG_RBP,REG_RAX,REG_RBX,REG_RCX,REG_RDX,REG_RSI,REG_RDI,REG_R8,REG_R9,REG_R10,REG_R11,REG_R12,REG_R13,REG_R14,REG_R15,REG_EFL,REG_TRAPNO,REG_ERR,REG_CR2};
    return (unsigned long long*)&((ucontext_t*)context)->uc_mcontext.gregs[order[which]];
}
static unsigned long long* findRip(void* context){return savedRegister(context,Rip);}
#else
// FreeBSD amd64 mcontext_t, as qword indexes: rdi 1 ... r15 15, trapno 16
// (low half), addr 17, err 19, rip 20, rflags 22, rsp 23.
static const int mcontextIndex[]={20,23,9,7,8,4,3,2,1,5,6,10,11,12,13,14,15,22,16,19,17};
#define SdkRipOffset (offsetof(ucontext_t,uc_mcontext)+offsetof(mcontext_t,mc_rip))
// 0.5's context dump: the kernel's mcontext sits at +0x40 (the SDK header
// says +0x10), so rip is at +0xe0. Calibration 1 checks it.
enum {KernelMcontext=0x40};
static const long ripOffset=KernelMcontext+(long)offsetof(mcontext_t,mc_rip);
static unsigned long long* savedRegister(void* context,int which) {
    return (unsigned long long*)((char*)context+ripOffset+(mcontextIndex[which]-20)*8);
}
// rip, when it is the store inside calibrationStore.
static unsigned long long* findRip(void* context) {
    unsigned long long* rip=savedRegister(context,Rip);
    return *rip>=(uintptr_t)calibrationStore&&*rip<(uintptr_t)calibrationStoreResume?rip:NULL;
}
#endif
static void dumpContext(void* context) {
    const uint64_t* q=context;
    faultLine("    contexto do sinal em %p (pelos cabecalhos, rip fica em +0x%zx):\n",context,(size_t)SdkRipOffset);
    for(int n=0;n<0x340/8;n+=4)faultLine("    +0x%03x  %016llx %016llx %016llx %016llx\n",n*8,
                                         (unsigned long long)q[n],(unsigned long long)q[n+1],(unsigned long long)q[n+2],(unsigned long long)q[n+3]);
}
static int calibrationEntries;
static sigjmp_buf calibrationJump;
static volatile int calibrationJumpReady;

// A fatal fault never ends the process the system's way (as a crash, with
// the GPU driver loaded): the main thread goes back to main() to unload the
// modules and exit normally (siglongjmp works on the PS5, calibration 0.5);
// any other thread, or a second fault, ends the process with _exit.
static sigjmp_buf recoveryJump;
static volatile int recoveryReady;
static pthread_t mainThread;
static volatile int fatalFaults;
// Keep the essential fault data even if low-level diagnostic writes fail.
// In a title, main-thread faults return immediately; format them back on
// the host frame rather than calling stdio/module APIs from the handler.
static volatile sig_atomic_t capturedFault;
static volatile sig_atomic_t exiting;   // the probe has returned; only exit() remains
static int capturedSignal,capturedCode;
static unsigned long long capturedAddress,capturedRegisters[RegisterCount];
static void leaveFault(void) {
    if(recoveryReady&&pthread_equal(pthread_self(),mainThread)) {
        recoveryReady=0;
        faultLine("    voltando ao probe para descarregar os modulos e sair normalmente\n");
        siglongjmp(recoveryJump,1);
    }
    faultLine("    encerrando o processo com _exit\n");
    _exit(3);
}

static void onFault(int number,siginfo_t* info,void* context) {
    if(calibrating) {
        int entry=++calibrationEntries;
        faultLine("    [tratador] sinal %d (codigo %d) na calibracao %d, entrada %d, endereco %p\n",number,info->si_code,calibrating,entry,info->si_addr);
        calibrationAddress=(unsigned long long)(uintptr_t)info->si_addr;
        if(entry==1&&calibrating==1)dumpContext(context);
        if(entry>2) {
            // The context edit did not take: leave by longjmp instead.
            faultLine("    [tratador] a edicao do contexto nao teve efeito; saindo por siglongjmp\n");
            if(calibrationJumpReady)siglongjmp(calibrationJump,1);
            _exit(3);
        }
        unsigned long long* rip=calibrating==2?savedRegister(context,Rip):findRip(context);
        if(!rip) {
            faultLine("    [tratador] rip de calibrationStore nao esta no contexto+0x%lx; saindo por siglongjmp\n",(long)((char*)savedRegister(context,Rip)-(char*)context));
            if(calibrationJumpReady)siglongjmp(calibrationJump,1);
            _exit(3);
        }
        unsigned long long* sp=savedRegister(context,Rsp);
        calibrationRip=*rip;calibrationSp=*sp;
        faultLine("    [tratador] rip 0x%llx em contexto+0x%lx, rsp 0x%llx em contexto+0x%lx\n",
                  calibrationRip,(long)((char*)rip-(char*)context),calibrationSp,(long)((char*)sp-(char*)context));
        // Store: resume after the store. Null call: return to the caller.
        if(calibrating==2){calibrationReturn=*(const uint64_t*)(uintptr_t)calibrationSp;*rip=calibrationReturn;*sp+=8;}
        else *rip=(uintptr_t)calibrationStoreResume;
        return;
    }
    // Title 0.15: after "exit() exit_value=0" the main thread still took a
    // signal; this handler called _exit(3), which returned while the process
    // was exiting, and the ud2 after it raised SIGILL. Past the probe's end,
    // record the signal and give it back to the system's default action.
    if(exiting) {
        unsigned long long rip=*savedRegister(context,Rip),rsp=*savedRegister(context,Rsp);
        char place[160];
        faultLine("%8.1f ms  SINAL NA SAIDA: sinal %d (codigo %d), rip 0x%llx%s, endereco %p, rsp 0x%llx, pid %d, enviado por pid %d\n",
                  elapsed(),number,info->si_code,rip,where(rip,place,sizeof place),info->si_addr,rsp,(int)getpid(),(int)info->si_pid);
        const uint64_t* stack=(const uint64_t*)(uintptr_t)(rsp&~7ull);
        for(int n=0;stack&&n<16;++n)faultLine("    [rsp+0x%03x] 0x%016llx%s\n",n*8,(unsigned long long)stack[n],where(stack[n],place,sizeof place));
        faultLine("    sinal %d devolvido ao comportamento padrao\n",number);
        if(reportFd>=0)fsync(reportFd);
        signal(number,SIG_DFL);
        return;
    }
    if(++fatalFaults>1) {
        faultLine("%8.1f ms  segunda falha fatal (sinal %d, endereco %p) durante \"%s\": encerrando o processo com _exit\n",
                  elapsed(),number,info->si_addr,stage);
        _exit(3);
    }
    unsigned long long r[RegisterCount];
    for(int n=0;n<RegisterCount;++n)r[n]=*savedRegister(context,n);
    r[18]&=0xffffffffull;   // trapno shares its qword with fs and gs
    capturedSignal=number;capturedCode=info->si_code;
    capturedAddress=(unsigned long long)(uintptr_t)info->si_addr;
    for(int n=0;n<RegisterCount;++n)capturedRegisters[n]=r[n];
    capturedFault=1;
#if defined(AZIMUTH_TITLE)
    if(recoveryReady&&pthread_equal(pthread_self(),mainThread)) {
        recoveryReady=0;
        siglongjmp(recoveryJump,1);
    }
#endif
    faultLine("%8.1f ms  FALHA FATAL: sinal %d (codigo %d) em rip 0x%llx, endereco 0x%llx, durante \"%s\"\n",
              elapsed(),number,info->si_code,r[0],(unsigned long long)(uintptr_t)info->si_addr,stage);
    char place[160];
    for(int n=0;n<21;++n)faultLine("    %-6s 0x%016llx%s\n",names[n],r[n],n<17?where(r[n],place,sizeof place):"");
    dumpContext(context);
    faultLine("    mapa de modulos no momento da falha:\n");
    readModules();
    if(reportFd>=0){writeModules(reportFd);fsync(reportFd);}
    for(int n=0;n<21;++n)if(n<17&&where(r[n],place,sizeof place)[0])faultLine("    %-6s 0x%016llx%s\n",names[n],r[n],place);
    // The stack last: a bad rsp faults again, and that ends the process.
    const uint64_t* stack=(const uint64_t*)(uintptr_t)(r[1]&~7ull);
    faultLine("    pilha a partir de rsp:\n");
    for(int n=0;stack&&n<64;++n)faultLine("    [rsp+0x%03x] 0x%016llx%s\n",n*8,(unsigned long long)stack[n],where(stack[n],place,sizeof place));
    faultLine("    fim do relatorio da falha\n");
    leaveFault();
}
// alternateStack: run the handler on its own stack, so a stack overflow is
// reported too (calibration 3 checks that this works here first).
static void installFaultHandlers(int alternateStack) {
    static char alternate[256*1024];
    if(alternateStack){stack_t altstack={0};altstack.ss_sp=alternate;altstack.ss_size=sizeof alternate;
        if(sigaltstack(&altstack,NULL))note("sigaltstack falhou: errno %d",errno);}
    struct sigaction action;memset(&action,0,sizeof action);
    action.sa_sigaction=onFault;action.sa_flags=SA_SIGINFO|(alternateStack?SA_ONSTACK:0);sigemptyset(&action.sa_mask);
    int signals[]={SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGABRT,SIGTRAP,SIGSYS};
    for(size_t n=0;n<sizeof signals/sizeof*signals;++n)
        if(sigaction(signals[n],&action,NULL))note("sigaction para sinal %d falhou: errno %d",signals[n],errno);
}
static void calibrationStep(int step,const char* what,void (*fault)(void)) {
    note("Calibracao %d: %s",step,what);
    calibrationRip=calibrationAddress=calibrationSp=calibrationReturn=0;
    calibrationEntries=0;
    calibrating=step;
    int jumped=sigsetjmp(calibrationJump,1);
    if(!jumped){calibrationJumpReady=1;fault();}
    calibrationJumpReady=0;
    calibrating=0;
    note("  voltou%s: rip 0x%llx, endereco 0x%llx, rsp 0x%llx",jumped?" por siglongjmp":" pela edicao do contexto",calibrationRip,calibrationAddress,calibrationSp);
#if !defined(__linux__)
    note("  rip fica em contexto+0x%lx (o SDK diz +0x%zx)",ripOffset,(size_t)SdkRipOffset);
#endif
    if(step==2)note("  [rsp] 0x%llx (retorno esperado logo depois de calibrationCall=%p)",calibrationReturn,(void*)calibrationCall);
}
// The alternate stack only on request: 0.3, which used it, died in its
// first calibration fault.
static void calibrate(int alternateStack) {
    char what[160];
    snprintf(what,sizeof what,"escrita em 0x10 (calibrationStore=%p, retoma em %p), pilha normal",(void*)calibrationStore,(const void*)calibrationStoreResume);
    calibrationStep(1,what,calibrationStore);
    snprintf(what,sizeof what,"chamada de ponteiro nulo (calibrationCall=%p), pilha normal",(void*)calibrationCall);
    calibrationStep(2,what,calibrationCall);
    if(!alternateStack)return;
    installFaultHandlers(1);
    calibrationStep(3,"escrita em 0x10, tratador na pilha alternativa",calibrationStore);
}
static void notify(const char* text) {
#if !defined(__linux__)
    notify_request_t request;memset(&request,0,sizeof request);
    snprintf(request.message,sizeof request.message,"%s",text);
    sceKernelSendNotificationRequest(0,&request,sizeof request,0);
#else
    (void)text;
#endif
}
static char stopWord[32];
static int stopAfter(const char* stage){return strcmp(stopWord,stage)==0;}

// ---- The RADV module --------------------------------------------------------
static const char* const candidates[]={
    REPORT_DIR "/radv/libvulkan.prx",
    "/data/homebrew/FAKE78001/win/wine/lib/wine/x86_64-unix/libvulkan.prx",
};
static int module=-1;
#if defined(__linux__)
static void* linuxModule;
#endif
#if !defined(__linux__)
int __sys_dynlib_load_prx(const char* path,uint64_t flags,int* handle,uint64_t unknown);
int sceKernelGetModuleInfoInternal(int handle,void* info);
// What the system knows about a loaded module: its segments and, from the
// internal info (PS4 layout, 0x1a8 bytes), the init/fini addresses that
// sceKernelLoadStartModule calls.
static uint64_t describeModule(int handle,const char* label) {
    static SceKernelModuleInfo info;memset(&info,0,sizeof info);info.size=sizeof info;
    int error=sceKernelGetModuleInfo(handle,&info);
    uint64_t base=0;
    if(error)note("  %s: sceKernelGetModuleInfo(%d) falhou: 0x%08x",label,handle,(unsigned)error);
    else {
        note("  %s: id %d, nome \"%.64s\", %u segmento(s)",label,handle,info.name,info.segmentCount);
        for(uint32_t s=0;s<info.segmentCount&&s<4;++s)
            note("    seg%u 0x%012llx..0x%012llx prot 0x%x",s,(unsigned long long)info.segments[s].address,
                 (unsigned long long)(info.segments[s].address+info.segments[s].size),info.segments[s].prot);
        base=info.segments[0].address;
    }
    static uint64_t internal[0x400/8];memset(internal,0,sizeof internal);internal[0]=0x1a8;
    error=sceKernelGetModuleInfoInternal(handle,internal);
    if(error){note("    sceKernelGetModuleInfoInternal falhou: 0x%08x",(unsigned)error);return base;}
    const unsigned char* raw=(const unsigned char*)internal;
    uint64_t init,fini,tlsInit,ehHeader;uint32_t tlsIndex,tlsSize;
    memcpy(&init,raw+0x128,8);memcpy(&fini,raw+0x130,8);memcpy(&tlsInit,raw+0x110,8);memcpy(&ehHeader,raw+0x148,8);
    memcpy(&tlsIndex,raw+0x10c,4);memcpy(&tlsSize,raw+0x11c,4);
    note("    interno: init 0x%llx (modulo+0x%llx), fini 0x%llx, tls indice %u tamanho %u em 0x%llx, eh_frame_hdr 0x%llx",
         (unsigned long long)init,(unsigned long long)(init-base),(unsigned long long)fini,tlsIndex,tlsSize,
         (unsigned long long)tlsInit,(unsigned long long)ehHeader);
    for(int n=0;n<0x1a8/8;n+=4)note("    interno+0x%03x  %016llx %016llx %016llx %016llx",n*8,
                                     (unsigned long long)internal[n],(unsigned long long)internal[n+1],(unsigned long long)internal[n+2],(unsigned long long)internal[n+3]);
    return base;
}
#if defined(AZIMUTH_TITLE)
// The public module list omits system modules. Flags=1 on FromAddr also
// finds those modules; the extended info has the same 0x1a8-byte layout.
static void describeAddress(uint64_t address,const char* label) {
    uint64_t info[0x400/8]={0};info[0]=0x1a8;
    int error=sceKernelGetModuleInfoFromAddr((const void*)(uintptr_t)address,1,info);
    if(error){note("  modulo de %s 0x%llx: consulta retornou 0x%08x",label,(unsigned long long)address,(unsigned)error);return;}
    char name[257];memcpy(name,(const char*)info+8,256);name[256]=0;
    uint64_t base;memcpy(&base,(const char*)info+0x160,8);
    note("  modulo de %s: %s base 0x%llx, endereco 0x%llx, offset 0x%llx",label,name,
         (unsigned long long)base,(unsigned long long)address,(unsigned long long)(address-base));
}
#endif
#endif
// PS5: load (map and relocate) without the system's start sequence, which
// faulted on 0.6 jumping to an unmapped address from inside libkernel; then
// describe the module, and run RADV's own entry, which runs its INIT_ARRAY.
// The entry offset is that of the known Playden build (ELF e_entry 0x4ef0).
enum {PlaydenRadvSize=24941932,PlaydenRadvEntry=0x4ef0,
      PlaydenRadvInitFlag=0x17f3278,PlaydenRadvOpcodeTables=0x17fa088};
static int loadModule(const char* path,long long size) {
#if defined(__linux__)
    (void)size;
    linuxModule=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!linuxModule){note("dlopen falhou: %s",dlerror());return -1;}
    return 1;
#else
    int handle=-1;
#if defined(AZIMUTH_TITLE)
    // A raw dynlib mapping is not a registered native runtime module:
    // Dlsym/StopUnload returned ESRCH, and its manual entry faulted in the
    // system runtime. Test the title's normal load/start path. Mapping-only
    // diagnostics still use the raw path and never execute the entry.
    if(!stopAfter("carregado")) {
        note("Titulo: carregamento e inicializacao pelo runtime do sistema");
        note("Ancora de simbolos: getenv=%p malloc=%p __cxa_atexit=%p sceKernelDlsym=%p",
             (void*)getenv,(void*)malloc,(void*)__cxa_atexit,(void*)sceKernelDlsym);
        enter("sceKernelLoadStartModule do RADV (titulo)");
        int result=0;
        handle=sceKernelLoadStartModule(path,0,NULL,0,NULL,&result);
        note("sceKernelLoadStartModule do RADV: 0x%08x, id %d, resultado da entrada %d",(unsigned)handle,handle,result);
        if(handle<0)return -1;
        knowModule(handle);keptModule=handle;readModules();uint64_t base=describeModule(handle,"RADV pelo runtime");
        if(result){note("Entrada do RADV retornou falha: %d",result);return -1;}
        // The start above runs the module's init, which for this build is
        // module+0x0 (DT_INIT is 0): "xor eax,eax; ret". RADV's INIT_ARRAY
        // runs from e_entry, guarded by a flag that INIT_ARRAY[1] also sets.
        // Title 0.13 copied the compute fill shader: every opcode was 0
        // (s_endpgm stored as s_nop), the shape of ACO's instr_info left
        // zero. INIT_ARRAY[4] (+0x4e7660) memcpy's its opcode tables into
        // .bss at +0x17fa088. Run the entry when the flag is still clear.
        if(size==PlaydenRadvSize&&base) {
            volatile const uint32_t* initialised=(const uint32_t*)(uintptr_t)(base+PlaydenRadvInitFlag);
            volatile const uint32_t* opcodes=(const uint32_t*)(uintptr_t)(base+PlaydenRadvOpcodeTables);
            note("Construtores do RADV: flag +0x%x = %u, tabelas de opcodes +0x%x: %08x %08x",
                 PlaydenRadvInitFlag,initialised[0],PlaydenRadvOpcodeTables,opcodes[0],opcodes[1]);
            if(!initialised[0]) {
                int (*entry)(size_t,const void*)=(int (*)(size_t,const void*))(uintptr_t)(base+PlaydenRadvEntry);
                note("INIT_ARRAY nao rodou: chamando a entrada do RADV em %p (modulo+0x%x)",(void*)entry,PlaydenRadvEntry);
                enter("entrada do RADV (INIT_ARRAY)");
                int entryResult=entry(0,NULL);
                note("  entrada retornou %d; flag = %u, tabelas de opcodes: %08x %08x",
                     entryResult,initialised[0],opcodes[0],opcodes[1]);
                if(!initialised[0]){note("A entrada nao marcou os construtores como executados");return -1;}
            }
        } else note("RADV de tamanho desconhecido ou sem base: construtores nao conferidos");
        return handle;
    }
#endif
    enter("__sys_dynlib_load_prx do RADV");
    int error=__sys_dynlib_load_prx(path,0,&handle,0);
    note("__sys_dynlib_load_prx: 0x%08x, id %d",(unsigned)error,handle);
    if(error||handle<0)return -1;
    enter("informacoes do modulo RADV");
    uint64_t base=describeModule(handle,"RADV");
    for(size_t n=0;n<knownCount;++n)describeModule(knownHandles[n],"dependencia");
    knowModule(handle);readModules();
    if(stopAfter("carregado")){note("gpu-stop: parada depois do carregamento, sem rodar a entrada do RADV");return handle;}
    if(size!=PlaydenRadvSize||!base){note("RADV de tamanho desconhecido ou sem base: a entrada nao e conhecida, parando");return -1;}
    int (*entry)(size_t,const void*)=(int (*)(size_t,const void*))(uintptr_t)(base+PlaydenRadvEntry);
    note("Rodando a entrada do RADV em %p (modulo+0x%x): construtores do INIT_ARRAY",(void*)entry,PlaydenRadvEntry);
    enter("entrada do RADV (INIT_ARRAY)");
    int result=entry(0,NULL);
    note("  entrada retornou %d",result);
    return handle;
#endif
}
static void* moduleSymbol(const char* name) {
#if defined(__linux__)
    return dlsym(linuxModule,name);
#else
    void* address=NULL;
    int error=sceKernelDlsym(module,name,&address);
    if(error){note("sceKernelDlsym(%s) falhou: 0x%08x",name,(unsigned)error);return NULL;}
    return address;
#endif
}

static PFN_vkGetInstanceProcAddr getProc;
static VkInstance instance;
#define LOAD(type,name) type name=(type)getProc(instance,#name);if(!name){note("%s ausente",#name);return 0;}

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*,const void*,VkInstance*);
typedef VkResult (*PFN_vkEnumerateInstanceVersion)(uint32_t*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance,uint32_t*,VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice,void*);
typedef void (*PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice,uint32_t*,VkQueueFamilyProperties*);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice,const VkDeviceCreateInfo*,const void*,VkDevice*);
typedef void (*PFN_vkGetDeviceQueue)(VkDevice,uint32_t,uint32_t,VkQueue*);
typedef VkResult (*PFN_vkCreateBuffer)(VkDevice,const VkBufferCreateInfo*,const void*,VkBuffer*);
typedef void (*PFN_vkGetBufferMemoryRequirements)(VkDevice,VkBuffer,VkMemoryRequirements*);
typedef VkResult (*PFN_vkAllocateMemory)(VkDevice,const VkMemoryAllocateInfo*,const void*,VkDeviceMemory*);
typedef VkResult (*PFN_vkBindBufferMemory)(VkDevice,VkBuffer,VkDeviceMemory,uint64_t);
typedef VkResult (*PFN_vkMapMemory)(VkDevice,VkDeviceMemory,uint64_t,uint64_t,uint32_t,void**);
typedef VkResult (*PFN_vkCreateCommandPool)(VkDevice,const VkCommandPoolCreateInfo*,const void*,VkCommandPool*);
typedef VkResult (*PFN_vkAllocateCommandBuffers)(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*);
typedef VkResult (*PFN_vkBeginCommandBuffer)(VkCommandBuffer,const VkCommandBufferBeginInfo*);
typedef void (*PFN_vkCmdFillBuffer)(VkCommandBuffer,VkBuffer,uint64_t,uint64_t,uint32_t);
typedef void (*PFN_vkCmdUpdateBuffer)(VkCommandBuffer,VkBuffer,uint64_t,uint64_t,const void*);
typedef void (*PFN_vkCmdPipelineBarrier)(VkCommandBuffer,uint32_t,uint32_t,uint32_t,uint32_t,const VkMemoryBarrier*,uint32_t,const void*,uint32_t,const void*);
typedef VkResult (*PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (*PFN_vkCreateFence)(VkDevice,const VkFenceCreateInfo*,const void*,VkFence*);
typedef VkResult (*PFN_vkQueueSubmit)(VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
typedef VkResult (*PFN_vkWaitForFences)(VkDevice,uint32_t,const VkFence*,uint32_t,uint64_t);
typedef VkResult (*PFN_vkGetFenceStatus)(VkDevice,VkFence);
typedef void (*PFN_vkDestroyFence)(VkDevice,VkFence,const void*);
typedef void (*PFN_vkDestroyCommandPool)(VkDevice,VkCommandPool,const void*);
typedef void (*PFN_vkUnmapMemory)(VkDevice,VkDeviceMemory);
typedef void (*PFN_vkFreeMemory)(VkDevice,VkDeviceMemory,const void*);
typedef void (*PFN_vkDestroyBuffer)(VkDevice,VkBuffer,const void*);
typedef VkResult (*PFN_vkDeviceWaitIdle)(VkDevice);
typedef void (*PFN_vkDestroyDevice)(VkDevice,const void*);
typedef void (*PFN_vkDestroyInstance)(VkInstance,const void*);

static const char* typeName(uint32_t type) {
    static const char* const names[]={"outro","integrada","dedicada","virtual","CPU"};
    return type<5?names[type]:"?";
}
static char summary[256]="GPU probe: sem resultado";

// Shared by the empty submission and the buffer submission, so the first
// stage isolates queue/fence operation from command execution and memory.
// The stage is entered once: entering it on every poll reset the watchdog,
// which then never reported (title 0.10). The first ten polls are logged
// before and after the call, later ones every 100 ms, so the report tells a
// call that never returns from a process that ends between calls.
static VkResult pollFence(VkDevice device,VkFence fence,PFN_vkGetFenceStatus status) {
    enter("consultas de vkGetFenceStatus");
    note("Consultando vkGetFenceStatus: limite de 2000 ms, intervalo de 1 ms");
    double before=elapsed(),logged=-1e9;unsigned polls=0;
    VkResult r;
    do {
        int detail=polls<10||elapsed()-logged>=100;
        if(detail)note("  consulta %u: chamando em %.1f ms",polls+1,elapsed()-before);
        r=status(device,fence);++polls;
        if(detail){note("  consulta %u: retornou %d em %.1f ms (1 = VK_NOT_READY)",polls,r,elapsed()-before);logged=elapsed();}
        if(r!=1||elapsed()-before>=2000)break;
        int sleepResult=usleep(1000);
        if(sleepResult)note("  pausa depois da consulta %u retornou %d",polls,sleepResult);
    } while(1);
    note("vkGetFenceStatus: %d depois de %u consulta(s), %.1f ms",r,polls,elapsed()-before);
    return r;
}

static int emptySubmitTest(uint32_t family,VkDevice device,int withCommand) {
    LOAD(PFN_vkGetDeviceQueue,vkGetDeviceQueue);LOAD(PFN_vkCreateFence,vkCreateFence);
    LOAD(PFN_vkQueueSubmit,vkQueueSubmit);LOAD(PFN_vkGetFenceStatus,vkGetFenceStatus);
    LOAD(PFN_vkDestroyFence,vkDestroyFence);
    LOAD(PFN_vkCreateCommandPool,vkCreateCommandPool);LOAD(PFN_vkAllocateCommandBuffers,vkAllocateCommandBuffers);
    LOAD(PFN_vkBeginCommandBuffer,vkBeginCommandBuffer);LOAD(PFN_vkEndCommandBuffer,vkEndCommandBuffer);
    LOAD(PFN_vkDestroyCommandPool,vkDestroyCommandPool);
    VkQueue queue=NULL;vkGetDeviceQueue(device,family,0,&queue);
    if(!queue)return 0;
    VkFenceCreateInfo info={8,NULL,0};VkFence fence=0;
    VkResult r=vkCreateFence(device,&info,NULL,&fence);
    note("Fila vazia: vkCreateFence %d, fence 0x%llx",r,(unsigned long long)fence);
    if(r||!fence)return 0;
    enter("status da fence antes do envio vazio");
    r=vkGetFenceStatus(device,fence);note("Fence antes do envio vazio: %d (esperado 1)",r);
    if(r!=1){vkDestroyFence(device,fence,NULL);return 0;}
    VkSubmitInfo submit={4,NULL,0,NULL,NULL,0,NULL,0,NULL};
    VkCommandPool pool=0;VkCommandBuffer command=NULL;
    if(withCommand) {
        enter("gravacao do command buffer vazio");
        VkCommandPoolCreateInfo poolInfo={39,NULL,0,family};
        r=vkCreateCommandPool(device,&poolInfo,NULL,&pool);note("Command buffer vazio: vkCreateCommandPool %d",r);
        if(r){vkDestroyFence(device,fence,NULL);return 0;}
        VkCommandBufferAllocateInfo commandInfo={40,NULL,pool,0,1};
        r=vkAllocateCommandBuffers(device,&commandInfo,&command);note("Command buffer vazio: vkAllocateCommandBuffers %d",r);
        if(!r){
            VkCommandBufferBeginInfo begin={42,NULL,1,NULL};
            r=vkBeginCommandBuffer(command,&begin);note("Command buffer vazio: vkBeginCommandBuffer %d",r);
            if(!r){r=vkEndCommandBuffer(command);note("Command buffer vazio: vkEndCommandBuffer %d",r);}
        }
        if(r){vkDestroyCommandPool(device,pool,NULL);vkDestroyFence(device,fence,NULL);return 0;}
        submit.commandBufferCount=1;submit.commandBuffers=&command;
    }
    enter(withCommand?"vkQueueSubmit command buffer vazio":"vkQueueSubmit sem comandos");
    note("Enviando %s",withCommand?"um command buffer vazio":"fila vazia: nenhum command buffer");
    r=vkQueueSubmit(queue,1,&submit,fence);note("Fila vazia: vkQueueSubmit retornou %d",r);
    if(r){if(pool)vkDestroyCommandPool(device,pool,NULL);vkDestroyFence(device,fence,NULL);return 0;}
    r=pollFence(device,fence,vkGetFenceStatus);
    if(r)return -1;
    vkDestroyFence(device,fence,NULL);
    if(pool)vkDestroyCommandPool(device,pool,NULL);
    note("%s concluido; isso ainda nao valida vkCmdFillBuffer",withCommand?"Command buffer vazio":"Envio sem command buffer");
    return 1;
}

// The GPU writes a host-visible buffer; the CPU checks every word. RADV
// (src/amd/vulkan/meta/radv_meta_buffer.c) picks the GPU path by size on
// an integrated GPU: vkCmdUpdateBuffer below 1024 bytes emits CP WRITE_DATA
// packets, vkCmdFillBuffer below 4096 bytes uses CP DMA, and from 4096
// bytes on it dispatches a compute shader. Titles 0.5-0.10 only ever ran
// the 4096-byte fill, so the three run here from the simplest path up.
enum GpuWrite {CpWriteData,CpDmaFill,ComputeFill};

// Title 0.11's klog: every wave of the compute fill stopped at ILLEGAL_INST,
// PC 0x20004001c, inside the shaders' 32-bit window (high word 2). The CPU
// copy of that window goes to a file, page by page at the page's own offset,
// so the code can be disassembled on the PC. pwrite reads the page in the
// kernel: an unmapped or unreadable one fails with EFAULT instead of faulting
// the probe. On Linux the window is not the shaders' and nothing is mapped.
enum {ShaderWindowPages=1024,PageBytes=4096};
static const uint64_t ShaderWindow=UINT64_C(0x200000000),FaultingPc=UINT64_C(0x20004001c);
static void dumpShaderWindow(void) {
    enter("copia da janela dos shaders");
    int fd=open(REPORT_DIR "/gpu-shader-window.bin",O_RDWR|O_CREAT|O_TRUNC,0666);
    if(fd<0){note("Janela dos shaders: arquivo nao abriu (errno %d)",errno);return;}
    unsigned copied=0;long first=-1;
    for(long page=0;page<=ShaderWindowPages;++page) {
        int ok=page<ShaderWindowPages&&
               pwrite(fd,(const void*)(uintptr_t)(ShaderWindow+(uint64_t)page*PageBytes),PageBytes,(off_t)page*PageBytes)==PageBytes;
        if(ok){copied++;if(first<0)first=page;continue;}
        if(first>=0) {
            note("  janela dos shaders: 0x%llx..0x%llx copiado",(unsigned long long)(ShaderWindow+(uint64_t)first*PageBytes),
                 (unsigned long long)(ShaderWindow+(uint64_t)page*PageBytes));
            first=-1;
        }
    }
    fsync(fd);
    note("Janela dos shaders 0x%llx + %u KiB: %u pagina(s) legiveis pela CPU gravadas em gpu-shader-window.bin",
         (unsigned long long)ShaderWindow,ShaderWindowPages*PageBytes/1024,copied);
    // The dwords around the faulting PC of 0.11, read back from the file.
    uint32_t words[16];
    off_t at=(off_t)(FaultingPc-ShaderWindow)&~(off_t)0x1f;
    if(pread(fd,words,sizeof words,at)==(ssize_t)sizeof words) {
        for(unsigned n=0;n<16;n+=4)
            note("  0x%llx: %08x %08x %08x %08x",(unsigned long long)(ShaderWindow+(uint64_t)at+n*4),
                 words[n],words[n+1],words[n+2],words[n+3]);
    } else note("  PC do 0.11 (0x%llx) fora das paginas copiadas",(unsigned long long)FaultingPc);
    close(fd);
}
static int fillTest(uint32_t family,VkDevice device,const VkPhysicalDeviceMemoryProperties* memory,enum GpuWrite path) {
    LOAD(PFN_vkGetDeviceQueue,vkGetDeviceQueue);LOAD(PFN_vkCreateBuffer,vkCreateBuffer);
    LOAD(PFN_vkGetBufferMemoryRequirements,vkGetBufferMemoryRequirements);LOAD(PFN_vkAllocateMemory,vkAllocateMemory);
    LOAD(PFN_vkBindBufferMemory,vkBindBufferMemory);LOAD(PFN_vkMapMemory,vkMapMemory);LOAD(PFN_vkCreateCommandPool,vkCreateCommandPool);
    LOAD(PFN_vkAllocateCommandBuffers,vkAllocateCommandBuffers);LOAD(PFN_vkBeginCommandBuffer,vkBeginCommandBuffer);
    LOAD(PFN_vkCmdFillBuffer,vkCmdFillBuffer);LOAD(PFN_vkCmdUpdateBuffer,vkCmdUpdateBuffer);
    LOAD(PFN_vkEndCommandBuffer,vkEndCommandBuffer);LOAD(PFN_vkCreateFence,vkCreateFence);
    LOAD(PFN_vkCmdPipelineBarrier,vkCmdPipelineBarrier);
    LOAD(PFN_vkQueueSubmit,vkQueueSubmit);LOAD(PFN_vkGetFenceStatus,vkGetFenceStatus);LOAD(PFN_vkDestroyFence,vkDestroyFence);
    LOAD(PFN_vkDestroyCommandPool,vkDestroyCommandPool);LOAD(PFN_vkUnmapMemory,vkUnmapMemory);LOAD(PFN_vkFreeMemory,vkFreeMemory);
    LOAD(PFN_vkDestroyBuffer,vkDestroyBuffer);
    static const uint32_t sizes[]={64,256,4096};
    static const char* const pathNames[]={"vkCmdUpdateBuffer (CP WRITE_DATA)","vkCmdFillBuffer (CP DMA)","vkCmdFillBuffer (compute shader)"};
    const uint32_t Size=sizes[path],pattern=0xa5c3e10fu;
    // Word n of the update is pattern^n, so a misplaced word is caught too.
    uint32_t data[64/4];for(unsigned n=0;n<64/4;++n)data[n]=pattern^n;
    note("Escrita pela GPU: %s, %u bytes",pathNames[path],Size);
    VkQueue queue=NULL;vkGetDeviceQueue(device,family,0,&queue);
    note("vkGetDeviceQueue: familia %u, fila %p",family,(void*)queue);
    if(!queue){note("vkGetDeviceQueue devolveu fila nula");return 0;}
    VkBufferCreateInfo bufferInfo={12,NULL,0,Size,0x2 /* TRANSFER_DST */,0,0,NULL};
    VkBuffer buffer=0;VkResult r=vkCreateBuffer(device,&bufferInfo,NULL,&buffer);
    if(r){note("vkCreateBuffer: %d",r);return 0;}
    VkMemoryRequirements need;vkGetBufferMemoryRequirements(device,buffer,&need);
    note("Memoria do buffer: tamanho %llu, alinhamento %llu, tipos 0x%08x",
         (unsigned long long)need.size,(unsigned long long)need.alignment,need.typeBits);
    uint32_t type=UINT32_MAX;
    // Prefer GPU-local memory that is also host visible and coherent. The
    // console advertises both (type 3) and non-local host memory (type 2).
    // Previous builds picked type 2 just because it comes first; its fill
    // submissions never completed. Local memory is an experiment, not a
    // confirmed fix. Keep the fallback for devices without a unified type.
    for(unsigned pass=0;pass<2&&type==UINT32_MAX;++pass) {
        uint32_t wanted=pass?0x6:0x7; // HOST_VISIBLE | HOST_COHERENT [+ DEVICE_LOCAL]
        for(uint32_t n=0;n<memory->typeCount;++n)
            if((need.typeBits&(1u<<n))&&(memory->types[n].flags&wanted)==wanted){type=n;break;}
    }
    if(type==UINT32_MAX){note("nenhum tipo de memoria visivel ao host para o buffer");return 0;}
    note("Memoria escolhida: tipo %u, heap %u, flags 0x%x, local da GPU %s",type,
         memory->types[type].heap,memory->types[type].flags,
         memory->types[type].flags&1?"sim":"nao (fallback)");
    VkMemoryAllocateInfo allocation={5,NULL,need.size,type};VkDeviceMemory deviceMemory=0;
    if((r=vkAllocateMemory(device,&allocation,NULL,&deviceMemory))){note("vkAllocateMemory: %d",r);return 0;}
    if((r=vkBindBufferMemory(device,buffer,deviceMemory,0))){note("vkBindBufferMemory: %d",r);return 0;}
    uint32_t* words=NULL;
    if((r=vkMapMemory(device,deviceMemory,0,Size,0,(void**)&words))){note("vkMapMemory: %d",r);return 0;}
    memset(words,0,Size);
    note("Buffer de %u bytes na memoria tipo %u, mapeado em %p",Size,type,(void*)words);
    VkCommandPoolCreateInfo poolInfo={39,NULL,0,family};VkCommandPool pool=0;
    if((r=vkCreateCommandPool(device,&poolInfo,NULL,&pool))){note("vkCreateCommandPool: %d",r);return 0;}
    VkCommandBufferAllocateInfo commandInfo={40,NULL,pool,0,1};VkCommandBuffer command=NULL;
    if((r=vkAllocateCommandBuffers(device,&commandInfo,&command))){note("vkAllocateCommandBuffers: %d",r);return 0;}
    VkCommandBufferBeginInfo begin={42,NULL,0x1 /* ONE_TIME_SUBMIT */,NULL};
    enter("vkBeginCommandBuffer");
    r=vkBeginCommandBuffer(command,&begin);note("vkBeginCommandBuffer: %d",r);
    if(r)return 0;
    enter(pathNames[path]);
    if(path==CpWriteData)vkCmdUpdateBuffer(command,buffer,0,Size,data);
    else vkCmdFillBuffer(command,buffer,0,Size,pattern);
    note("%s gravado",pathNames[path]);
    // RADV compiles and uploads the fill pipeline while recording it.
    if(path==ComputeFill)dumpShaderWindow();
    // Make transfer writes available to host reads after the fence signals.
    // HOST_COHERENT removes flush/invalidate, not the memory dependency.
    enter("barreira transferencia para leitura da CPU");
    VkMemoryBarrier hostRead={46,NULL,0x1000 /* TRANSFER_WRITE */,0x2000 /* HOST_READ */};
    vkCmdPipelineBarrier(command,0x1000 /* TRANSFER */,0x4000 /* HOST */,0,1,&hostRead,0,NULL,0,NULL);
    note("Barreira TRANSFER_WRITE -> HOST_READ gravada");
    enter("vkEndCommandBuffer");
    r=vkEndCommandBuffer(command);note("vkEndCommandBuffer: %d",r);
    if(r)return 0;
    VkFenceCreateInfo fenceInfo={8,NULL,0};VkFence fence=0;
    enter("vkCreateFence");
    r=vkCreateFence(device,&fenceInfo,NULL,&fence);note("vkCreateFence: %d, fence 0x%llx",r,(unsigned long long)fence);
    if(r||!fence)return 0;
    VkSubmitInfo submit={4,NULL,0,NULL,NULL,1,&command,0,NULL};
    enter("vkQueueSubmit");
    note("Chamando vkQueueSubmit: fila %p, command buffer %p, fence 0x%llx",(void*)queue,(void*)command,(unsigned long long)fence);
    double before=elapsed();
    r=vkQueueSubmit(queue,1,&submit,fence);note("vkQueueSubmit retornou %d depois de %.2f ms",r,elapsed()-before);
    if(r)return 0;
    // The title 0.6 console report ends inside vkWaitForFences despite its
    // two-second timeout. Query status instead; never release pending GPU
    // resources or call DeviceWaitIdle after the polling deadline.
    before=elapsed();
    r=pollFence(device,fence,vkGetFenceStatus);
    double after=elapsed();
    if(r==1){
        note("Fence pendente: primeira palavra 0x%08x; encerrando sem liberar recursos em uso",words[0]);
        return -1;
    }
    if(r){note("Erro ao consultar a fence; encerrando sem espera bloqueante");return -1;}
    unsigned wrong=0;
    for(unsigned n=0;n<Size/4;++n)if(words[n]!=(path==CpWriteData?data[n]:pattern))++wrong;
    note("Fence sinalizada em %.2f ms; %u de %u palavras diferentes do esperado (primeira: 0x%08x, esperada 0x%08x)",
         after-before,wrong,Size/4,words[0],pattern);
    enter("limpeza do buffer e da fence");
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkUnmapMemory(device,deviceMemory);
    vkFreeMemory(device,deviceMemory,NULL);vkDestroyBuffer(device,buffer,NULL);
    return wrong==0;
}

static int run(void) {
    enter("pausa antes do RADV");
    note("Antes da pausa de 1 ms, ainda sem carregar RADV");
    double pauseStart=elapsed();int pauseResult=usleep(1000);
    note("Pausa antes do RADV retornou %d em %.2f ms",pauseResult,elapsed()-pauseStart);
    if(pauseResult){snprintf(summary,sizeof summary,"GPU probe: pausa inicial falhou");return 0;}
    const char* path=NULL;struct stat st;
    for(size_t n=0;n<sizeof candidates/sizeof*candidates;++n)if(stat(candidates[n],&st)==0){path=candidates[n];break;}
#if defined(__linux__)
    if(!path)path="libvulkan.so.1";
#endif
    if(!path){note("libvulkan.prx (RADV do PS5) nao encontrado em %s/radv nem na instalacao do Playden",REPORT_DIR);snprintf(summary,sizeof summary,"GPU probe: RADV nao encontrado");return 0;}
#if !defined(__linux__)
#if defined(AZIMUTH_TITLE)
    // Native titles do not share the payload process's filesystem view.
    // IDs from ps5-payload-sdk v0.45 crt/rtld_sprx.c; AGC also matches the
    // native prospero-win path. Let SceSysmodule locate the system modules.
    static const struct {unsigned int id;const char* name;} dependencies[]={
        {0x80000080u,"libSceAgcDriver"},{0x80000094u,"libSceAgc"},{0x80000022u,"libSceVideoOut"}
    };
    for(size_t n=0;n<sizeof dependencies/sizeof*dependencies;++n) {
        unsigned int id=dependencies[n].id;
        const char* name=dependencies[n].name;
        enter(name);
        int before=sceSysmoduleIsLoadedInternal(id);
        note("Etapa 1.%zu: dependencia %s, sysmodule 0x%08x, estado anterior 0x%08x",n+1,name,id,(unsigned)before);
        if(before==0){note("  ja carregada pelo sistema; probe nao vai descarrega-la");continue;}
        int result=sceSysmoduleLoadModuleInternal(id);
        note("  sceSysmoduleLoadModuleInternal: 0x%08x",(unsigned)result);
        if(result) {
            snprintf(summary,sizeof summary,"GPU probe: dependencia %s nao carregou (0x%08x)",name,(unsigned)result);
            return 0;
        }
        ownedSysmodules[ownedSysmoduleCount++]=id;
        int after=sceSysmoduleIsLoadedInternal(id);
        note("  estado depois do carregamento: 0x%08x",(unsigned)after);
        if(after) {
            snprintf(summary,sizeof summary,"GPU probe: %s nao confirmou estado carregado",name);
            return 0;
        }
    }
#else
    // RADV's system dependencies, one at a time, before RADV itself
    // (libScePosixForWebKit is exported by libkernel_web, already loaded).
    static const char* const dependencies[]={"libSceAgcDriver.sprx","libSceAgc.sprx","libSceVideoOut.sprx"};
    for(size_t n=0;n<sizeof dependencies/sizeof*dependencies;++n) {
        char dependency[96];snprintf(dependency,sizeof dependency,"/system/common/lib/%s",dependencies[n]);
        note("Etapa 1.%zu: dependencia do RADV %s",n+1,dependency);
        enter(dependency);
        int result=0,id=sceKernelLoadStartModule(dependency,0,NULL,0,NULL,&result);
        if(id<0)note("  falhou: 0x%08x",(unsigned)id);else{note("  carregada: id %d, resultado do start %d",id,result);knowModule(id);}
    }
#endif
    int error=readModules();
    if(error)note("sceKernelGetModuleList falhou: 0x%08x",(unsigned)error);
    else{note("Modulos do processo antes do RADV: %d",moduleCount);if(reportFd>=0){fflush(report);writeModules(reportFd);fsync(reportFd);}}
    if(stopAfter("dependencias")){note("gpu-stop: parada depois das dependencias");snprintf(summary,sizeof summary,"GPU probe: dependencias carregadas");return 1;}
#endif
    note("Etapa 1: carregar o RADV: %s (%lld bytes)",path,(long long)st.st_size);
    enter("sceKernelLoadStartModule do RADV");
    if((module=loadModule(path,(long long)st.st_size))<0){snprintf(summary,sizeof summary,"GPU probe: RADV nao carregou");return 0;}
    if(stopAfter("carregado")) {
        note("Parada planejada: RADV mapeado; entrada e Vulkan ainda nao executados");
        snprintf(summary,sizeof summary,"GPU probe: RADV mapeado; parada antes da entrada");
        return 2;
    }
    getProc=(PFN_vkGetInstanceProcAddr)moduleSymbol("vkGetInstanceProcAddr");
    PFN_negotiate negotiate=(PFN_negotiate)moduleSymbol("vk_icdNegotiateLoaderICDInterfaceVersion");
    note("vkGetInstanceProcAddr=%p, vk_icdNegotiateLoaderICDInterfaceVersion=%p",(void*)getProc,(void*)negotiate);
    if(!getProc){snprintf(summary,sizeof summary,"GPU probe: RADV sem vkGetInstanceProcAddr");return 0;}
    if(negotiate){uint32_t version=5;VkResult r=negotiate(&version);note("Interface de ICD negociada: versao %u (resultado %d)",version,r);}
    if(stopAfter("modulo")){note("gpu-stop: parada depois do carregamento do modulo");snprintf(summary,sizeof summary,"GPU probe: RADV carregado");return 1;}

    note("Etapa 2: instancia Vulkan e dispositivos fisicos");
    enter("instancia Vulkan");
    PFN_vkEnumerateInstanceVersion enumerateVersion=(PFN_vkEnumerateInstanceVersion)getProc(NULL,"vkEnumerateInstanceVersion");
    uint32_t api=0;if(enumerateVersion)enumerateVersion(&api);
    note("Vulkan da instancia: %u.%u.%u",api>>22,(api>>12)&0x3ff,api&0xfff);
    PFN_vkCreateInstance createInstance=(PFN_vkCreateInstance)getProc(NULL,"vkCreateInstance");
    if(!createInstance){note("vkCreateInstance ausente");return 0;}
    VkApplicationInfo app={0,NULL,"Azimuth_PS5 GPU probe",1,"Azimuth_PS5",1,(1u<<22)|(3u<<12)};
    // Title 0.12 set RADV_DEBUG=metashaders,asm here: RADV read it, then
    // faulted on the CPU in libSceLibcInternal (read of address 0) while
    // dumping the compute fill shader, before any submission. The shader
    // code is read from memory instead (dumpShaderWindow).
    const char* radvDebug=getenv("RADV_DEBUG");
    note("RADV_DEBUG: %s",radvDebug?radvDebug:"nao definido");
    VkInstanceCreateInfo instanceInfo={1,NULL,0,&app,0,NULL,0,NULL};
    VkResult r=createInstance(&instanceInfo,NULL,&instance);
    note("vkCreateInstance: %d",r);
    if(r){snprintf(summary,sizeof summary,"GPU probe: vkCreateInstance %d",r);return 0;}
    LOAD(PFN_vkEnumeratePhysicalDevices,vkEnumeratePhysicalDevices);LOAD(PFN_vkGetPhysicalDeviceProperties,vkGetPhysicalDeviceProperties);
    LOAD(PFN_vkGetPhysicalDeviceMemoryProperties,vkGetPhysicalDeviceMemoryProperties);
    LOAD(PFN_vkGetPhysicalDeviceQueueFamilyProperties,vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(PFN_vkCreateDevice,vkCreateDevice);LOAD(PFN_vkDeviceWaitIdle,vkDeviceWaitIdle);LOAD(PFN_vkDestroyDevice,vkDestroyDevice);
    LOAD(PFN_vkDestroyInstance,vkDestroyInstance);
    uint32_t count=0;r=vkEnumeratePhysicalDevices(instance,&count,NULL);
    note("vkEnumeratePhysicalDevices: %d, %u dispositivo(s)",r,count);
    if(r||!count){snprintf(summary,sizeof summary,"GPU probe: nenhum dispositivo Vulkan");return 0;}
    VkPhysicalDevice physicals[4];if(count>4)count=4;vkEnumeratePhysicalDevices(instance,&count,physicals);
    VkPhysicalDevice physical=physicals[0];
    _Alignas(8) unsigned char properties[PropertiesSize];memset(properties,0,sizeof properties);
    vkGetPhysicalDeviceProperties(physical,properties);
    uint32_t p[5];memcpy(p,properties,sizeof p);
    note("Dispositivo: \"%.255s\" vendor 0x%04x device 0x%04x, %s, Vulkan %u.%u.%u, driver 0x%08x",
         (const char*)properties+20,p[2],p[3],typeName(p[4]),p[0]>>22,(p[0]>>12)&0x3ff,p[0]&0xfff,p[1]);
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    for(uint32_t n=0;n<memory.heapCount&&n<16;++n)note("  heap %u: %llu MiB%s",n,(unsigned long long)(memory.heaps[n].size>>20),(memory.heaps[n].flags&1)?" (local)":"");
    for(uint32_t n=0;n<memory.typeCount&&n<32;++n)note("  tipo de memoria %u: heap %u, flags 0x%x",n,memory.types[n].heap,memory.types[n].flags);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);
    VkQueueFamilyProperties family[8];if(families>8)families=8;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,family);
    uint32_t graphics=UINT32_MAX;
    for(uint32_t n=0;n<families;++n) {
        note("  fila %u: flags 0x%x, %u fila(s)",n,family[n].flags,family[n].count);
        if(graphics==UINT32_MAX&&(family[n].flags&1))graphics=n;
    }
    if(stopAfter("instancia")){note("gpu-stop: parada antes de criar o dispositivo");vkDestroyInstance(instance,NULL);snprintf(summary,sizeof summary,"GPU probe: instancia ok");return 1;}
    if(graphics==UINT32_MAX){note("nenhuma fila grafica");return 0;}

    note("Etapa 3: dispositivo logico (inicializa o driver da GPU pelo libSceAgc)");
    enter("vkCreateDevice");
    float priority=1.f;VkDeviceQueueCreateInfo queueInfo={2,NULL,0,graphics,1,&priority};
    VkDeviceCreateInfo deviceInfo={3,NULL,0,1,&queueInfo,0,NULL,0,NULL,NULL};
    VkDevice device=NULL;r=vkCreateDevice(physical,&deviceInfo,NULL,&device);
    note("vkCreateDevice: %d",r);
    if(r){snprintf(summary,sizeof summary,"GPU probe: vkCreateDevice %d",r);vkDestroyInstance(instance,NULL);return 0;}

    // A loaded module may have changed process-wide handlers. Keep the
    // probe's diagnostics active for submission and any worker threads.
    installFaultHandlers(0);
    note("Etapa 4a: submissao vazia e sinalizacao da fence");
    int emptyOk=emptySubmitTest(graphics,device,0);
    if(emptyOk<0){note("[FALHOU] Fence da fila vazia nao concluiu; _exit(5)");_exit(5);}
    if(!emptyOk){note("[FALHOU] Preparacao da fila vazia falhou; _exit(5)");_exit(5);}
    note("Etapa 4b: execucao de um command buffer vazio");
    emptyOk=emptySubmitTest(graphics,device,1);
    if(emptyOk<=0){note("[FALHOU] Command buffer vazio nao concluiu; _exit(5)");_exit(5);}
    // Simplest GPU write first; a pending fence ends the probe there, so
    // the last stage in the report is the first path that did not complete.
    static const char* const steps[]={"4c","4d","4e"};
    int ok=1;
    for(int path=CpWriteData;path<=ComputeFill&&ok>0;++path) {
        note("Etapa %s: a GPU escreve um buffer e a CPU confere",steps[path]);
        enter("teste de escrita pela GPU");
        ok=fillTest(graphics,device,&memory,(enum GpuWrite)path);
        if(ok<0){
            note("[FALHOU] GPU probe: etapa %s aceita, conclusao nao confirmada; _exit(5)",steps[path]);
            _exit(5);
        }
    }
    enter("vkDeviceWaitIdle");
    note("Chamando vkDeviceWaitIdle para finalizar o teste");
    r=vkDeviceWaitIdle(device);note("vkDeviceWaitIdle retornou %d",r);
    if(r)ok=0;
    enter("vkDestroyDevice");vkDestroyDevice(device,NULL);
    enter("vkDestroyInstance");vkDestroyInstance(instance,NULL);
    snprintf(summary,sizeof summary,ok?"GPU probe: a GPU executou comandos pelo Vulkan":"GPU probe: dispositivo criado, mas o teste de preenchimento falhou");
    return ok;
}

// ---- Not needing a reboot between runs --------------------------------------
// A probe left alive holds the GPU driver, and stopping a payload from
// outside did not end 0.4. REPORT_DIR/gpu-probe.pid holds the pid of the
// running probe; the next run ends it with SIGKILL before touching the GPU,
// if it is still alive under this probe's own process name. Other processes
// with that name (probes from before this file existed) are ended too when
// the name is this project's, and only listed otherwise.
#define PID_FILE REPORT_DIR "/gpu-probe.pid"
static int processInfo(pid_t pid,char* name,size_t size,int* ended) {
#if defined(__linux__)
    char path[64];snprintf(path,sizeof path,"/proc/%d/stat",(int)pid);
    FILE* file=fopen(path,"r");if(!file)return -1;
    char state=0;int got=fscanf(file,"%*d (%31[^)]) %c",name,&state);fclose(file);
    (void)size;*ended=state=='Z'||state=='X';
    return got==2?0:-1;
#else
    int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PID,(int)pid};
    struct kinfo_proc info;memset(&info,0,sizeof info);size_t length=sizeof info;
    if(sysctl(mib,4,&info,&length,NULL,0)!=0||info.ki_structsize!=(int)sizeof info||info.ki_pid!=pid)return -1;
    snprintf(name,size,"%s",info.ki_comm);
    *ended=info.ki_stat==5;   // SZOMB
    return 0;
#endif
}
static int endProcess(pid_t pid,const char* why) {
    note("  %s: pid %d ainda vivo; encerrando com SIGKILL",why,(int)pid);
    if(kill(pid,SIGKILL)!=0){note("    kill falhou: errno %d",errno);return 0;}
    char name[32];int ended=0;
    for(int n=0;n<50;++n) {
        if(processInfo(pid,name,sizeof name,&ended)!=0||ended){note("    encerrado depois de %d ms",n*100);return 1;}
        usleep(100000);
    }
    note("    ainda vivo 5 s depois do SIGKILL (preso dentro do sistema?)");
    return 0;
}
static void endPreviousProbe(void) {
#if defined(AZIMUTH_TITLE)
    // A title's process name may be the generic "eboot.bin", shared with
    // unrelated applications. Never identify another title by that name.
    note("Titulo: encerramento de probes anteriores por nome/pid desativado");
    return;
#endif
    char own[32];int ended=0;
    if(processInfo(getpid(),own,sizeof own,&ended)!=0) {
#if !defined(__linux__)
        note("Nome do proprio processo indisponivel pelo sysctl (kinfo_proc de %zu bytes): sem verificacao de probe anterior",sizeof(struct kinfo_proc));
#endif
        return;
    }
    note("Processo %d \"%s\"",(int)getpid(),own);
    FILE* file=fopen(PID_FILE,"r");int previous=0;
    if(file){if(fscanf(file,"%d",&previous)!=1)previous=0;fclose(file);}
    char name[32];
    if(previous>0&&previous!=getpid()) {
        if(processInfo(previous,name,sizeof name,&ended)!=0||ended)note("Probe anterior (pid %d) ja terminou",previous);
        else if(strcmp(name,own)!=0)note("O pid %d do probe anterior agora e outro processo (\"%s\"): nao foi tocado",previous,name);
        else endProcess(previous,"Probe anterior");
    }
    file=fopen(PID_FILE,"w");
    if(file){fprintf(file,"%d\n",(int)getpid());fclose(file);}
#if !defined(__linux__)
    // Probes from before the pid file: the other processes with this name.
    int mib[3]={CTL_KERN,KERN_PROC,KERN_PROC_PROC};size_t length=0;
    if(sysctl(mib,3,NULL,&length,NULL,0)!=0||!length)return;
    length+=length/4;
    struct kinfo_proc* list=malloc(length);
    if(!list)return;
    if(sysctl(mib,3,list,&length,NULL,0)!=0){free(list);return;}
    int ours=strstr(own,"azimuth")!=NULL;
    for(size_t n=0;n<length/sizeof*list;++n) {
        const struct kinfo_proc* p=&list[n];
        if(p->ki_pid==getpid()||p->ki_pid==previous||strcmp(p->ki_comm,own)!=0||p->ki_stat==5)continue;
        if(ours)endProcess(p->ki_pid,"Outro processo com o nome do probe");
        else note("  outro processo com o mesmo nome: pid %d (nao encerrado: o nome nao e deste projeto)",(int)p->ki_pid);
    }
    free(list);
#endif
}
// Unloads what this probe loaded, newest first (RADV, then its system
// dependencies), so the driver is released by its own stop code and not by
// a dying process.
static void unloadModules(void) {
#if defined(__linux__)
    if(linuxModule){dlclose(linuxModule);linuxModule=NULL;}
#else
    for(size_t n=knownCount;n-->0;) {
        // Title 0.14 passed, then exit() ran an atexit handler that faulted:
        // a started RADV registers handlers (its constructors' __cxa_atexit,
        // Mesa's util_queue atexit) that libc runs at exit, so its code must
        // stay mapped until then. The process exit releases it.
        if(knownHandles[n]==keptModule) {
            note("  modulo %d (RADV iniciado): mantido carregado ate o fim do processo",knownHandles[n]);
            continue;
        }
        enter("descarregando os modulos");
        int result=0,error=sceKernelStopUnloadModule(knownHandles[n],0,NULL,0,NULL,&result);
        note("  modulo %d: sceKernelStopUnloadModule 0x%08x, resultado %d",knownHandles[n],(unsigned)error,result);
    }
    knownCount=0;
#if defined(AZIMUTH_TITLE)
    for(size_t n=ownedSysmoduleCount;n-->0;) {
        enter("descarregando dependencias pelo sysmodule");
        int error=sceSysmoduleUnloadModuleInternal(ownedSysmodules[n]);
        note("  sysmodule 0x%08x: sceSysmoduleUnloadModuleInternal 0x%08x",ownedSysmodules[n],(unsigned)error);
    }
    ownedSysmoduleCount=0;
#endif
#endif
}

int main(void) {
    clock_gettime(CLOCK_MONOTONIC,&started);
    mkdir(REPORT_DIR,0777);
    // A new file, not the old one truncated: a previous probe still alive
    // (0.4 was) keeps writing to its own copy instead of into this report.
    unlink(REPORT_DIR "/gpu-probe.txt");
    report=fopen(REPORT_DIR "/gpu-probe.txt","w");
    // The SDK's C fileno macro may read FILE::_file directly before the
    // first thread. Native libc has its own opaque FILE layout: call the
    // exported function, as the C++ host does, never inspect its fields.
    if(report)reportFd=(fileno)(report);
    // RADV writes its own errors to stderr: they go to a file next to the report.
    int errors=open(REPORT_DIR "/gpu-stderr.txt",O_WRONLY|O_CREAT|O_TRUNC,0666);
    if(errors>=0){dup2(errors,2);close(errors);}
    FILE* stop=fopen(REPORT_DIR "/gpu-stop","r");
    if(stop){if(fscanf(stop,"%31s",stopWord)!=1)stopWord[0]=0;fclose(stop);}
    note("Azimuth_PS5 GPU probe 0.22 (%s)%s%s",PLATFORM,stopWord[0]?", gpu-stop=":"",stopWord);
    note("Descritor do relatorio obtido pela funcao fileno: %d",reportFd);
    faultLine("    diagnostico direto por write: descritor %d\n",reportFd);
    mainThread=pthread_self();
    pthread_t thread;
    int threadError=pthread_create(&thread,NULL,watchdog,NULL);
    note("Criacao do watchdog: pthread_create %d",threadError);
    if(threadError){note("Sem watchdog: teste nao iniciado");if(report)fclose(report);return 1;}
    double readyStart=elapsed();
    while(!watchdogReady&&elapsed()-readyStart<2000)usleep(1000);
    if(!watchdogReady){note("Watchdog nao iniciou em 2 s; teste nao iniciado");_exit(4);}
    note("Watchdog confirmou que a thread esta executando");
    enter("probe anterior");
    endPreviousProbe();
    enter("calibracao");
    installFaultHandlers(0);
    if(stopAfter("sem-calibracao"))note("gpu-stop: sem calibracao (tratador na pilha normal, como no 0.2)");
    else calibrate(stopAfter("pilha-alternativa"));
    volatile int ok=0;
    if(!sigsetjmp(recoveryJump,1)) {
        recoveryReady=1;
        if(stopAfter("falha")){note("gpu-stop: falha de teste (chamada de ponteiro nulo)");enter("falha de teste");readModules();calibrationCall();}
        if(stopAfter("falha-syscall")){note("gpu-stop: teste do tratador de SIGSYS");enter("falha de teste SIGSYS");raise(SIGSYS);}
        ok=run();
    } else {
        if(capturedFault) {
            note("FALHA CAPTURADA: sinal %d (codigo %d), rip 0x%llx, endereco 0x%llx, etapa \"%s\"",
                 capturedSignal,capturedCode,capturedRegisters[Rip],capturedAddress,stage);
#if defined(AZIMUTH_TITLE) && !defined(__linux__)
            describeAddress(capturedRegisters[Rip],"rip");
            describeAddress(capturedRegisters[7],"rsi");
            describeAddress(capturedRegisters[8],"rdi");
            describeAddress(capturedRegisters[6],"rdx");
#endif
            readModules();
            char place[160];
            for(int n=0;n<RegisterCount;++n)
                note("  capturado %-6s 0x%016llx%s",names[n],capturedRegisters[n],
                     n<17?where(capturedRegisters[n],place,sizeof place):"");
            if(reportFd>=0){fflush(report);writeModules(reportFd);fsync(reportFd);}
        }
        note("Falha fatal recuperada em \"%s\": o probe segue para a limpeza",stage);
        snprintf(summary,sizeof summary,"GPU probe: falha fatal em \"%s\"",stage);
    }
    recoveryReady=0;
    note("Limpeza: descarregando os modulos carregados pelo probe");
    unloadModules();
    enter("fim");
    note("%s %s",ok==2?"[PAROU]":ok?"[PASSOU]":"[FALHOU]",summary);
    notify(summary);
    unlink(PID_FILE);
    note("Fim do GPU probe");
    watchdogDone=1;
    threadError=pthread_join(thread,NULL);
    note("Watchdog encerrado: pthread_join %d",threadError);
    // The report stays open to the end of the process: a signal during the
    // title's exit (0.15) is written here by onFault. The exit closes it.
    note("Relatorio mantido aberto; sinais durante a saida do processo serao registrados aqui");
    enter("saida do processo (depois do probe)");
    exiting=1;
    return ok?0:1;
}
