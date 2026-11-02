// Native-title entry dispatcher. Start with runtime/address checks; opt into GPU or engine.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ucontext.h>
#include "../host/src/Platform.h"

int azimuthHostMain(int, char**);
extern "C" int azimuthGpuMain(void);
// Normally supplied by crtbegin; the payload CRT is deliberately absent here.
extern "C" { void* __dso_handle=&__dso_handle; }
namespace {
int faultFd=-1;
const char* volatile smokeStage="inicio";
void smokeFault(int signal,siginfo_t*,void* context) {
    // Do not use stdio, allocation or locks from a fault handler.
    char line[512];size_t used=0;
    auto text=[&](const char* s){while(*s&&used<sizeof(line)-1)line[used++]=*s++;};
    auto hex=[&](uint64_t value) {
        text("0x");bool leading=true;
        for(int shift=60;shift>=0;shift-=4) {
            unsigned digit=(value>>shift)&15;
            if(digit||!leading||!shift) {leading=false;if(used<sizeof(line)-1)line[used++]="0123456789abcdef"[digit];}
        }
    };
    text("Falha fatal no smoke: sinal=");hex(signal);text(" etapa=");text(smokeStage);
#if defined(AZIMUTH_TITLE)
    // PS5 kernel layout measured by GPU probe 0.5/0.6; SDK headers say +0x10.
    if(context) {
        auto* saved=reinterpret_cast<const mcontext_t*>(static_cast<const char*>(context)+0x40);
        text(" rip=");hex(saved->mc_rip);text(" addr=");hex(saved->mc_addr);
        text(" trap=");hex(saved->mc_trapno);text(" err=");hex(saved->mc_err);
    }
#else
    (void)context;
#endif
    text("\n");
    if(faultFd>=0) {write(faultFd,line,used);fsync(faultFd);}
    _exit(128+signal);
}
bool installSmokeFaults(FILE* out) {
    faultFd=fileno(out);
    struct sigaction action{};sigemptyset(&action.sa_mask);
    action.sa_sigaction=smokeFault;action.sa_flags=SA_SIGINFO;
    for(int signal:{SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGABRT,SIGSYS})
        if(sigaction(signal,&action,nullptr))return false;
    return true;
}
thread_local int tlsValue=17;
bool constructed=false;
bool threadDestructorRan=false;
struct ThreadSentinel {
    bool active=false;
    ~ThreadSentinel(){if(active)threadDestructorRan=true;}
};
thread_local ThreadSentinel threadSentinel;
struct ConstructorCheck { ConstructorCheck(){constructed=true;} } constructorCheck;
void* threadCheck(void* value) {
    int* result=static_cast<int*>(value);
    *result=tlsValue==17?0:1;
    tlsValue=29;
    threadSentinel.active=true;
    return nullptr;
}
int smoke(FILE* out) {
    int failures=0;
    auto step=[&](const char* name) {
        smokeStage=name;
        std::fprintf(out,"Etapa: %s\n",name);std::fflush(out);fsync(fileno(out));
    };
    auto check=[&](bool ok,const char* name) {
        std::fprintf(out,"[%s] %s\n",ok?"OK":"FALHOU",name);
        std::fflush(out);fsync(fileno(out));if(!ok)++failures;
    };
    step("construtores");
    check(constructed,"construtores C++ executados");
    step("excecao C++");
    bool caught=false;
    try {throw std::runtime_error(std::string("Azimuth title exception"));}
    catch(const std::exception& error){caught=std::strcmp(error.what(),"Azimuth title exception")==0;}
    check(caught,"excecao C++ e unwind");
    step("pthread e TLS");
    tlsValue=23;int result=-1;pthread_t thread;
    int error=pthread_create(&thread,nullptr,threadCheck,&result);
    if(!error)error=pthread_join(thread,nullptr);
    check(!error&&result==0&&tlsValue==23,"pthread e TLS do host isolado por thread");
    check(!error&&threadDestructorRan,"destrutor C++ de TLS na saida da thread");
    step("reservas de endereco (sem commit)");
    // Reserve one allocation-granularity block only; do not commit gigabytes on startup.
    for(uintptr_t target:{uintptr_t(0x140000000),uintptr_t(0x1000000000)}) {
        step("mmap pela libkernel");
        std::fprintf(out,"chamando mmap endereco=0x%llx tamanho=65536 prot=PROT_NONE\n",
                     static_cast<unsigned long long>(target));
        std::fflush(out);fsync(fileno(out));
        void* memory=mmap(reinterpret_cast<void*>(target),65536,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
        std::fprintf(out,"reserva solicitada=0x%llx obtida=%p errno=%d\n",
                     static_cast<unsigned long long>(target),memory,memory==MAP_FAILED?errno:0);
        check(memory==reinterpret_cast<void*>(target),"reserva no endereco solicitado");
        if(memory!=MAP_FAILED) {
            step("munmap pela libkernel");
            if(munmap(memory,65536)) {++failures;std::fprintf(out,"munmap falhou: errno=%d\n",errno);}
        }
    }
    return failures?1:0;
}
// The title base does not support leaving main: its _start then calls exit(),
// which the system refuses (klog of 0.16, smoke and gpu alike: SIGSYS/SIGILL
// after "exit() exit_value=0", reported as an app crash). Its documentation
// keeps main alive; the system ends the process when the title is closed.
[[noreturn]] void waitForClose() {
    for(;;)sleep(3600);
}
}
int main() {
    mkdir("/data/azimuth-ps5",0777);
    FILE* out=std::fopen("/data/azimuth-ps5/title.txt","w");
    if(!out)waitForClose();
    // Without a title-mode file the title runs the game (1.52: the public
    // release must work as installed); smoke, gpu and d3d12 stay for diagnosis.
    char mode[32]="host";
    if(FILE* input=std::fopen("/data/azimuth-ps5/title-mode","r")) {
        if(std::fscanf(input,"%31s",mode)!=1)std::strcpy(mode,"host");
        std::fclose(input);
    }
    std::fprintf(out,"Playmuth title 1.53, modo=%s, main=%p, pid=%d\n",mode,
                 reinterpret_cast<void*>(main),static_cast<int>(getpid()));
    std::fflush(out);fsync(fileno(out));
    int status=1;
    if(!std::strcmp(mode,"smoke")) {
        if(installSmokeFaults(out))status=smoke(out);
        else std::fprintf(out,"Nao foi possivel instalar o tratador de falhas: errno=%d\n",errno);
    }
    else if(!std::strcmp(mode,"gpu"))status=azimuthGpuMain();
    else if(!std::strcmp(mode,"host")) {
        // Engine path comes from the usual search, never from title-launch arguments.
        char program[]="azimuth-title";char* argv[]={program,nullptr};
        status=azimuthHostMain(1,argv);
    }else if(!std::strcmp(mode,"d3d12")) {
        // The host without the engine: vkd3d-proton's D3D12 over the host's
        // vulkan-1.dll and RADV (report in host.txt).
        char program[]="azimuth-title";char flag[]="--d3d12";char* argv[]={program,flag,nullptr};
        status=azimuthHostMain(2,argv);
    }else std::fprintf(out,"Modo desconhecido; use smoke, gpu, host ou d3d12\n");
    std::fprintf(out,"Fim do titulo: codigo=%d\n",status);
    std::fprintf(out,"Titulo aguardando ser fechado pelo menu do sistema\n");
    std::fflush(out);fsync(fileno(out));std::fclose(out);
    if(!std::strcmp(mode,"smoke"))
        azimuth::platform::notify(status?"Playmuth: diagnostico do titulo falhou; veja title.txt":
                                       "Playmuth: diagnostico do titulo passou; veja title.txt");
    azimuth::platform::notify("Playmuth: teste terminado (codigo "+std::to_string(status)+
                              "); feche o aplicativo pelo menu do PS5");
    waitForClose();
}
