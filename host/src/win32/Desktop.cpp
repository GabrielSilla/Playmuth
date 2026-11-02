// The desktop libraries the engine loads as the game starts, beyond the
// Visual C++ runtime it ships in data\bin (title 0.60): what it takes from
// each (its "desktop dependencies"). Winsock's events are plain events;
// Media Foundation starts but makes no media, and the Windows Runtime has no
// classes: callers get the failure Windows gives for a missing component,
// and the log names what was asked for.
#include "Win32.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
constexpr int32_t NotImplemented=int32_t(0x80004001),ClassNotRegistered=int32_t(0x80040154);
}

// ---- ws2_32: WSAEVENTs are manual-reset events ---------------------------
extern "C" void* win_WSACreateEvent(void){return win_CreateEventW(nullptr,1,0,nullptr);}
extern "C" int32_t win_WSACloseEvent(void* event){return win_CloseHandle(event);}
extern "C" int32_t win_WSASetEvent(void* event){return win_SetEvent(event);}
extern "C" int32_t win_WSAResetEvent(void* event){return win_ResetEvent(event);}
// WSA_WAIT_EVENT_0 and WSA_WAIT_TIMEOUT have WAIT_OBJECT_0's and WAIT_TIMEOUT's values.
extern "C" uint32_t win_WSAWaitForMultipleEvents(uint32_t count,void* const* events,int32_t all,uint32_t milliseconds,int32_t alertable) {
    return win_WaitForMultipleObjectsEx(count,events,all,milliseconds,alertable);
}

// Winsock starts with no network: version 2.2, and the byte-order and IPv4
// text helpers the game's code is bound to. The error is the thread's last error.
// WSADATA (x64, 408 bytes): versions, max sockets and datagram size, vendor
// pointer at 8, description at 16, status at 273.
extern "C" int32_t win_WSAStartup(uint16_t,void* data) {
    if(!data)return 10014;   // WSAEFAULT
    auto p=static_cast<uint8_t*>(data);std::memset(p,0,408);
    uint16_t fields[4]={0x0202,0x0202,0,0};std::memcpy(p,fields,8);
    std::strcpy(reinterpret_cast<char*>(p+16),"WinSock 2.0");std::strcpy(reinterpret_cast<char*>(p+273),"Running");
    return 0;
}
extern "C" int32_t win_WSACleanup(void){return 0;}
extern "C" int32_t win_WSAGetLastError(void){return int32_t(win_GetLastError());}
extern "C" void win_WSASetLastError(int32_t error){setLastError(uint32_t(error));}
extern "C" uint32_t win_htonl(uint32_t value){return __builtin_bswap32(value);}
extern "C" uint16_t win_htons(uint16_t value){return __builtin_bswap16(value);}
extern "C" uint32_t win_ntohl(uint32_t value){return __builtin_bswap32(value);}
extern "C" uint16_t win_ntohs(uint16_t value){return __builtin_bswap16(value);}
// "a.b.c.d" to an address in network order; INADDR_NONE otherwise.
extern "C" uint32_t win_inet_addr(const char* text) {
    unsigned part[4];char end;
    if(!text||std::sscanf(text,"%u.%u.%u.%u%c",&part[0],&part[1],&part[2],&part[3],&end)!=4)return 0xffffffff;
    for(unsigned n:part)if(n>255)return 0xffffffff;
    return part[0]|(part[1]<<8)|(part[2]<<16)|(part[3]<<24);
}
extern "C" char* win_inet_ntoa(uint32_t address) {
    static thread_local char text[16];
    std::snprintf(text,sizeof text,"%u.%u.%u.%u",address&0xff,(address>>8)&0xff,(address>>16)&0xff,address>>24);
    return text;
}

// ---- Media Foundation ------------------------------------------------------
extern "C" int32_t win_MFStartup(uint32_t,uint32_t){log("MFStartup: Media Foundation sem decodificadores no host");return 0;}
extern "C" int32_t win_MFShutdown(void){return 0;}
extern "C" int32_t win_MFCreateMediaType(void** type) {
    if(type)*type=nullptr;
    log("MFCreateMediaType ainda sem implementacao");return NotImplemented;
}
extern "C" int32_t win_MFInitMediaTypeFromWaveFormatEx(void*,const void*,uint32_t) {
    log("MFInitMediaTypeFromWaveFormatEx ainda sem implementacao");return NotImplemented;
}
extern "C" int32_t win_MFCreateSourceReaderFromURL(const uint16_t* url,void*,void** reader) {
    if(reader)*reader=nullptr;
    log("MFCreateSourceReaderFromURL ainda sem implementacao: "+(url?utf8(url,length(url)):std::string("(nulo)")));
    return NotImplemented;
}

// ---- vccorlib110: GetActivationFactoryByPCWSTR(PCWSTR class, Guid&, void**) --
extern "C" int32_t win_vccorlibGetActivationFactoryByPCWSTR(const uint16_t* name,const void*,void** factory) {
    if(factory)*factory=nullptr;
    log("Windows Runtime: classe "+(name?utf8(name,length(name)):std::string("(nula)"))+" nao registrada no host");
    return ClassNotRegistered;
}
