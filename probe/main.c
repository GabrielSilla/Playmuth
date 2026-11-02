// Azimuth_PS5 viability probe: the platform facts a native host needs before
// any engine code is mapped. Each test reports what it saw; none is assumed.
// Builds as a PS5 payload (ps5-payload-sdk) or as a Linux x86-64 program, to
// check the probe itself on a PC.
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "msabi.h"

#if defined(__linux__)
#include <asm/prctl.h>
#define PLATFORM "linux"
#define REPORT_DIR "."
#else
#define PLATFORM "ps5"
#define REPORT_DIR "/data/azimuth-ps5"
#ifndef SYS_sysarch
#define SYS_sysarch 165
#endif
#define AMD64_SET_GSBASE 131
typedef struct {char reserved[45];char message[3075];} notify_request_t;
int sceKernelSendNotificationRequest(int device,notify_request_t* request,size_t size,int blocking);
#endif

static FILE* report;
static int passed,failed;

static void note(const char* format,...) {
    char line[512];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
    printf("%s\n",line);fflush(stdout);
    if(report){fprintf(report,"%s\n",line);fflush(report);}
}
static void verdict(const char* test,int ok,const char* detail) {
    if(ok)++passed;else ++failed;
    note("[%s] %s: %s",ok?"PASSOU":"FALHOU",test,detail);
}

// --- 1. Executable memory and Windows x64 calls ---------------------------
// Generated code that calls the host the way the engine does: arguments in
// rcx/rdx/r8/r9 and on the stack above 32 bytes of shadow space. The host
// function (msabi.c) calls back into this System V code.
int64_t probeCombine(int64_t a,int64_t b,int64_t c,int64_t d,int64_t e,int64_t f) {
    return a+2*b+3*c+4*d+5*e+6*f;
}
static void testExecutableAndMsAbi(void) {
    static const unsigned char head[]={
        0x48,0x83,0xEC,0x38,                               // sub rsp,0x38
        0x48,0xC7,0x44,0x24,0x20,0x05,0x00,0x00,0x00,       // mov qword [rsp+0x20],5
        0x48,0xC7,0x44,0x24,0x28,0x06,0x00,0x00,0x00,       // mov qword [rsp+0x28],6
        0xB9,0x01,0x00,0x00,0x00,                           // mov ecx,1
        0xBA,0x02,0x00,0x00,0x00,                           // mov edx,2
        0x41,0xB8,0x03,0x00,0x00,0x00,                      // mov r8d,3
        0x41,0xB9,0x04,0x00,0x00,0x00,                      // mov r9d,4
        0x48,0xB8};                                         // mov rax,imm64
    static const unsigned char tail[]={
        0xFF,0xD0,                                          // call rax
        0x48,0x83,0xC4,0x38,                                // add rsp,0x38
        0xC3};                                              // ret
    char detail[256];
    unsigned char* code=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(code==MAP_FAILED){snprintf(detail,sizeof detail,"mmap RW falhou, errno %d",errno);verdict("memoria executavel",0,detail);return;}
    uint64_t target=(uint64_t)(uintptr_t)msabiHostSum();size_t n=0;
    memcpy(code+n,head,sizeof head);n+=sizeof head;
    memcpy(code+n,&target,8);n+=8;
    memcpy(code+n,tail,sizeof tail);n+=sizeof tail;
    if(mprotect(code,4096,PROT_READ|PROT_EXEC)) {
        snprintf(detail,sizeof detail,"mprotect RW->RX falhou, errno %d",errno);verdict("memoria executavel",0,detail);
        munmap(code,4096);return;
    }
    verdict("memoria executavel",1,"pagina anonima RW->RX aceita");
    int64_t result=msabiCallGuest(code);
    snprintf(detail,sizeof detail,"codigo gerado chamou o host com 6 argumentos: %lld (esperado 91)",(long long)result);
    verdict("chamada ms_abi",result==91,detail);
    munmap(code,4096);
    // Reported only: the engine's mapper never needs writable and executable at once.
    void* rwx=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);
    note("  (informativo) mmap RWX direto: %s",rwx==MAP_FAILED?"recusado":"aceito");
    if(rwx!=MAP_FAILED)munmap(rwx,4096);
}

// --- 2. A Windows TEB per thread through gs --------------------------------
static int setGsBase(void* base) {
#if defined(__linux__)
    return syscall(SYS_arch_prctl,ARCH_SET_GS,(unsigned long)(uintptr_t)base)==0?0:errno;
#else
    void* value=base;
    return syscall(SYS_sysarch,AMD64_SET_GSBASE,&value)==0?0:errno;
#endif
}
static inline uint64_t gsRead(uint64_t offset) {
    uint64_t value;__asm__ volatile("movq %%gs:(%1), %0":"=r"(value):"r"(offset));return value;
}
enum {TebThreads=4,TebIterations=200000};
struct TebRun {uint64_t teb[0x200];int error;long mismatches;};
static void* tebThread(void* context) {
    struct TebRun* run=context;
    run->teb[0x30/8]=(uint64_t)(uintptr_t)run->teb;           // NT_TIB.Self
    run->teb[0x58/8]=(uint64_t)(uintptr_t)&run->teb[0x100];   // ThreadLocalStoragePointer
    run->error=setGsBase(run->teb);
    if(run->error)return NULL;
    for(long i=0;i<TebIterations;++i) {
        if(gsRead(0x30)!=run->teb[0x30/8]||gsRead(0x58)!=run->teb[0x58/8])++run->mismatches;
        // Context switches and libc calls must leave gs alone.
        if(i%4096==0){char* p=malloc(64);snprintf(p,64,"%ld",i);free(p);usleep(50);}
        else if(i%256==0)sched_yield();
    }
    return NULL;
}
static void testTeb(void) {
    static struct TebRun runs[TebThreads];pthread_t threads[TebThreads];char detail[256];
    int started=0;
    for(int t=0;t<TebThreads;++t)if(pthread_create(&threads[t],NULL,tebThread,&runs[t])==0)++started;
    for(int t=0;t<started;++t)pthread_join(threads[t],NULL);
    if(started!=TebThreads){snprintf(detail,sizeof detail,"so %d de %d threads criadas",started,TebThreads);verdict("TEB pelo gs",0,detail);return;}
    long mismatches=0;int error=0;
    for(int t=0;t<TebThreads;++t){mismatches+=runs[t].mismatches;if(runs[t].error)error=runs[t].error;}
    if(error){snprintf(detail,sizeof detail,"definir a base do gs falhou, errno %d",error);verdict("TEB pelo gs",0,detail);return;}
    snprintf(detail,sizeof detail,"%d threads, %d leituras cada de gs:[0x30] e gs:[0x58], %ld divergencias, com trocas de contexto e libc",
             TebThreads,TebIterations,mismatches);
    verdict("TEB pelo gs",mismatches==0,detail);
}

// --- 3. Reservations at fixed addresses ------------------------------------
// The engine's preferred base, and the 64 GiB range the game wrote to under
// Wine (0x10_07E8_0004).
static void testFixed(const char* name,uint64_t address,uint64_t size) {
    char detail[256];
    void* got=mmap((void*)(uintptr_t)address,size,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(got==MAP_FAILED){snprintf(detail,sizeof detail,"reserva de 0x%llx bytes em 0x%llx falhou, errno %d",(unsigned long long)size,(unsigned long long)address,errno);verdict(name,0,detail);return;}
    if((uint64_t)(uintptr_t)got!=address) {
        snprintf(detail,sizeof detail,"pedido 0x%llx, sistema deu 0x%llx",(unsigned long long)address,(unsigned long long)(uintptr_t)got);
        verdict(name,0,detail);munmap(got,size);return;
    }
    // Commit one page in the middle, as a later VirtualAlloc(MEM_COMMIT) would.
    unsigned char* page=(unsigned char*)got+size/2;
    int ok=mprotect(page,4096,PROT_READ|PROT_WRITE)==0;
    if(ok){page[4]=0x5a;ok=page[4]==0x5a;}
    snprintf(detail,sizeof detail,"0x%llx bytes em 0x%llx; pagina em 0x%llx %s",(unsigned long long)size,(unsigned long long)address,
             (unsigned long long)(uintptr_t)page,ok?"gravada e lida":"nao gravavel");
    verdict(name,ok,detail);
    munmap(got,size);
}

// --- 4. Memory available for the title -------------------------------------
// FH2 had about 5 GiB on the Xbox One. Commits and touches 256 MiB blocks up
// to 6 GiB and reports how far it got; everything is released afterwards.
// A payload runs inside a system process: on the console this took the
// system's own memory and froze the whole UI for about 70 s (26/10/2026), so
// it runs only when REPORT_DIR/memory-test exists.
static void testMemory(void) {
    if(access(REPORT_DIR "/memory-test",F_OK)) {
        note("[PULADO] memoria para o titulo: crie %s/memory-test para rodar (trava a interface do PS5 por cerca de 70 s)",REPORT_DIR);
        return;
    }
    enum {Block=256u<<20,Blocks=24};
    void* blocks[Blocks];int got=0;char detail[256];
    struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
    for(;got<Blocks;++got) {
        void* p=mmap(NULL,Block,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
        if(p==MAP_FAILED)break;
        for(size_t at=0;at<Block;at+=4096)((volatile unsigned char*)p)[at]=1;
        blocks[got]=p;
        note("  memoria: %d MiB comprometidos e tocados",(got+1)*256);
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    for(int i=0;i<got;++i)munmap(blocks[i],Block);
    long ms=(end.tv_sec-start.tv_sec)*1000+(end.tv_nsec-start.tv_nsec)/1000000;
    snprintf(detail,sizeof detail,"%d MiB comprometidos e tocados em %ld ms (meta: 5120 MiB)",got*256,ms);
    verdict("memoria para o titulo",got*256>=5120,detail);
}

int main(void) {
    mkdir(REPORT_DIR,0777);
    report=fopen(REPORT_DIR "/probe.txt","w");
    note("Azimuth_PS5 probe 0.2 (%s); processadores online: %ld; pid %d",PLATFORM,sysconf(_SC_NPROCESSORS_ONLN),(int)getpid());
    testExecutableAndMsAbi();
    testTeb();
    testFixed("reserva na base do motor",0x140000000ull,64ull<<20);
    testFixed("reserva na faixa dos 64 GiB",0x1000000000ull,4ull<<30);
    testMemory();
    note("Resultado: %d passaram, %d falharam",passed,failed);
#if !defined(__linux__)
    notify_request_t request;memset(&request,0,sizeof request);
    snprintf(request.message,sizeof request.message,"Azimuth_PS5 probe: %d passaram, %d falharam",passed,failed);
    sceKernelSendNotificationRequest(0,&request,sizeof request,0);
#endif
    if(report)fclose(report);
    return failed?1:0;
}
