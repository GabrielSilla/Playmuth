// Windows x64 calling convention on the PS5. The SDK's x86_64-sie-ps5 target
// accepts only the C convention, so every function the engine calls and every
// call into the engine's code lives in this file, compiled for
// x86_64-pc-freebsd: same CPU, same System V ABI for everything else. It calls
// only this project's own functions, never a system library, so nothing here
// depends on the PS5's dynamic linking. (Checked on the console by the probe.)
#include "guest_abi.h"

typedef void (__attribute__((ms_abi)) *EntryPoint)(void);
typedef void (__attribute__((ms_abi)) *TlsCallback)(uint64_t image,uint32_t reason,void* reserved);

// Reached by a jump from the stub, so the return address is the engine's call site.
static __attribute__((ms_abi,noreturn)) void missingImport(uint32_t slot) {
    const uint64_t* stack=(const uint64_t*)__builtin_frame_address(0);
    hostMissingImport(slot,(uint64_t)__builtin_return_address(0),stack);
}
const void* guestMissingImportHandler(void){return (const void*)&missingImport;}
void guestCallEntry(const void* entry){((EntryPoint)entry)();}
void guestCallTlsCallback(const void* callback,uint64_t imageBase,uint32_t reason) {
    ((TlsCallback)callback)(imageBase,reason,0);
}
typedef int32_t (__attribute__((ms_abi)) *LocaleEnum)(uint16_t* name);
int32_t guestCallLocaleEnum(const void* callback,uint16_t* name){return ((LocaleEnum)callback)(name);}
typedef uint32_t (__attribute__((ms_abi)) *ThreadStart)(void* parameter);
uint32_t guestCallThreadStart(const void* start,void* parameter){return ((ThreadStart)start)(parameter);}
typedef void (__attribute__((ms_abi)) *FlsCallback)(void* value);
void guestCallFlsCallback(const void* callback,void* value){((FlsCallback)callback)(value);}
typedef void (__attribute__((ms_abi)) *SimpleCallback)(void* instance,void* context);
void guestCallSimpleCallback(const void* callback,void* instance,void* context){((SimpleCallback)callback)(instance,context);}
typedef int64_t (__attribute__((ms_abi)) *WindowProc)(void* window,uint32_t message,uint64_t w,int64_t l);
int64_t guestCallWindowProc(const void* proc,void* window,uint32_t message,uint64_t w,int64_t l){return ((WindowProc)proc)(window,message,w,l);}
typedef void (__attribute__((ms_abi)) *TimerProc)(void* window,uint32_t message,uint64_t id,uint32_t time);
void guestCallTimerProc(const void* proc,void* window,uint32_t message,uint64_t id,uint32_t time){((TimerProc)proc)(window,message,id,time);}
typedef int32_t (__attribute__((ms_abi)) *DllEntry)(uint64_t instance,uint32_t reason,void* reserved);
int32_t guestCallDllMain(const void* entry,uint64_t instance,uint32_t reason,void* reserved){return ((DllEntry)entry)(instance,reason,reserved);}
typedef void (__attribute__((ms_abi)) *VoidFunction)(void);
void guestCallVoid(const void* function){((VoidFunction)function)();}
typedef int32_t (__attribute__((ms_abi)) *CompareFunction)(const void* a,const void* b);
int32_t guestCallCompare(const void* compare,const void* a,const void* b){return ((CompareFunction)compare)(a,b);}
typedef int32_t (__attribute__((ms_abi)) *InitOnceFunction)(void* once,void* parameter,void** context);
int32_t guestCallInitOnce(const void* function,void* once,void* parameter,void** context){return ((InitOnceFunction)function)(once,parameter,context);}
typedef uint64_t (__attribute__((ms_abi)) *Call8)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
uint64_t guestCall8(const void* function,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    return ((Call8)function)(a,b,c,d,e,f,g,h);
}

// One Windows x64 entry point per implemented function, forwarding to the
// host's System V implementation once the gate lets the thread through. The
// cleanup marks the return (title 1.20: it also times the call, for the report
// of where the host's time goes).
#define EXPORT(dll,name,ret,params,args) \
    ret win_##name params; \
    static __attribute__((ms_abi)) ret ms_##name params { \
        hostGateNamed(#name);int leaving __attribute__((cleanup(hostLeave)))=0;(void)leaving; \
        return win_##name args; \
    }
#include "win32/exports.inc"
#undef EXPORT

// RtlCaptureContext(CONTEXT*): the caller's registers as they are at the
// call, with the rsp and rip it will have after the return (x64 CONTEXT
// offsets; FltSave is 16-byte aligned, as CONTEXT is declared). A leaf that
// only reads registers, so it needs no gate and is written in assembly.
__attribute__((naked)) static void captureContext(void) {
    __asm__ volatile(
        "pushfq\n"
        "movq %rax,0x78(%rcx)\n" "movq %rcx,0x80(%rcx)\n" "movq %rdx,0x88(%rcx)\n" "movq %rbx,0x90(%rcx)\n"
        "leaq 16(%rsp),%rax\n" "movq %rax,0x98(%rcx)\n"
        "movq %rbp,0xa0(%rcx)\n" "movq %rsi,0xa8(%rcx)\n" "movq %rdi,0xb0(%rcx)\n"
        "movq %r8,0xb8(%rcx)\n" "movq %r9,0xc0(%rcx)\n" "movq %r10,0xc8(%rcx)\n" "movq %r11,0xd0(%rcx)\n"
        "movq %r12,0xd8(%rcx)\n" "movq %r13,0xe0(%rcx)\n" "movq %r14,0xe8(%rcx)\n" "movq %r15,0xf0(%rcx)\n"
        "movq 8(%rsp),%rax\n" "movq %rax,0xf8(%rcx)\n"
        "movw %cs,0x38(%rcx)\n" "movw %ds,0x3a(%rcx)\n" "movw %es,0x3c(%rcx)\n"
        "movw %fs,0x3e(%rcx)\n" "movw %gs,0x40(%rcx)\n" "movw %ss,0x42(%rcx)\n"
        "popq %rax\n" "movl %eax,0x44(%rcx)\n"
        "movq 0x78(%rcx),%rax\n"
        "stmxcsr 0x34(%rcx)\n"
        "fxsave 0x100(%rcx)\n"
        "movl $0x10000f,0x30(%rcx)\n"   // CONTEXT_FULL | CONTEXT_SEGMENTS
        "ret\n");
}

// CoCreateFreeThreadedMarshaler (asked for by the game's code through
// combase, title 0.83): one shared object that answers only IUnknown. A free-
// threaded marshaler only matters across processes; inside the one process
// the host runs, its caller works the same without IMarshal.
static __attribute__((ms_abi)) int32_t freeThreadedQueryInterface(void* self,const void* iid,void** out) {
    static const uint8_t unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    if(!out)return (int32_t)0x80004003;   // E_POINTER
    if(iid&&!__builtin_memcmp(iid,unknown,16)){*out=self;return 0;}
    *out=0;return (int32_t)0x80004002;   // E_NOINTERFACE
}
static __attribute__((ms_abi)) uint32_t freeThreadedAddRef(void* self){(void)self;return 2;}
static __attribute__((ms_abi)) uint32_t freeThreadedRelease(void* self){(void)self;return 1;}
static const void* const freeThreadedVtable[3]={(const void*)&freeThreadedQueryInterface,(const void*)&freeThreadedAddRef,(const void*)&freeThreadedRelease};
static const struct {const void* const* vtable;} freeThreadedMarshaler={freeThreadedVtable};
static __attribute__((ms_abi)) int32_t ms_CoCreateFreeThreadedMarshaler(void* outer,void** marshal) {
    int leaving __attribute__((cleanup(hostLeave)))=0;(void)leaving;
    hostGateNamed("CoCreateFreeThreadedMarshaler");
    (void)outer;
    if(!marshal)return (int32_t)0x80070057;   // E_INVALIDARG
    *marshal=(void*)&freeThreadedMarshaler;return 0;
}

// ID3DBlob over constant bytes (D3DCompile's precompiled shaders, title
// 0.91): one static object per shader, never freed, like the marshaler above.
static __attribute__((ms_abi)) int32_t blobQueryInterface(void* self,const void* iid,void** out) {
    static const uint8_t unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    static const uint8_t blob[16]={0x08,0xfb,0xa5,0x8b,0x95,0x51,0xe2,0x40,0xac,0x58,0x0d,0x98,0x9c,0x3a,0x01,0x02};
    if(!out)return (int32_t)0x80004003;   // E_POINTER
    if(iid&&(!__builtin_memcmp(iid,unknown,16)||!__builtin_memcmp(iid,blob,16))){*out=self;return 0;}
    *out=0;return (int32_t)0x80004002;   // E_NOINTERFACE
}
struct ConstantBlob {const void* const* vtable;const void* data;uint64_t size;};
static __attribute__((ms_abi)) void* blobPointer(void* self){return (void*)((struct ConstantBlob*)self)->data;}
static __attribute__((ms_abi)) uint64_t blobSize(void* self){return ((struct ConstantBlob*)self)->size;}
static const void* const blobVtable[5]={(const void*)&blobQueryInterface,(const void*)&freeThreadedAddRef,(const void*)&freeThreadedRelease,
                                        (const void*)&blobPointer,(const void*)&blobSize};
void* guestConstantBlob(unsigned index,const void* data,uint64_t size) {
    static struct ConstantBlob blobs[256];   // one per CompiledShaders.inc entry (16 until title 1.48)
    if(index>=256)return 0;
    blobs[index].vtable=blobVtable;blobs[index].data=data;blobs[index].size=size;
    return &blobs[index];
}

// vccorlib110's export has a C++ decorated name, which EXPORT cannot spell.
int32_t win_vccorlibGetActivationFactoryByPCWSTR(const uint16_t* name,const void* iid,void** factory);
static __attribute__((ms_abi)) int32_t ms_vccorlibGetActivationFactory(const uint16_t* name,const void* iid,void** factory) {
    int leaving __attribute__((cleanup(hostLeave)))=0;(void)leaving;
    hostGateNamed("GetActivationFactoryByPCWSTR");return win_vccorlibGetActivationFactoryByPCWSTR(name,iid,factory);
}

// ---- Exception dispatch (Exceptions.cpp, title 1.00) ---------------------
// RaiseException and RtlUnwindEx start by saving the caller's registers as a
// CONTEXT (0x4d0 bytes, on their own 16-byte aligned stack) as they will be
// once the call returns: Rip the return address, Rsp just above it. The host
// dispatches from that context.
#define CAPTURE_CONTEXT \
    "subq $0x4d8,%rsp\n" \
    "movq %rax,0x78(%rsp)\n" "movq %rcx,0x80(%rsp)\n" "movq %rdx,0x88(%rsp)\n" "movq %rbx,0x90(%rsp)\n" \
    "leaq 0x4e0(%rsp),%rax\n" "movq %rax,0x98(%rsp)\n" \
    "movq %rbp,0xa0(%rsp)\n" "movq %rsi,0xa8(%rsp)\n" "movq %rdi,0xb0(%rsp)\n" \
    "movq %r8,0xb8(%rsp)\n" "movq %r9,0xc0(%rsp)\n" "movq %r10,0xc8(%rsp)\n" "movq %r11,0xd0(%rsp)\n" \
    "movq %r12,0xd8(%rsp)\n" "movq %r13,0xe0(%rsp)\n" "movq %r14,0xe8(%rsp)\n" "movq %r15,0xf0(%rsp)\n" \
    "movq 0x4d8(%rsp),%rax\n" "movq %rax,0xf8(%rsp)\n" \
    "fxsave 0x100(%rsp)\n" "stmxcsr 0x34(%rsp)\n" \
    "movw %cs,0x38(%rsp)\n" "movw %ds,0x3a(%rsp)\n" "movw %es,0x3c(%rsp)\n" \
    "movw %fs,0x3e(%rsp)\n" "movw %gs,0x40(%rsp)\n" "movw %ss,0x42(%rsp)\n" \
    "pushfq\n" "popq %rax\n" "movl %eax,0x44(%rsp)\n" \
    "movl $0x10000f,0x30(%rsp)\n" \
    "movq 0x78(%rsp),%rax\n"

void hostRaiseException(uint32_t code,uint32_t flags,uint32_t count,const uint64_t* arguments,void* context);
void hostUnwindEx(void* frame,void* target,void* record,void* value,void* contextBuffer,void* history,void* captured);
__attribute__((used,ms_abi)) static void raiseWithContext(uint32_t code,uint32_t flags,uint32_t count,const uint64_t* arguments,void* context) {
    int leaving __attribute__((cleanup(hostLeave)))=0;(void)leaving;
    hostGateNamed("RaiseException");
    hostRaiseException(code,flags,count,arguments,context);
}
// RtlUnwindEx's fifth and sixth arguments are on the caller's stack, above the home area.
__attribute__((used,ms_abi)) static void unwindWithContext(void* frame,void* target,void* record,void* value,void* context,const uint64_t* entry) {
    hostGateNamed("RtlUnwindEx");
    hostUnwindEx(frame,target,record,value,(void*)entry[5],(void*)entry[6],context);
}
__attribute__((naked)) static void raiseException(void) {
    __asm__ volatile(
        CAPTURE_CONTEXT
        "movq %rsp,%rax\n"
        "subq $0x30,%rsp\n"
        "movq %rax,0x20(%rsp)\n"
        "call raiseWithContext\n"
        "addq $0x30,%rsp\n"
        "addq $0x4d8,%rsp\n"
        "ret\n");
}
__attribute__((naked)) static void unwindEx(void) {
    __asm__ volatile(
        CAPTURE_CONTEXT
        "movq %rsp,%rax\n"
        "leaq 0x4d8(%rsp),%r10\n"
        "subq $0x30,%rsp\n"
        "movq %rax,0x20(%rsp)\n"
        "movq %r10,0x28(%rsp)\n"
        "call unwindWithContext\n"
        "addq $0x30,%rsp\n"
        "addq $0x4d8,%rsp\n"
        "ret\n");
}
// Calls a frame's language handler (Windows convention) or a consolidation
// callback. Whoever unwinds past the call finds, just above the return
// address (guestHandlerReturn, guestConsolidateReturn), at +0x20, the context
// to go on from: the host's own frames below it have no unwind data.
//   uint32_t guestCallHandler(handler, record, frame, context, dispatch, resume)
//   uint64_t guestCallConsolidate(callback, record, resume)
__attribute__((naked)) uint32_t guestCallHandler(void) {
    __asm__ volatile(
        "pushq %rbp\n" "movq %rsp,%rbp\n"
        "andq $-16,%rsp\n" "subq $0x30,%rsp\n"
        "movq %r9,0x20(%rsp)\n"
        "movq %rdi,%rax\n" "movq %rcx,%r10\n"
        "movq %rsi,%rcx\n" "movq %r8,%r9\n" "movq %r10,%r8\n"
        "call *%rax\n"
        ".globl guestHandlerReturn\n" "guestHandlerReturn:\n"
        "movq %rbp,%rsp\n" "popq %rbp\n" "ret\n");
}
__attribute__((naked)) uint64_t guestCallConsolidate(void) {
    __asm__ volatile(
        "pushq %rbp\n" "movq %rsp,%rbp\n"
        "andq $-16,%rsp\n" "subq $0x30,%rsp\n"
        "movq %rdx,0x20(%rsp)\n"
        "movq %rsi,%rcx\n"
        "call *%rdi\n"
        ".globl guestConsolidateReturn\n" "guestConsolidateReturn:\n"
        "movq %rbp,%rsp\n" "popq %rbp\n" "ret\n");
}
// guestRestoreContext(context): loads a CONTEXT's registers and jumps to its
// Rip on its Rsp (NtContinue). The naked functions take their arguments in registers.
__attribute__((naked)) void guestRestoreContext(void) {
    __asm__ volatile(
        "fxrstor 0x100(%rdi)\n" "ldmxcsr 0x34(%rdi)\n"
        "movq 0x98(%rdi),%rax\n" "subq $8,%rax\n" "movq 0xf8(%rdi),%rcx\n" "movq %rcx,(%rax)\n"
        "movq 0x80(%rdi),%rcx\n" "movq 0x88(%rdi),%rdx\n" "movq 0x90(%rdi),%rbx\n"
        "movq 0xa0(%rdi),%rbp\n" "movq 0xa8(%rdi),%rsi\n"
        "movq 0xb8(%rdi),%r8\n" "movq 0xc0(%rdi),%r9\n" "movq 0xc8(%rdi),%r10\n" "movq 0xd0(%rdi),%r11\n"
        "movq 0xd8(%rdi),%r12\n" "movq 0xe0(%rdi),%r13\n" "movq 0xe8(%rdi),%r14\n" "movq 0xf0(%rdi),%r15\n"
        "movq %rax,%rsp\n"
        "movq 0x78(%rdi),%rax\n" "movq 0xb0(%rdi),%rdi\n"
        "ret\n");
}

#define EXPORT(dll,name,ret,params,args) {#dll,#name,(const void*)&ms_##name},
const GuestExport guestExports[]={
#include "win32/exports.inc"
    {"KERNEL32","RtlCaptureContext",(const void*)&captureContext},
    {"KERNEL32","RaiseException",(const void*)&raiseException},
    {"KERNEL32","RtlUnwindEx",(const void*)&unwindEx},
    {"ntdll","RtlUnwindEx",(const void*)&unwindEx},
    // ntdll's names for the same functions (xg_x.dll imports them from there).
    {"ntdll","RtlCaptureContext",(const void*)&captureContext},
    {"ntdll","RtlLookupFunctionEntry",(const void*)&ms_RtlLookupFunctionEntry},
    {"ntdll","RtlVirtualUnwind",(const void*)&ms_RtlVirtualUnwind},
    {"ole32","CoCreateFreeThreadedMarshaler",(const void*)&ms_CoCreateFreeThreadedMarshaler},
    {"vccorlib110","?GetActivationFactoryByPCWSTR@@YAJPEAXAEAVGuid@Platform@@PEAPEAX@Z",(const void*)&ms_vccorlibGetActivationFactory},
    {0,0,0}
};
#undef EXPORT

// The thread pool's waits and timers (title 1.10, ConcRT): an engine thread
// (made by CreateThread) running the host's loop for one wait or timer.
uint32_t pool_ThreadMain(void* parameter);
static __attribute__((ms_abi)) uint32_t ms_PoolThreadMain(void* parameter){hostGateNamed("PoolThread");return pool_ThreadMain(parameter);}
const void* const guestPoolThreadStart=(const void*)&ms_PoolThreadMain;

// Fibers (win32/Fibers.cpp, title 1.12). fiberSwitch(save, next) keeps the
// System V callee-saved registers, MXCSR and the x87 control word on the
// running stack, stores its pointer in *save and resumes the stack at next.
// A new fiber's stack starts with such a frame that returns into fiberStart,
// which calls fiberMain(r12 = the fiber) on an aligned stack.
__asm__(
    ".text\n"
    ".globl fiberSwitch\n"
    ".type fiberSwitch,@function\n"
    "fiberSwitch:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq $16,%rsp\n"
    "    stmxcsr 8(%rsp)\n"
    "    fnstcw (%rsp)\n"
    "    movq %rsp,(%rdi)\n"
    "    movq %rsi,%rsp\n"
    "    ldmxcsr 8(%rsp)\n"
    "    fldcw (%rsp)\n"
    "    addq $16,%rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    ret\n"
    ".size fiberSwitch,.-fiberSwitch\n"
    ".globl fiberStart\n"
    ".type fiberStart,@function\n"
    "fiberStart:\n"
    "    movq %r12,%rdi\n"
    "    andq $-16,%rsp\n"
    "    call fiberMain@PLT\n"
    "    ud2\n"
    ".size fiberStart,.-fiberStart\n");
