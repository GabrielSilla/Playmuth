// Engine imports that need no real machinery (title 0.30 stopped on the first,
// RtlCaptureStackBackTrace, from a worker thread). Each answers as Windows
// does where the feature is absent: a stack walk that finds no frames, no
// console, COM without registered classes. What would need real work
// (structured exception dispatch, HLSL compilation) stays a diagnostic stop.
#include "Win32.h"
#include <atomic>
#include <mutex>
#include <random>
#include "../HostHeap.h"
#include "../Modules.h"
#include <cstring>
#include <string>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
constexpr uint32_t ErrorEnvvarNotFound=203;
constexpr int32_t ENotImplemented=int32_t(0x80004001),ClassNotRegistered=int32_t(0x80040154);
void once(const char* what) {
    static std::mutex lock;static std::vector<std::string> told;
    std::lock_guard<std::mutex> guard(lock);
    for(auto& t:told)if(t==what)return;
    told.push_back(what);log(what);
}
}

// No frames: the engine's stack capture (allocation tracking, logging) gets an empty trace.
extern "C" uint16_t win_RtlCaptureStackBackTrace(uint32_t,uint32_t,void**,uint32_t* hash){if(hash)*hash=0;return 0;}
extern "C" int32_t win_FlushInstructionCache(void*,const void*,uint64_t){return 1;}   // x86 keeps instruction caches coherent

extern "C" uint32_t win_GetEnvironmentVariableW(const uint16_t* name,uint16_t* buffer,uint32_t size) {
    auto key=utf8(name,length(name));
    std::unique_lock<std::mutex> lock;
    for(auto& v:dllEnvironment(lock)) {
        if(v.first!=key)continue;
        auto wide=utf16(v.second);
        if(wide.size()>=size)return uint32_t(wide.size()+1);
        std::memcpy(buffer,wide.c_str(),(wide.size()+1)*2);
        return uint32_t(wide.size());
    }
    setLastError(ErrorEnvvarNotFound);return 0;
}
extern "C" int32_t win_SetEnvironmentVariableW(const uint16_t* name,const uint16_t* value) {
    return win_SetEnvironmentVariableA(utf8(name,length(name)).c_str(),value?utf8(value,length(value)).c_str():nullptr);
}
extern "C" uint32_t win_GetSystemDirectoryW(uint16_t* buffer,uint32_t size) {
    static const char16_t directory[]=u"C:\\windows\\system32";
    constexpr uint32_t needed=sizeof directory/2;
    if(size<needed)return needed;
    std::memcpy(buffer,directory,sizeof directory);
    return needed-1;
}
extern "C" uint32_t win_FormatMessageW(uint32_t flags,const void*,uint32_t id,uint32_t,uint16_t* buffer,uint32_t size,void*) {
    constexpr uint32_t AllocateBuffer=0x100;
    if(flags&AllocateBuffer){setLastError(ErrorNotSupported);return 0;}
    auto text=utf16("Erro "+std::to_string(id)+"\r\n");
    if(!buffer||text.size()>=size){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(buffer,text.c_str(),(text.size()+1)*2);
    return uint32_t(text.size());
}

// A desktop process started without a console.
extern "C" int32_t win_SetConsoleCtrlHandler(void*,int32_t){return 1;}
extern "C" int32_t win_GetConsoleMode(void*,uint32_t*){setLastError(ErrorInvalidHandle);return 0;}
extern "C" int32_t win_WriteConsoleW(void*,const uint16_t*,uint32_t,uint32_t* written,void*){if(written)*written=0;setLastError(ErrorInvalidHandle);return 0;}
extern "C" uint32_t win_GetConsoleOutputCP(void){return 437;}
extern "C" int32_t win_UnhandledExceptionFilter(void*){return 0;}   // EXCEPTION_CONTINUE_SEARCH

// File I/O completes at once (Files.cpp), so an OVERLAPPED is already done:
// Internal (offset 0) is the status, InternalHigh (offset 8) the byte count.
extern "C" int32_t win_GetOverlappedResultEx(void*,void* overlapped,uint32_t* bytes,uint32_t,int32_t) {
    if(!overlapped){setLastError(ErrorInvalidParameter);return 0;}
    uint64_t status,done;
    std::memcpy(&status,overlapped,8);std::memcpy(&done,static_cast<uint8_t*>(overlapped)+8,8);
    if(bytes)*bytes=uint32_t(done);
    if(status==0xc0000011){setLastError(ErrorHandleEof);return 0;}   // STATUS_END_OF_FILE
    if(status){setLastError(31);return 0;}   // ERROR_GEN_FAILURE
    return 1;
}
namespace {
std::atomic<uint64_t> watchCalls{0},watchPages{0},watchBytes{0},watchResets{0};
}
std::string azimuth::win32::writeWatchReport() {
    return "WriteWatch: "+std::to_string(watchCalls.exchange(0))+" consultas, "+
        std::to_string(watchBytes.exchange(0))+" bytes consultados, "+
        std::to_string(watchPages.exchange(0))+" paginas devolvidas, "+
        std::to_string(watchResets.exchange(0))+" resets (intervalo de ~10 s)";
}
// Real tracking lives with the virtual-memory region table.
namespace azimuth::win32 {
uint32_t queryWriteWatch(uint32_t,void*,uint64_t,void**,uint64_t*,uint32_t*);
uint32_t resetWriteWatch(void*,uint64_t);
}
extern "C" uint32_t win_GetWriteWatch(uint32_t flags,void* base,uint64_t size,void** addresses,uint64_t* count,uint32_t* granularity){
    auto result=queryWriteWatch(flags,base,size,addresses,count,granularity);
    if(!result){
        watchCalls.fetch_add(1,std::memory_order_relaxed);watchBytes.fetch_add(size,std::memory_order_relaxed);
        watchPages.fetch_add(*count,std::memory_order_relaxed);if(flags&1)watchResets.fetch_add(1,std::memory_order_relaxed);
    }
    return result;
}
extern "C" uint32_t win_ResetWriteWatch(void* base,uint64_t size){
    watchResets.fetch_add(1,std::memory_order_relaxed);return resetWriteWatch(base,size);
}

// COM with two classes, MMDeviceEnumerator (Audio.cpp) and the WIC factory
// (Wic.cpp); every other class is "not registered", named in the log.
int32_t createAudioEnumerator(const void* id,const void* want,void** out,bool& known);
extern "C" int32_t createWicFactory(const void* id,const void* want,void** out,bool& known);   // Wic.cpp, title 1.42
extern "C" void* win_CoTaskMemAlloc(uint64_t size){return azimuth::heapAlloc(size,16,false);}
extern "C" void win_CoTaskMemFree(void* block){azimuth::heapFree(block);}
extern "C" int32_t win_CoInitializeEx(void*,uint32_t){return 0;}
extern "C" void win_CoUninitialize(void){}
// WinRT on a thread: as COM, nothing to set up (ConcRT calls it for its threads).
extern "C" int32_t win_RoInitialize(uint32_t){return 0;}
extern "C" void win_RoUninitialize(void){}
extern "C" int32_t win_CoCreateInstance(const void* id,void*,uint32_t,const void* want,void** out) {
    bool known;
    int32_t result=createAudioEnumerator(id,want,out,known);
    if(known)return result;
    result=createWicFactory(id,want,out,known);
    if(known)return result;
    if(out)*out=nullptr;
    static std::atomic<unsigned> reports{0};
    if(id&&reports++<20) {
        uint8_t g[16];std::memcpy(g,id,16);char text[96];
        std::snprintf(text,sizeof text,"CoCreateInstance: classe %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x nao registrada no host",
                      g[3],g[2],g[1],g[0],g[5],g[4],g[7],g[6],g[8],g[9],g[10],g[11],g[12],g[13],g[14],g[15]);
        log(text);
    }
    return ClassNotRegistered;
}
extern "C" int32_t win_CoGetApartmentType(int32_t* type,int32_t* qualifier){if(type)*type=1;if(qualifier)*qualifier=0;return 0;}   // APTTYPE_MTA
// A version-4 random GUID.
extern "C" int32_t win_CoCreateGuid(uint8_t* guid) {
    if(!guid)return int32_t(0x80070057);
    static std::mutex lock;static std::mt19937_64 random{std::random_device{}()};
    std::lock_guard<std::mutex> guard(lock);
    for(int n=0;n<16;n+=8){uint64_t value=random();std::memcpy(guid+n,&value,8);}
    guid[7]=uint8_t((guid[7]&0x0f)|0x40);guid[8]=uint8_t((guid[8]&0x3f)|0x80);
    return 0;
}
extern "C" int32_t win_SHCreateItemFromParsingName(const uint16_t* path,void*,const void*,void** out) {
    if(out)*out=nullptr;
    log("SHCreateItemFromParsingName: "+(path?utf8(path,length(path)):std::string("(nulo)"))+" ainda sem implementacao (E_NOTIMPL)");
    return ENotImplemented;
}

// RTL_OSVERSIONINFOW / EXW: the same Windows 10 GetVersionExW reports; NTSTATUS.
extern "C" int32_t win_RtlGetVersion(void* info) {
    if(!info)return int32_t(0xC000000D);
    return win_GetVersionExW(info)?0:int32_t(0xC000000D);
}
// COM streams: not offered; the caller gets the failure instead of the engine stopping.
extern "C" int32_t win_CreateStreamOnHGlobal(void*,int32_t,void** stream) {
    if(stream)*stream=nullptr;
    once("CreateStreamOnHGlobal ainda sem implementacao (E_NOTIMPL)");return int32_t(0x80004001);
}
extern "C" int32_t win_CoMarshalInterThreadInterfaceInStream(const void*,void*,void** stream) {
    if(stream)*stream=nullptr;
    once("CoMarshalInterThreadInterfaceInStream ainda sem implementacao (E_NOTIMPL)");return int32_t(0x80004001);
}
extern "C" int32_t win_CoGetInterfaceAndReleaseStream(void*,const void*,void** out) {
    if(out)*out=nullptr;
    once("CoGetInterfaceAndReleaseStream ainda sem implementacao (E_NOTIMPL)");return int32_t(0x80004001);
}

// Azimuth 1.0.2's report of the modules in its process (who loaded what, and
// from where) at launch: the engine, then the DLLs the host mapped. Paths of
// mapped files are named by device as Windows does, \Device\HarddiskVolumeN
// for drive letter N (A = 1), which QueryDosDeviceW maps back.
namespace {
std::u16string deviceOf(char16_t letter){return u"\\Device\\HarddiskVolume"+utf16(std::to_string(letter-u'A'+1));}
uint32_t copyOut(const std::u16string& text,uint16_t* buffer,uint32_t size) {
    if(!buffer||size<=text.size()){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(buffer,text.c_str(),(text.size()+1)*2);
    return uint32_t(text.size());
}
}
extern "C" int32_t win_K32EnumProcessModules(void* process,void** modules,uint32_t size,uint32_t* needed) {
    if(reinterpret_cast<intptr_t>(process)!=-1||!needed){setLastError(ErrorInvalidParameter);return 0;}
    std::vector<uint64_t> bases{azimuth::win32::process().imageBase};
    for(auto base:azimuth::moduleBases())bases.push_back(base);
    *needed=uint32_t(bases.size()*8);
    for(size_t n=0;n<bases.size()&&(n+1)*8<=size;++n)modules[n]=reinterpret_cast<void*>(bases[n]);
    return 1;
}
extern "C" uint32_t win_K32GetMappedFileNameW(void* process,void* address,uint16_t* buffer,uint32_t size) {
    constexpr uint32_t ErrorFileInvalid=1006;
    if(reinterpret_cast<intptr_t>(process)!=-1){setLastError(ErrorInvalidHandle);return 0;}
    auto at=reinterpret_cast<uint64_t>(address);auto const& p=azimuth::win32::process();
    std::u16string path;
    if(at>=p.imageBase&&at-p.imageBase<p.imageSize)path=p.modulePath;
    else if(auto base=azimuth::moduleContaining(at))path=utf16(azimuth::moduleWindowsPath(base));
    if(path.size()<3||path[1]!=u':'){setLastError(ErrorFileInvalid);return 0;}
    char16_t letter=path[0]>=u'a'&&path[0]<=u'z'?char16_t(path[0]-32):path[0];
    return copyOut(deviceOf(letter)+path.substr(2),buffer,size);
}
// "X:" for drive C (the system's) and the drives the host serves; with no
// name, the list of them. The answer is a list ending in two nulls.
extern "C" uint32_t win_QueryDosDeviceW(const uint16_t* name,uint16_t* buffer,uint32_t size) {
    std::u16string drives=u"C";
    for(auto const& m:azimuth::win32::process().mounts)
        if(m.guest.size()>=2&&m.guest[1]==u':'&&drives.find(m.guest[0])==std::u16string::npos)drives+=m.guest[0];
    std::u16string out;
    if(!name){for(auto d:drives){out+=d;out+=u':';out+=u'\0';}}
    else {
        std::u16string text(reinterpret_cast<const char16_t*>(name));
        char16_t letter=text.size()==2&&text[1]==u':'?(text[0]>=u'a'&&text[0]<=u'z'?char16_t(text[0]-32):text[0]):0;
        if(!letter||drives.find(letter)==std::u16string::npos){setLastError(ErrorFileNotFound);return 0;}
        out=deviceOf(letter)+u'\0';
    }
    out+=u'\0';
    if(!buffer||size<out.size()){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(buffer,out.data(),out.size()*2);
    return uint32_t(out.size());
}
extern "C" uint32_t win_GetSystemWindowsDirectoryW(uint16_t* buffer,uint32_t size) {
    static const char16_t directory[]=u"C:\\windows";
    constexpr uint32_t needed=sizeof directory/2;
    if(size<needed)return needed;
    std::memcpy(buffer,directory,sizeof directory);
    return needed-1;
}
// Azimuth 1.0.2 starts itself again on the way back to the launcher; a title
// has no second process. It logs the error and the session ends.
extern "C" int32_t win_CreateProcessW(const uint16_t* application,uint16_t* command,void*,void*,int32_t,uint32_t,void*,const uint16_t*,void*,void*) {
    log("CreateProcessW: o titulo nao inicia outro processo ("+(application?utf8(application,length(application)):std::string("sem aplicativo"))+
        (command?", "+utf8(command,length(command)):std::string())+")");
    setLastError(ErrorNotSupported);return 0;
}
