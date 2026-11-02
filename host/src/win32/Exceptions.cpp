// Windows exceptions (title 1.00): RaiseException dispatches through the
// unwind tables of the engine, the game and the DLLs, calling each frame's
// language handler (msvcr110's __CxxFrameHandler3 for C++), and RtlUnwindEx
// unwinds to the frame that handles it, as ntdll does on x64. An exception no
// handler takes is reported (for C++, its types, the thrower and its object)
// and ends the run.
#include "Win32.h"
#include "../Modules.h"
#include "../guest_abi.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>
#include <vector>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

// guest_abi.c's trampolines for dispatch.
extern "C" {
uint32_t guestCallHandler(const void* handler,void* record,uint64_t frame,void* context,void* dispatch,void* resume);
uint64_t guestCallConsolidate(const void* callback,void* record,void* resume);
[[noreturn]] void guestRestoreContext(const void* context);
extern char guestHandlerReturn[],guestConsolidateReturn[];
}
namespace {uint64_t tableImage(uint64_t pc);}

namespace {
constexpr uint32_t CppException=0xe06d7363,CppMagic=0x19930520,CppMagicPure=0x19930521,CppMagicNoexcept=0x19930522;
std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
bool inImage(uint64_t address){return address>=process().imageBase&&address-process().imageBase<process().imageSize;}
// A NUL-terminated string inside the engine image or a DLL the host loaded (bounded).
std::string imageString(uint64_t address) {
    if(!inImage(address)&&!azimuth::moduleContaining(address))return "?";
    std::string out;auto p=reinterpret_cast<const char*>(address);
    for(int n=0;n<256&&p[n];++n)out+=p[n];
    return out;
}
// MSVC x64 ThrowInfo (image-relative offsets): attributes, pmfnUnwind,
// pForwardCompat, pCatchableTypeArray -> { count, rva[] } -> CatchableType
// { properties, pType, ... } -> TypeDescriptor { vftable, spare, name[] }.
std::string thrownTypes(uint64_t throwInfo,uint64_t imageBase) {
    if(!throwInfo||!imageBase)return "?";
    uint32_t arrayRva;std::memcpy(&arrayRva,reinterpret_cast<const void*>(throwInfo+12),4);
    auto array=imageBase+arrayRva;int32_t count;std::memcpy(&count,reinterpret_cast<const void*>(array),4);
    std::string types;
    for(int32_t n=0;n<count&&n<8;++n) {
        uint32_t typeRva;std::memcpy(&typeRva,reinterpret_cast<const void*>(array+4+n*4),4);
        uint32_t descriptorRva;std::memcpy(&descriptorRva,reinterpret_cast<const void*>(imageBase+typeRva+4),4);
        if(!types.empty())types+=", ";
        types+=imageString(imageBase+descriptorRva+16);
    }
    return types;
}
}

extern "C" void* win_RtlPcToFileHeader(void* pc,void** base) {
    auto address=reinterpret_cast<uint64_t>(pc);
    if(inImage(address)) {
        *base=reinterpret_cast<void*>(process().imageBase);return *base;
    }
    // The DLLs the host loaded (msvcr110 throws its C++ exceptions with RVAs
    // from its own base: a null base made it read 0xae538, title 0.58).
    if(auto module=azimuth::moduleContaining(address)) {
        *base=reinterpret_cast<void*>(module);return *base;
    }
    // Images the engine mapped itself (the game), known by their function tables.
    if(auto module=tableImage(address)){*base=reinterpret_cast<void*>(module);return *base;}
    *base=nullptr;return nullptr;
}
// An exception no frame handled: reported, and the run ends.
[[noreturn]] static void unhandled(uint32_t code,uint32_t flags,uint32_t count,const uint64_t* arguments) {
    if(code==CppException&&count>=3&&arguments&&(arguments[0]==CppMagic||arguments[0]==CppMagicPure||arguments[0]==CppMagicNoexcept)) {
        uint64_t object=arguments[1],throwInfo=arguments[2],imageBase=count>=4?arguments[3]:0;
        auto types=thrownTypes(throwInfo,imageBase);
        std::string message;
        // std::exception: vftable, then __std_exception_data { const char* _What; bool _DoFree; }.
        if(object&&types.find("exception@std")!=std::string::npos) {
            uint64_t what;std::memcpy(&what,reinterpret_cast<const void*>(object+8),8);
            if(what){auto text=reinterpret_cast<const char*>(what);for(int n=0;n<512&&text[n];++n)message+=text[n];}
        }
        // Anything else (C++/CX's Platform::COMException is a handle): the thrown
        // bytes and the object they point to, where its HRESULT is, and who threw
        // (title 0.98: a COMException with no visible cause).
        auto dump=[](uint64_t at,unsigned words) {
            std::string out;
            for(unsigned n=0;n<words;++n){uint64_t v;std::memcpy(&v,reinterpret_cast<const void*>(at+8*n),8);out+=" "+hex(v);}
            return out;
        };
        std::string detail;
        if(object&&types.find("exception@std")==std::string::npos) {
            detail=" | objeto lancado em "+hex(object)+":"+dump(object,2);
            uint64_t handle;std::memcpy(&handle,reinterpret_cast<const void*>(object),8);
            if(handle>0x10000&&(handle&7)==0)detail+=" | objeto apontado:"+dump(handle,12);
        }
        log("Excecao C++ ["+types+"]; lancada de"+codeCallers()+detail);
        stopGuest("o motor lancou uma excecao C++ ["+types+"]"+(message.empty()?"":": "+message)+
                  " (tratamento de excecoes do Windows ainda nao implementado)");
    }
    stopGuest("o motor chamou RaiseException codigo "+hex(code)+" flags "+hex(flags)+" (tratamento de excecoes ainda nao implementado)");
}

// ---- Unwind tables -----------------------------------------------------------
// RUNTIME_FUNCTION { BeginAddress, EndAddress, UnwindData } (image-relative),
// from tables code registers (RtlAddFunctionTable: the images the engine maps
// itself) or from the exception directory of the engine and of the DLLs the
// host loaded.
namespace {
struct RuntimeFunction {uint32_t begin,end,unwind;};
struct Table {const RuntimeFunction* entries;uint32_t count;uint64_t base;};
std::mutex tablesLock;
std::vector<Table> tables;
const RuntimeFunction* search(const RuntimeFunction* entries,uint32_t count,uint64_t base,uint64_t pc) {
    if(pc<base)return nullptr;
    auto rva=uint32_t(pc-base);
    auto end=entries+count;
    auto found=std::upper_bound(entries,end,rva,[](uint32_t value,const RuntimeFunction& f){return value<f.begin;});
    if(found==entries)return nullptr;
    --found;
    if(rva>=found->end)return nullptr;
    // An entry whose unwind data has its low bit set points to the primary entry.
    if(found->unwind&1)return reinterpret_cast<const RuntimeFunction*>(base+(found->unwind&~1u));
    return found;
}
// The exception directory (data directory 3) of the PE image mapped at `base`.
const RuntimeFunction* imageFunctions(uint64_t base,uint32_t& count) {
    uint32_t header;std::memcpy(&header,reinterpret_cast<const void*>(base+0x3c),4);
    auto optional=base+header+24;
    uint32_t rva,size;
    std::memcpy(&rva,reinterpret_cast<const void*>(optional+112+3*8),4);
    std::memcpy(&size,reinterpret_cast<const void*>(optional+112+3*8+4),4);
    count=size/sizeof(RuntimeFunction);
    return rva&&count?reinterpret_cast<const RuntimeFunction*>(base+rva):nullptr;
}
}
namespace {
// The image (the game, mapped by the engine) whose registered table covers `pc`.
uint64_t tableImage(uint64_t pc) {
    std::lock_guard<std::mutex> guard(tablesLock);
    for(auto const& t:tables)if(pc>=t.base+t.entries[0].begin&&pc<t.base+t.entries[t.count-1].end)return t.base;
    return 0;
}
// CONTEXT (x64): Rax..R15 from 0x78 in the unwind codes' register order, Rip
// at 0xf8, the XMM registers in FltSave from 0x1a0.
constexpr size_t ContextSize=0x4d0,RegistersOffset=0x78,RipOffset=0xf8,XmmOffset=0x1a0;
enum Register {Rax=0,Rcx,Rdx,Rbx,Rsp,Rbp,Rsi,Rdi,R8,R9,R10,R11,R12,R13,R14,R15};
uint64_t& reg(uint8_t* context,unsigned n){return *reinterpret_cast<uint64_t*>(context+RegistersOffset+8*n);}
uint64_t& rip(uint8_t* context){return *reinterpret_cast<uint64_t*>(context+RipOffset);}
// KNONVOLATILE_CONTEXT_POINTERS: FloatingContext[16], then IntegerContext[16].
void setReg(uint8_t* context,uint64_t* pointers,unsigned n,uint64_t at) {
    std::memcpy(&reg(context,n),reinterpret_cast<const void*>(at),8);
    if(pointers)pointers[16+n]=at;
}
void setXmm(uint8_t* context,uint64_t* pointers,unsigned n,uint64_t at) {
    std::memcpy(context+XmmOffset+16*n,reinterpret_cast<const void*>(at),16);
    if(pointers)pointers[n]=at;
}

// UNWIND_INFO: version and flags, prolog size, code count, frame register and
// offset, then the codes (each 2 bytes: prolog offset, op and info).
enum UnwindOp {PushNonvol=0,AllocLarge=1,AllocSmall=2,SetFpreg=3,SaveNonvol=4,SaveNonvolFar=5,Epilog=6,SaveXmm128=8,SaveXmm128Far=9,PushMachframe=10};
unsigned opSlots(uint8_t op,uint8_t info) {
    switch(op) {
        case AllocLarge:return info?3:2;
        case SaveNonvol:case SaveXmm128:case Epilog:return 2;
        case SaveNonvolFar:case SaveXmm128Far:return 3;
        default:return 1;
    }
}
// Whether `pc` is in an epilog: an optional add/lea to rsp, pops, then a ret
// (or a jmp inside the function to more of the same).
bool inEpilog(const uint8_t* pc,uint64_t base,const RuntimeFunction* f) {
    if((pc[0]&0xf8)==0x48) {
        switch(pc[1]) {
            case 0x81:if(pc[0]==0x48&&pc[2]==0xc4){pc+=7;break;}return false;   // add $nnnn,%rsp
            case 0x83:if(pc[0]==0x48&&pc[2]==0xc4){pc+=4;break;}return false;   // add $n,%rsp
            case 0x8d:                                                          // lea n(reg),%rsp
                if(pc[0]&0x06)return false;
                if(((pc[2]>>3)&7)!=4)return false;
                if((pc[2]&7)==4)return false;
                if((pc[2]>>6)==1){pc+=4;break;}
                if((pc[2]>>6)==2){pc+=7;break;}
                return false;
        }
    }
    for(unsigned guard=0;guard<64;++guard) {
        if((*pc&0xf0)==0x40)++pc;
        switch(*pc) {
            case 0x58:case 0x59:case 0x5a:case 0x5b:case 0x5c:case 0x5d:case 0x5e:case 0x5f:++pc;continue;
            case 0xc2:case 0xc3:return true;
            case 0xe9:{int32_t rel;std::memcpy(&rel,pc+1,4);pc+=5+rel;
                       uint64_t at=reinterpret_cast<uint64_t>(pc)-base;if(at>=f->begin&&at<f->end)continue;return false;}
            case 0xeb:{pc+=2+int8_t(pc[1]);uint64_t at=reinterpret_cast<uint64_t>(pc)-base;if(at>=f->begin&&at<f->end)continue;return false;}
            case 0xf3:return pc[1]==0xc3;
        }
        return false;
    }
    return false;
}
void runEpilog(const uint8_t* pc,uint8_t* context,uint64_t* pointers) {
    for(unsigned guard=0;guard<64;++guard) {
        uint8_t rex=0;
        if((*pc&0xf0)==0x40)rex=*pc++&0x0f;
        switch(*pc) {
            case 0x58:case 0x59:case 0x5a:case 0x5b:case 0x5c:case 0x5d:case 0x5e:case 0x5f:
                setReg(context,pointers,unsigned(*pc-0x58)+(rex&1)*8,reg(context,Rsp));reg(context,Rsp)+=8;++pc;continue;
            case 0x81:{int32_t n;std::memcpy(&n,pc+2,4);reg(context,Rsp)+=int64_t(n);pc+=6;continue;}
            case 0x83:reg(context,Rsp)+=int64_t(int8_t(pc[2]));pc+=3;continue;
            case 0x8d:
                if((pc[1]>>6)==1){reg(context,Rsp)=reg(context,(pc[1]&7)+(rex&1)*8)+int64_t(int8_t(pc[2]));pc+=3;}
                else{int32_t n;std::memcpy(&n,pc+2,4);reg(context,Rsp)=reg(context,(pc[1]&7)+(rex&1)*8)+int64_t(n);pc+=6;}
                continue;
            case 0xc2:{uint16_t n;std::memcpy(&n,pc+1,2);std::memcpy(&rip(context),reinterpret_cast<const void*>(reg(context,Rsp)),8);reg(context,Rsp)+=8+n;return;}
            case 0xc3:case 0xf3:std::memcpy(&rip(context),reinterpret_cast<const void*>(reg(context,Rsp)),8);reg(context,Rsp)+=8;return;
            case 0xe9:{int32_t rel;std::memcpy(&rel,pc+1,4);pc+=5+rel;continue;}
            case 0xeb:pc+=2+int8_t(pc[1]);continue;
        }
        return;
    }
}
// RtlVirtualUnwind (as ntdll's): unwinds one frame of `context`, returns the
// frame's handler of the `type` asked (or null), its data and the establisher frame.
const void* virtualUnwind(uint32_t type,uint64_t base,uint64_t pc,const RuntimeFunction* f,uint8_t* context,void** data,uint64_t* frameOut,uint64_t* pointers) {
    uint64_t frame=reg(context,Rsp);*frameOut=frame;
    bool machFrame=false;
    const uint8_t* info=nullptr;uint32_t prologOffset=~0u;
    for(unsigned chain=0;chain<32;++chain) {
        info=reinterpret_cast<const uint8_t*>(base+f->unwind);
        uint8_t version=info[0]&7,flags=info[0]>>3,prolog=info[1],count=info[2],frameReg=info[3]&15,frameOffset=info[3]>>4;
        if(version!=1&&version!=2)return nullptr;
        if(frameReg)frame=reg(context,frameReg)-uint64_t(frameOffset)*16;
        if(pc>=base+f->begin&&pc<base+f->begin+prolog)prologOffset=uint32_t(pc-base-f->begin);
        else {
            prologOffset=~0u;
            if(count&&inEpilog(reinterpret_cast<const uint8_t*>(pc),base,f)) {
                runEpilog(reinterpret_cast<const uint8_t*>(pc),context,pointers);
                *frameOut=frame;
                return nullptr;
            }
        }
        const uint8_t* codes=info+4;
        for(unsigned i=0;i<count;i+=opSlots(codes[2*i+1]&15,codes[2*i+1]>>4)) {
            uint8_t offset=codes[2*i],op=codes[2*i+1]&15,opInfo=codes[2*i+1]>>4;
            if(prologOffset<offset)continue;
            uint16_t slot1;std::memcpy(&slot1,codes+2*(i+1),2);
            uint32_t slots;std::memcpy(&slots,codes+2*(i+1),4);
            switch(op) {
                case PushNonvol:setReg(context,pointers,opInfo,reg(context,Rsp));reg(context,Rsp)+=8;break;
                case AllocLarge:reg(context,Rsp)+=opInfo?slots:uint32_t(slot1)*8;break;
                case AllocSmall:reg(context,Rsp)+=uint64_t(opInfo+1)*8;break;
                case SetFpreg:reg(context,Rsp)=*frameOut=frame;break;
                case SaveNonvol:setReg(context,pointers,opInfo,frame+uint64_t(slot1)*8);break;
                case SaveNonvolFar:setReg(context,pointers,opInfo,frame+slots);break;
                case SaveXmm128:setXmm(context,pointers,opInfo,frame+uint64_t(slot1)*16);break;
                case SaveXmm128Far:setXmm(context,pointers,opInfo,frame+slots);break;
                case PushMachframe:
                    if(opInfo)reg(context,Rsp)+=8;
                    std::memcpy(&rip(context),reinterpret_cast<const void*>(reg(context,Rsp)),8);
                    std::memcpy(&reg(context,Rsp),reinterpret_cast<const void*>(reg(context,Rsp)+24),8);
                    machFrame=true;break;
                default:break;   // Epilog (version 2): nothing to do
            }
        }
        if(!(flags&4))break;   // UNW_FLAG_CHAININFO: go on with the chained entry
        f=reinterpret_cast<const RuntimeFunction*>(codes+2*((count+1)&~1u));
    }
    if(!machFrame) {
        std::memcpy(&rip(context),reinterpret_cast<const void*>(reg(context,Rsp)),8);
        reg(context,Rsp)+=8;
    }
    uint8_t flags=info[0]>>3,count=info[2];
    if(!(flags&type)||prologOffset!=~0u)return nullptr;
    auto handler=reinterpret_cast<const uint32_t*>(info+4+2*((count+1)&~1u));
    if(data)*data=const_cast<uint32_t*>(handler+1);
    return reinterpret_cast<const void*>(base+*handler);
}
}
extern "C" int32_t win_RtlAddFunctionTable(void* entries,uint32_t count,uint64_t base) {
    if(!entries||!count)return 0;
    std::lock_guard<std::mutex> guard(tablesLock);
    tables.push_back({static_cast<const RuntimeFunction*>(entries),count,base});
    log("RtlAddFunctionTable: "+std::to_string(count)+" funcoes na imagem em "+hex(base));
    return 1;
}
extern "C" int32_t win_RtlDeleteFunctionTable(void* entries) {
    std::lock_guard<std::mutex> guard(tablesLock);
    for(auto t=tables.begin();t!=tables.end();++t)if(t->entries==entries){tables.erase(t);return 1;}
    return 0;
}
extern "C" void* win_RtlLookupFunctionEntry(uint64_t pc,uint64_t* imageBase,void*) {
    {
        std::lock_guard<std::mutex> guard(tablesLock);
        for(auto const& t:tables) {
            if(pc<t.base+t.entries[0].begin||pc>=t.base+t.entries[t.count-1].end)continue;
            if(auto f=search(t.entries,t.count,t.base,pc)){if(imageBase)*imageBase=t.base;return const_cast<RuntimeFunction*>(f);}
        }
    }
    uint64_t base=inImage(pc)?process().imageBase:azimuth::moduleContaining(pc);
    if(!base)return nullptr;
    uint32_t count=0;auto entries=imageFunctions(base,count);
    if(!entries)return nullptr;
    auto f=search(entries,count,base,pc);
    if(f&&imageBase)*imageBase=base;
    return const_cast<RuntimeFunction*>(f);
}
extern "C" void* win_RtlVirtualUnwind(uint32_t type,uint64_t base,uint64_t pc,void* function,void* context,void** data,uint64_t* frame,void* pointers) {
    return const_cast<void*>(virtualUnwind(type,base,pc,static_cast<const RuntimeFunction*>(function),static_cast<uint8_t*>(context),data,frame,
                                           static_cast<uint64_t*>(pointers)));
}

// ---- Dispatch -------------------------------------------------------------
namespace {
constexpr uint32_t UnwFlagEHandler=1,UnwFlagUHandler=2;
constexpr uint32_t EhNoncontinuable=1,EhUnwinding=2,EhExitUnwind=4,EhTargetUnwind=0x20,EhCollidedUnwind=0x40;
constexpr uint32_t StatusUnwind=0xc0000027,StatusLongJump=0x80000026,StatusUnwindConsolidate=0x80000029;
enum Disposition {ContinueExecution=0,ContinueSearch=1,NestedException=2,CollidedUnwind=3};
// EXCEPTION_RECORD (x64, 0x98 bytes).
struct Record {uint32_t code,flags;uint64_t record,address;uint32_t count,pad;uint64_t information[15];};
static_assert(sizeof(Record)==0x98,"EXCEPTION_RECORD");
// DISPATCHER_CONTEXT (x64).
struct Dispatch {uint64_t controlPc,imageBase;const void* functionEntry;uint64_t establisherFrame,targetIp;void* contextRecord;
                 const void* languageHandler;void* handlerData;void* historyTable;uint32_t scopeIndex,fill;};
static_assert(sizeof(Dispatch)==80,"DISPATCHER_CONTEXT");
struct alignas(16) Context {uint8_t bytes[ContextSize];};

// Code that unwind data can describe: the engine, the DLLs the host loaded,
// and the images whose tables were registered (the game). Anything else is
// the host's, where an exception stops being the engine's.
bool guestCode(uint64_t pc) {
    return inImage(pc)||azimuth::moduleContaining(pc)||tableImage(pc);
}
// One frame up (ntdll's virtual_unwind): `context` becomes the caller's, `d`
// says what the frame was. False past the engine's outermost frame.
bool unwindFrame(uint32_t type,Dispatch& d,uint8_t* context) {
    uint64_t pc=rip(context);
    d.controlPc=pc;d.imageBase=0;d.scopeIndex=0;d.functionEntry=nullptr;d.languageHandler=nullptr;d.handlerData=nullptr;
    // The host's handler and catch-block trampolines: on with the context they saved.
    if(pc==reinterpret_cast<uint64_t>(guestHandlerReturn)||pc==reinterpret_cast<uint64_t>(guestConsolidateReturn)) {
        void* resume;std::memcpy(&resume,reinterpret_cast<const void*>(reg(context,Rsp)+0x20),8);
        d.establisherFrame=reg(context,Rsp);
        std::memcpy(context,resume,ContextSize);
        return true;
    }
    uint64_t base=0;
    if(auto f=static_cast<const RuntimeFunction*>(win_RtlLookupFunctionEntry(pc,&base,nullptr))) {
        d.imageBase=base;d.functionEntry=f;
        d.languageHandler=virtualUnwind(type,base,pc,f,context,&d.handlerData,&d.establisherFrame,nullptr);
        return true;
    }
    if(!pc||!guestCode(pc))return false;
    // A leaf function: the return address is on top of the stack.
    d.establisherFrame=reg(context,Rsp);
    std::memcpy(&rip(context),reinterpret_cast<const void*>(reg(context,Rsp)),8);
    reg(context,Rsp)+=8;
    return true;
}
bool validFrame(uint64_t frame){return frame&&!(frame&7);}

// NtContinue with RtlRestoreContext's special records: longjmp's jump buffer,
// and C++'s consolidation (the catch block runs, returning where to go on).
[[noreturn]] void restore(Context& context,Record* record) {
    auto c=context.bytes;
    if(record&&record->code==StatusLongJump&&record->count>=1) {
        // _JUMP_BUFFER: Frame, Rbx, Rsp, Rbp, Rsi, Rdi, R12-R15, Rip, MxCsr, FpCsr, Spare, Xmm6-15.
        auto jump=reinterpret_cast<const uint8_t*>(record->information[0]);
        const unsigned regs[]={Rbx,Rsp,Rbp,Rsi,Rdi,R12,R13,R14,R15};
        for(unsigned n=0;n<9;++n)std::memcpy(&reg(c,regs[n]),jump+8+8*n,8);
        std::memcpy(&rip(c),jump+80,8);
        for(unsigned n=0;n<10;++n)std::memcpy(c+XmmOffset+16*(6+n),jump+96+16*n,16);
    }else if(record&&record->code==StatusUnwindConsolidate&&record->count>=1) {
        rip(c)=guestCallConsolidate(reinterpret_cast<const void*>(record->information[0]),record,c);
    }
    guestRestoreContext(c);
}
}

// RaiseException (guest_abi.c's entry point gives the caller's context).
extern "C" void hostRaiseException(uint32_t code,uint32_t flags,uint32_t count,const uint64_t* arguments,void* captured) {
    // Messages for a debugger, raised inside a __try whose handler only
    // swallows them: with no debugger attached the raiser just goes on, so
    // returning is what Windows' dispatch amounts to (title 0.91: a game thread
    // naming itself, MS_VC_EXCEPTION). THREADNAME_INFO: type 0x1000, name, thread.
    constexpr uint32_t ThreadName=0x406d1388,DebugPrint=0x40010006,DebugPrintWide=0x4001000a;
    if(code==ThreadName) {
        if(count>=2&&arguments&&uint32_t(arguments[0])==0x1000&&arguments[1]) {
            std::string name;auto text=reinterpret_cast<const char*>(arguments[1]);
            for(int n=0;n<64&&text[n];++n)name+=text[n];
            uint32_t thread=count>=3?uint32_t(arguments[2]):~0u;
            log("Nome de thread pelo motor: "+name+" (thread "+(thread==~0u?"atual":std::to_string(thread))+")");
        }
        return;
    }
    if(code==DebugPrint||code==DebugPrintWide)return;
    Record record{};
    record.code=code;record.flags=flags&EhNoncontinuable;record.address=rip(static_cast<uint8_t*>(captured));
    record.count=std::min<uint32_t>(count,15);
    if(arguments)for(uint32_t n=0;n<record.count;++n)record.information[n]=arguments[n];
    Context context;std::memcpy(context.bytes,captured,ContextSize);
    Dispatch d{};d.contextRecord=context.bytes;
    for(unsigned depth=0;depth<4096;++depth) {
        if(!unwindFrame(UnwFlagEHandler,d,context.bytes))break;
        if(!validFrame(d.establisherFrame))break;
        if(!d.languageHandler)continue;
        uint32_t result=guestCallHandler(d.languageHandler,&record,d.establisherFrame,captured,&d,captured);
        record.flags&=EhNoncontinuable;
        if(result==ContinueExecution) {
            if(record.flags&EhNoncontinuable)break;
            Context resume;std::memcpy(resume.bytes,captured,ContextSize);
            restore(resume,nullptr);
        }
    }
    unhandled(code,flags,count,arguments);
}

// RtlUnwindEx (guest_abi.c's entry point gives the caller's context; Windows
// captures its own and unwinds it first, which comes to the same).
extern "C" void hostUnwindEx(void* end,void* target,void* recordArg,void* value,void* buffer,void* history,void* captured) {
    Context scratch;
    auto context=buffer?static_cast<uint8_t*>(buffer):scratch.bytes;
    std::memcpy(context,captured,ContextSize);
    Record local{};
    auto record=recordArg?static_cast<Record*>(recordArg):&local;
    if(!recordArg){local.code=StatusUnwind;local.address=rip(context);}
    record->flags|=EhUnwinding|(end?0:EhExitUnwind);
    auto frameEnd=reinterpret_cast<uint64_t>(end);
    Dispatch d{};
    d.establisherFrame=reg(context,Rsp);d.targetIp=reinterpret_cast<uint64_t>(target);d.contextRecord=context;d.historyTable=history;
    Context next;std::memcpy(next.bytes,context,ContextSize);
    for(unsigned depth=0;depth<4096;++depth) {
        if(!unwindFrame(UnwFlagUHandler,d,next.bytes))break;
    collided:
        if(!validFrame(d.establisherFrame))break;
        if(d.languageHandler) {
            if(frameEnd&&d.establisherFrame>frameEnd)
                stopGuest("RtlUnwindEx: quadro de destino "+hex(frameEnd)+" nao encontrado (passou de "+hex(d.establisherFrame)+")");
            if(d.establisherFrame==frameEnd)record->flags|=EhTargetUnwind;
            uint32_t result=guestCallHandler(d.languageHandler,record,d.establisherFrame,d.contextRecord,&d,d.contextRecord);
            if(result==CollidedUnwind) {
                // An unwind handler's own unwind reached here: go on from where it was.
                std::memcpy(context,d.contextRecord,ContextSize);std::memcpy(next.bytes,context,ContextSize);d.contextRecord=context;
                uint64_t frame;void* data;
                virtualUnwind(0,d.imageBase,d.controlPc,static_cast<const RuntimeFunction*>(d.functionEntry),next.bytes,&data,&frame,nullptr);
                record->flags|=EhCollidedUnwind;
                goto collided;
            }
            record->flags&=~EhCollidedUnwind;
        }
        if(d.establisherFrame==frameEnd)break;
        std::memcpy(context,next.bytes,ContextSize);
    }
    reg(context,Rax)=reinterpret_cast<uint64_t>(value);
    rip(context)=reinterpret_cast<uint64_t>(target);
    Context final;std::memcpy(final.bytes,context,ContextSize);
    restore(final,record);
}
