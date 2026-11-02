// D3D12 for the engine, which imports D3D12CreateDevice and
// D3D12GetDebugInterface by ordinal (main.cpp maps the ordinals). The device
// is vkd3d-proton's: its d3d12.dll, loaded by the host (Modules.cpp), creates
// it over the host's DXGI adapter and vulkan-1.dll. There is no debug layer.
#include "Win32.h"
#include "../Modules.h"
#include "../gpu/Dxgi.h"
#include "../guest_abi.h"
#include <cstdio>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
constexpr int32_t ENoInterface=int32_t(0x80004002),EPointer=int32_t(0x80004003),DxgiErrorUnsupported=int32_t(0x887a0004);
std::string guid(const void* id) {
    if(!id)return "(nulo)";
    auto b=static_cast<const uint8_t*>(id);char s[40];
    std::snprintf(s,sizeof s,"{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  b[3],b[2],b[1],b[0],b[5],b[4],b[7],b[6],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
    return s;
}
}

extern "C" int32_t win_D3D12GetDebugInterface(const void* id,void** out) {
    if(!out)return EPointer;
    *out=nullptr;
    log("D3D12GetDebugInterface "+guid(id)+": sem camada de debug no host (E_NOINTERFACE)");
    return ENoInterface;
}
// The host's DXGI (gpu/Dxgi.cpp): a factory with the Vulkan driver's device.
extern "C" int32_t win_CreateDXGIFactory2(uint32_t flags,const void* id,void** out) {
    if(flags)log("CreateDXGIFactory2 flags="+std::to_string(flags)+" (debug ignorado)");
    return azimuth::gpu::createDxgiFactory(id,out);
}
extern "C" int32_t win_D3D12CreateDevice(void* adapter,uint32_t level,const void* id,void** out) {
    if(out)*out=nullptr;
    auto base=azimuth::loadModule("d3d12");
    auto create=base?azimuth::moduleExport(base,"D3D12CreateDevice"):nullptr;
    char text[160];
    std::snprintf(text,sizeof text,"D3D12CreateDevice do motor: FL 0x%x %s adaptador %p",level,out?"":"(so verificacao)",adapter);
    if(!create) {
        log(std::string(text)+": d3d12.dll do vkd3d-proton indisponivel (DXGI_ERROR_UNSUPPORTED)");
        return DxgiErrorUnsupported;
    }
    auto result=int32_t(guestCall8(create,reinterpret_cast<uint64_t>(adapter),level,reinterpret_cast<uint64_t>(id),reinterpret_cast<uint64_t>(out),0,0,0,0));
    char done[40];std::snprintf(done,sizeof done,": 0x%08x",unsigned(uint32_t(result)));
    log(std::string(text)+" "+guid(id)+done);
    return result;
}
extern "C" int32_t win_CreateDXGIFactory1(const void* id,void** out){return azimuth::gpu::createDxgiFactory(id,out);}

// The engine builds its root signatures with this one; vkd3d-proton's d3d12.dll
// serializes them (the blob it returns is the one D3D12 devices read back).
extern "C" int32_t win_D3D12SerializeRootSignature(const void* desc,uint32_t version,void** blob,void** errors) {
    if(blob)*blob=nullptr;
    if(errors)*errors=nullptr;
    auto base=azimuth::loadModule("d3d12");
    auto serialize=base?azimuth::moduleExport(base,"D3D12SerializeRootSignature"):nullptr;
    if(!serialize) {
        log("D3D12SerializeRootSignature do motor: d3d12.dll do vkd3d-proton indisponivel (DXGI_ERROR_UNSUPPORTED)");
        return DxgiErrorUnsupported;
    }
    auto result=int32_t(guestCall8(serialize,reinterpret_cast<uint64_t>(desc),version,reinterpret_cast<uint64_t>(blob),reinterpret_cast<uint64_t>(errors),0,0,0,0));
    static unsigned reports=0;
    if(reports++<6||result<0)log("D3D12SerializeRootSignature do motor: versao "+std::to_string(version)+" -> 0x"+[&]{char t[16];std::snprintf(t,sizeof t,"%08x",unsigned(uint32_t(result)));return std::string(t);}());
    return result;
}
