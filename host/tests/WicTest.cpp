// WIC check (title 0.95): the game's sequence through the Windows x64
// vtables (wic_abi.c) on a PNG the host writes: factory, stream from memory,
// decoder, frame (size, format, pixels), and a converter to BGRA.
#include "../src/gfx/Png.h"
#include "../src/guest_abi.h"
#include "../src/win32/Win32.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT
namespace azimuth::win32 {
void log(const std::string& text){std::printf("log: %s\n",text.c_str());}
void stopGuest(const std::string& reason){std::printf("STOP: %s\n",reason.c_str());std::exit(2);}
}

namespace {
int failures=0;
void check(bool ok,const std::string& what){std::printf("%s %s\n",ok?"ok  ":"FALHOU",what.c_str());if(!ok)++failures;}
uint64_t u(const void* p){return reinterpret_cast<uint64_t>(p);}
uint64_t call(void* object,unsigned slot,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0,uint64_t f=0,uint64_t g=0) {
    auto vtable=*static_cast<void* const* const*>(object);
    return guestCall8(vtable[slot],u(object),a,b,c,d,e,f,g);
}
bool ok(uint64_t hr){return int32_t(uint32_t(hr))>=0;}
const uint8_t Rgba[16]={0x2d,0xad,0xc7,0xf5,0x8d,0x6a,0xdd,0x43,0xa7,0xa8,0xa2,0x99,0x35,0x26,0x1a,0xe9};
const uint8_t Bgra[16]={0x24,0xc3,0xdd,0x6f,0x03,0x4e,0xfe,0x4b,0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0f};
}

int main() {
    // 3x2 B,G,R,X pixels: red, green, blue / white, black, grey.
    std::vector<uint32_t> pixels={0x00ff0000,0x0000ff00,0x000000ff,0x00ffffff,0x00000000,0x00808080};
    const char* path="/tmp/azimuth-wic-test.png";
    check(azimuth::gfx::savePng(path,pixels,3,2),"PNG de teste gravado");
    std::ifstream in(path,std::ios::binary);
    std::vector<uint8_t> png((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());

    // The engine's load of a desktop dependency for the game (title 0.95):
    // LoadLibraryExW, then the module "containing" that handle, then GetProcAddress on it.
    const char16_t name[]=u"windowscodecs.dll";
    void* library=win_LoadLibraryExW(reinterpret_cast<const uint16_t*>(name),nullptr,0);
    check(library!=nullptr,"LoadLibraryExW(windowscodecs.dll)");
    void* containing=nullptr;
    check(win_GetModuleHandleExW(4,static_cast<const uint16_t*>(library),&containing)&&containing==library,"GetModuleHandleExW(FROM_ADDRESS) devolve o mesmo modulo");
    check(win_GetProcAddress(containing,"WICCreateImagingFactory_Proxy")!=nullptr,"GetProcAddress(WICCreateImagingFactory_Proxy)");

    void* factory=nullptr;
    check(ok(uint64_t(uint32_t(win_WICCreateImagingFactory_Proxy(0x237,&factory))))&&factory,"WICCreateImagingFactory_Proxy");
    void* stream=nullptr;
    check(ok(call(factory,14,u(&stream)))&&stream,"CreateStream");
    check(ok(call(stream,16,u(png.data()),png.size())),"InitializeFromMemory");
    void* decoder=nullptr;
    check(ok(call(factory,4,u(stream),0,0,u(&decoder)))&&decoder,"CreateDecoderFromStream");
    void* frame=nullptr;
    check(ok(call(decoder,13,0,u(&frame)))&&frame,"GetFrame(0)");
    uint32_t w=0,h=0;
    check(ok(call(frame,3,u(&w),u(&h)))&&w==3&&h==2,"GetSize 3x2 ("+std::to_string(w)+"x"+std::to_string(h)+")");
    uint8_t format[16]={};
    check(ok(call(frame,4,u(format)))&&!std::memcmp(format,Rgba,16),"GetPixelFormat 32bppRGBA");
    uint8_t rgba[24]={};
    check(ok(call(frame,7,0,12,sizeof rgba,u(rgba))),"CopyPixels");
    check(rgba[0]==255&&rgba[1]==0&&rgba[2]==0&&rgba[3]==255&&rgba[4]==0&&rgba[5]==255&&rgba[8+2]==255&&rgba[12+20-12]==128,"pixels RGBA");
    void* converter=nullptr;
    check(ok(call(factory,10,u(&converter)))&&converter,"CreateFormatConverter");
    // Initialize(source, format, dither, palette, alpha (double 0.0), palette type).
    check(ok(call(converter,8,u(frame),u(Bgra),0,0,0,0)),"Initialize para 32bppBGRA");
    uint8_t bgra[24]={};
    check(ok(call(converter,7,0,12,sizeof bgra,u(bgra))),"CopyPixels do conversor");
    check(bgra[0]==0&&bgra[2]==255&&bgra[4]==0&&bgra[5]==255&&bgra[8]==255&&bgra[10]==0,"pixels BGRA");
    for(void* o:{converter,frame,decoder,stream,factory})call(o,2);
    std::printf(failures?"FALHOU: %d verificacoes de WIC\n":"OK: WIC (fabrica, stream, decodificador, quadro e conversor)\n",failures);
    return failures?1:0;
}
