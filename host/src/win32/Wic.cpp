// windowscodecs' WIC for the game (title 0.94: it loads WindowsCodecs.dll for
// WICCreateImagingFactory_Proxy and decodes images from memory: a stream,
// CreateDecoderFromStream, the first frame, then CopyPixels or a format
// converter to one of its 32-bit formats). Images are decoded by stb_image
// (PNG, JPEG, BMP, TGA, GIF) into 32bpp RGBA; the converter gives RGBA, BGRA,
// BGRX and their premultiplied forms. The vtables are in wic_abi.c.
#include "WicAbi.h"
#include "Win32.h"
#include "../HostHeap.h"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

// stb_image, with the host heap (the title's malloc is small).
namespace {
void* stbRealloc(void* block,size_t old,size_t size) {
    void* fresh=azimuth::heapAlloc(size,16,false);
    if(fresh&&block)std::memcpy(fresh,block,old<size?old:size);
    if(block)azimuth::heapFree(block);
    return fresh;
}
}
#define STBI_MALLOC(size) azimuth::heapAlloc(size,16,false)
#define STBI_REALLOC_SIZED(block,old,size) stbRealloc(block,old,size)
#define STBI_FREE(block) azimuth::heapFree(block)
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif

using azimuth::win32::log;

namespace {
constexpr int32_t Ok=0,False=1,NotImplemented=int32_t(0x80004001),NoInterface=int32_t(0x80004002),Pointer=int32_t(0x80004003),
                  InvalidArgument=int32_t(0x80070057),
                  WrongState=int32_t(0x88982f04),UnknownImageFormat=int32_t(0x88982f07),NotInitialized=int32_t(0x88982f0c),
                  UnsupportedPixelFormat=int32_t(0x88982f80),ComponentNotFound=int32_t(0x88982f50);
struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
constexpr Guid Unknown{0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
// The classes CoCreateInstance gives the factory for: CLSID_WICImagingFactory
// and CLSID_WICImagingFactory2 (the 1.0.2 engine, title 1.42).
constexpr Guid FactoryClasses[]={
    {0xcacaf262,0x9370,0x4615,{0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a}},
    {0x317d06e8,0x5f24,0x433d,{0xbd,0xf7,0x79,0xce,0x68,0xd8,0xab,0xc2}},
};
constexpr Guid FactoryIds[]={
    {0xec5ec8a9,0xc395,0x4314,{0x9c,0x77,0x54,0xd7,0xa9,0x35,0xff,0x70}},   // IWICImagingFactory
};
constexpr Guid StreamIds[]={
    {0x135ff860,0x22b7,0x4ddf,{0xb0,0xf6,0x21,0x8f,0x4f,0x29,0x9a,0x43}},   // IWICStream
    {0x0000000c,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}},   // IStream
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0xaa,0x00,0x44,0x77,0x3d}},   // ISequentialStream
};
constexpr Guid DecoderId{0x9edde9e7,0x8dee,0x47ea,{0x99,0xdf,0xe6,0xfa,0xf2,0xed,0x44,0xbf}};
constexpr Guid SourceId{0x00000120,0xa8f2,0x4877,{0xba,0x0a,0xfd,0x2b,0x66,0x45,0xfb,0x94}};
constexpr Guid FrameId{0x3b16811b,0x6a43,0x4ec9,{0xa8,0x13,0x3d,0x93,0x0c,0x13,0xb9,0x40}};
constexpr Guid ConverterId{0x00000301,0xa8f2,0x4877,{0xba,0x0a,0xfd,0x2b,0x66,0x45,0xfb,0x94}};
// Pixel formats the host gives.
constexpr Guid Rgba{0xf5c7ad2d,0x6a8d,0x43dd,{0xa7,0xa8,0xa2,0x99,0x35,0x26,0x1a,0xe9}};
constexpr Guid Prgba{0x3cc4a650,0xa527,0x4d37,{0xa9,0x16,0x31,0x42,0xc7,0xeb,0xed,0xba}};
constexpr Guid Bgra{0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0f}};
constexpr Guid Pbgra{0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x10}};
constexpr Guid Bgrx{0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0e}};   // 32bppBGR
constexpr Guid Rgbx{0xd98c6b95,0x3efe,0x47d6,{0xbb,0x25,0xeb,0x17,0x48,0xab,0x0c,0xf1}};   // 32bppRGB
// Containers, for GetContainerFormat.
constexpr Guid PngContainer{0x1b7cfaf4,0x713f,0x473c,{0xbb,0xcd,0x61,0x37,0x42,0x5f,0xae,0xaf}};
constexpr Guid JpegContainer{0x19e4a5aa,0x5662,0x4fc5,{0xa0,0xc0,0x17,0x58,0x02,0x8e,0x10,0x57}};
constexpr Guid BmpContainer{0x0af1d87e,0xfcfe,0x4188,{0xbd,0xeb,0xa7,0x90,0x64,0x71,0xcb,0xe3}};
constexpr Guid GifContainer{0x1f8a5601,0x7d4d,0x4cbd,{0x9c,0x82,0x1b,0xc8,0xd4,0xee,0xb9,0xa5}};
constexpr Guid TgaContainer{0x00000000,0x0000,0x0000,{0,0,0,0,0,0,0,0}};   // WIC has no TGA container: GUID_NULL

bool same(const void* a,const Guid& b){return !std::memcmp(a,&b,sizeof b);}
std::string text(const void* p) {
    Guid g;std::memcpy(&g,p,sizeof g);char s[40];
    std::snprintf(s,sizeof s,"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.a,g.b,g.c,g.d[0],g.d[1],g.d[2],g.d[3],g.d[4],g.d[5],g.d[6],g.d[7]);
    return s;
}
std::mutex reportLock;
std::set<std::string> reported;
void reportOnce(const std::string& line){std::lock_guard<std::mutex> guard(reportLock);if(reported.insert(line).second)log(line);}

// A decoded picture: rows of RGBA, top-down.
struct Picture {
    uint32_t width=0,height=0;std::vector<uint8_t> rgba;const Guid* container=&PngContainer;
};
// The pixel formats a source can give, and how one RGBA pixel becomes it.
enum class Out {Rgba,Prgba,Bgra,Pbgra,Bgrx,Rgbx};
bool outFormat(const void* id,Out& out) {
    if(same(id,Rgba))out=Out::Rgba;else if(same(id,Prgba))out=Out::Prgba;else if(same(id,Bgra))out=Out::Bgra;
    else if(same(id,Pbgra))out=Out::Pbgra;else if(same(id,Bgrx))out=Out::Bgrx;else if(same(id,Rgbx))out=Out::Rgbx;else return false;
    return true;
}
const Guid& formatId(Out o) {
    switch(o){case Out::Rgba:return Rgba;case Out::Prgba:return Prgba;case Out::Bgra:return Bgra;case Out::Pbgra:return Pbgra;case Out::Bgrx:return Bgrx;default:return Rgbx;}
}
void convert(const uint8_t* s,uint8_t* d,Out o) {
    uint8_t r=s[0],g=s[1],b=s[2],a=s[3];
    if(o==Out::Prgba||o==Out::Pbgra){r=uint8_t((r*a+127)/255);g=uint8_t((g*a+127)/255);b=uint8_t((b*a+127)/255);}
    bool swap=o==Out::Bgra||o==Out::Pbgra||o==Out::Bgrx;
    d[0]=swap?b:r;d[1]=g;d[2]=swap?r:b;d[3]=(o==Out::Bgrx||o==Out::Rgbx)?255:a;
}

// The COM layout: the vtable pointer first (no C++ virtual functions here).
struct Object {const void* const* vtable;std::atomic<uint32_t> references{1};};
struct Stream:Object {const uint8_t* data=nullptr;uint64_t size=0,at=0;bool ready=false;std::vector<uint8_t> owned;};
struct Decoder:Object {std::shared_ptr<Picture> picture;};
// A frame and a converter are both a picture seen in one pixel format.
struct Source:Object {std::shared_ptr<Picture> picture;Out format=Out::Rgba;};

Object* object(void* self){return static_cast<Object*>(self);}
bool isSource(void* p){return p&&(object(p)->vtable==guestWicFrameVtable||object(p)->vtable==guestWicConverterVtable);}
void destroy(Object* o) {
    auto v=o->vtable;
    if(v==guestWicStreamVtable)delete static_cast<Stream*>(o);
    else if(v==guestWicDecoderVtable)delete static_cast<Decoder*>(o);
    else if(v==guestWicFrameVtable||v==guestWicConverterVtable)delete static_cast<Source*>(o);
    else delete o;
}
bool supports(Object* o,const void* want) {
    if(same(want,Unknown))return true;
    auto v=o->vtable;
    if(v==guestWicFactoryVtable){for(auto& g:FactoryIds)if(same(want,g))return true;return false;}
    if(v==guestWicStreamVtable){for(auto& g:StreamIds)if(same(want,g))return true;return false;}
    if(v==guestWicDecoderVtable)return same(want,DecoderId);
    if(v==guestWicFrameVtable)return same(want,FrameId)||same(want,SourceId);
    return v==guestWicConverterVtable&&(same(want,ConverterId)||same(want,SourceId));
}

// Decodes what a stream holds (title 0.94: images from memory).
int32_t decode(Stream* s,std::shared_ptr<Picture>& out) {
    if(!s->ready)return NotInitialized;
    if(s->size>uint64_t(INT32_MAX))return UnknownImageFormat;
    int w=0,h=0,channels=0;
    uint8_t* pixels=stbi_load_from_memory(s->data,int(s->size),&w,&h,&channels,4);
    if(!pixels) {
        char magic[64];const uint8_t* m=s->data;
        std::snprintf(magic,sizeof magic,"%02x %02x %02x %02x %02x %02x %02x %02x",s->size>0?m[0]:0,s->size>1?m[1]:0,s->size>2?m[2]:0,
                      s->size>3?m[3]:0,s->size>4?m[4]:0,s->size>5?m[5]:0,s->size>6?m[6]:0,s->size>7?m[7]:0);
        static std::atomic<unsigned> reports{0};
        if(reports++<20)log("WIC: imagem de "+std::to_string(s->size)+" bytes nao decodificada ("+stbi_failure_reason()+"), inicio "+magic);
        return ComponentNotFound;
    }
    auto p=std::make_shared<Picture>();
    p->width=uint32_t(w);p->height=uint32_t(h);
    p->rgba.assign(pixels,pixels+size_t(w)*size_t(h)*4);
    stbi_image_free(pixels);
    const uint8_t* m=s->data;
    if(s->size>=2&&m[0]==0xff&&m[1]==0xd8)p->container=&JpegContainer;
    else if(s->size>=2&&m[0]=='B'&&m[1]=='M')p->container=&BmpContainer;
    else if(s->size>=3&&m[0]=='G'&&m[1]=='I'&&m[2]=='F')p->container=&GifContainer;
    else if(s->size>=4&&m[0]==0x89&&m[1]=='P')p->container=&PngContainer;
    else p->container=&TgaContainer;
    static std::atomic<unsigned> decoded{0};
    if(decoded++<10)log("WIC: imagem "+std::to_string(w)+"x"+std::to_string(h)+" decodificada ("+std::to_string(channels)+" canais, "+std::to_string(s->size)+" bytes)");
    out=p;return Ok;
}
}

extern "C" {
// windowscodecs' exported entry point: the factory.
int32_t win_WICCreateImagingFactory_Proxy(uint32_t,void** factory) {
    if(!factory)return Pointer;
    auto f=new Object;f->vtable=guestWicFactoryVtable;
    *factory=f;
    reportOnce("WIC: fabrica criada (PNG, JPEG, BMP, TGA e GIF pelo stb_image)");
    return Ok;
}
int32_t wicMissing(const char* object,uint32_t slot) {
    reportOnce(std::string("WIC: metodo ")+object+" #"+std::to_string(slot)+" ainda sem implementacao (E_NOTIMPL)");
    return NotImplemented;
}
int32_t wic_QueryInterface(void* self,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    auto o=object(self);
    if(!supports(o,want)){reportOnce("WIC: objeto sem a interface "+text(want));return NoInterface;}
    ++o->references;*out=o;return Ok;
}
uint32_t wic_AddRef(void* self){return ++object(self)->references;}
uint32_t wic_Release(void* self) {
    auto o=object(self);
    uint32_t left=--o->references;
    if(!left)destroy(o);
    return left;
}

// IWICImagingFactory
int32_t wic_CreateStream(void*,void** stream) {
    if(!stream)return Pointer;
    auto s=new Stream;s->vtable=guestWicStreamVtable;*stream=s;return Ok;
}
int32_t wic_CreateDecoderFromStream(void*,void* stream,const void*,uint32_t,void** decoder) {
    if(!decoder)return Pointer;
    *decoder=nullptr;
    if(!stream)return InvalidArgument;
    if(object(stream)->vtable!=guestWicStreamVtable){reportOnce("WIC: CreateDecoderFromStream de um IStream que nao e do host");return ComponentNotFound;}
    std::shared_ptr<Picture> picture;
    if(int32_t result=decode(static_cast<Stream*>(stream),picture);result<0)return result;
    auto d=new Decoder;d->vtable=guestWicDecoderVtable;d->picture=picture;
    *decoder=d;return Ok;
}
// The 1.0.2 engine's images from files (data\bin\azimuth-mark.png): the
// whole file read into a stream the decoder owns.
int32_t wic_CreateDecoderFromFilename(void*,const uint16_t* name,const void*,uint32_t,uint32_t,void** decoder) {
    if(!decoder)return Pointer;
    *decoder=nullptr;
    if(!name)return InvalidArgument;
    auto guest=azimuth::win32::fullPath(azimuth::win32::fromGuest(name));
    auto host=azimuth::win32::hostPath(guest);
    int fd=host.empty()?-1: ::open(host.c_str(),O_RDONLY);
    if(fd<0) {
        int code=errno;
        log("WIC: CreateDecoderFromFilename de "+azimuth::win32::utf8(reinterpret_cast<const uint16_t*>(guest.data()),guest.size())+": arquivo nao abriu (errno "+std::to_string(code)+")");
        return code==ENOENT?int32_t(0x80070002):int32_t(0x80070005);
    }
    Stream file;file.vtable=guestWicStreamVtable;
    uint8_t chunk[65536];
    for(ssize_t n;(n=::read(fd,chunk,sizeof chunk))>0;)file.owned.insert(file.owned.end(),chunk,chunk+n);
    ::close(fd);
    file.data=file.owned.data();file.size=file.owned.size();file.ready=true;
    std::shared_ptr<Picture> picture;
    if(int32_t result=decode(&file,picture);result<0)return result;
    auto d=new Decoder;d->vtable=guestWicDecoderVtable;d->picture=picture;
    *decoder=d;return Ok;
}
int32_t wic_CreateFormatConverter(void*,void** converter) {
    if(!converter)return Pointer;
    auto c=new Source;c->vtable=guestWicConverterVtable;*converter=c;return Ok;
}

// IWICStream over memory the caller keeps.
int32_t wic_InitializeFromMemory(void* self,uint8_t* data,uint32_t size) {
    auto s=static_cast<Stream*>(self);
    if(!data)return InvalidArgument;
    if(s->ready)return WrongState;
    s->data=data;s->size=size;s->at=0;s->ready=true;return Ok;
}
int32_t wic_StreamRead(void* self,void* data,uint32_t size,uint32_t* done) {
    auto s=static_cast<Stream*>(self);
    if(!data)return Pointer;
    if(!s->ready)return NotInitialized;
    uint64_t n=s->at<s->size?std::min<uint64_t>(size,s->size-s->at):0;
    std::memcpy(data,s->data+s->at,size_t(n));s->at+=n;
    if(done)*done=uint32_t(n);
    return n==size?Ok:False;
}
int32_t wic_StreamSeek(void* self,int64_t move,uint32_t origin,uint64_t* position) {
    auto s=static_cast<Stream*>(self);
    if(!s->ready)return NotInitialized;
    int64_t base=origin==0?0:origin==1?int64_t(s->at):origin==2?int64_t(s->size):-1;
    if(base<0||base+move<0)return InvalidArgument;
    s->at=uint64_t(base+move);
    if(position)*position=s->at;
    return Ok;
}
// STATSTG: pwcsName, type, cbSize at 16.
int32_t wic_StreamStat(void* self,void* stat,uint32_t) {
    auto s=static_cast<Stream*>(self);
    if(!stat)return Pointer;
    std::memset(stat,0,80);
    uint32_t type=2;std::memcpy(static_cast<uint8_t*>(stat)+8,&type,4);   // STGTY_STREAM
    std::memcpy(static_cast<uint8_t*>(stat)+16,&s->size,8);
    return Ok;
}

// IWICBitmapDecoder: one frame.
int32_t wic_QueryCapability(void*,void*,uint32_t* capability){if(!capability)return Pointer;*capability=0x4;return Ok;}   // CanDecodeAllImages
int32_t wic_DecoderInitialize(void* self,void* stream,uint32_t) {
    auto d=static_cast<Decoder*>(self);
    if(d->picture)return WrongState;
    if(!stream||object(stream)->vtable!=guestWicStreamVtable)return InvalidArgument;
    return decode(static_cast<Stream*>(stream),d->picture);
}
int32_t wic_GetContainerFormat(void* self,void* format) {
    auto d=static_cast<Decoder*>(self);
    if(!format)return Pointer;
    if(!d->picture)return NotInitialized;
    std::memcpy(format,d->picture->container,16);return Ok;
}
int32_t wic_DecoderGetColorContexts(void*,uint32_t,void**,uint32_t* actual){if(!actual)return Pointer;*actual=0;return Ok;}
int32_t wic_GetFrameCount(void* self,uint32_t* count) {
    if(!count)return Pointer;
    *count=static_cast<Decoder*>(self)->picture?1:0;return Ok;
}
int32_t wic_GetFrame(void* self,uint32_t index,void** frame) {
    if(!frame)return Pointer;
    *frame=nullptr;
    auto d=static_cast<Decoder*>(self);
    if(!d->picture)return NotInitialized;
    if(index!=0)return InvalidArgument;
    auto f=new Source;f->vtable=guestWicFrameVtable;f->picture=d->picture;f->format=Out::Rgba;
    *frame=f;return Ok;
}

// IWICBitmapSource (frame and converter)
int32_t wic_GetSize(void* self,uint32_t* width,uint32_t* height) {
    auto s=static_cast<Source*>(self);
    if(!width||!height)return Pointer;
    if(!s->picture)return NotInitialized;
    *width=s->picture->width;*height=s->picture->height;return Ok;
}
int32_t wic_GetPixelFormat(void* self,void* format) {
    auto s=static_cast<Source*>(self);
    if(!format)return Pointer;
    if(!s->picture)return NotInitialized;
    std::memcpy(format,&formatId(s->format),16);return Ok;
}
int32_t wic_GetResolution(void*,double* x,double* y){if(!x||!y)return Pointer;*x=96;*y=96;return Ok;}
// WICRect: X, Y, Width, Height (INT).
int32_t wic_CopyPixels(void* self,const void* rect,uint32_t stride,uint32_t size,uint8_t* buffer) {
    auto s=static_cast<Source*>(self);
    if(!buffer)return Pointer;
    if(!s->picture)return NotInitialized;
    auto& p=*s->picture;
    int32_t x=0,y=0,w=int32_t(p.width),h=int32_t(p.height);
    if(rect){int32_t r[4];std::memcpy(r,rect,16);x=r[0];y=r[1];w=r[2];h=r[3];}
    if(x<0||y<0||w<0||h<0||uint32_t(x+w)>p.width||uint32_t(y+h)>p.height)return InvalidArgument;
    if(stride<uint32_t(w)*4||(h&&uint64_t(stride)*uint64_t(h-1)+uint64_t(w)*4>size))return InvalidArgument;
    for(int32_t row=0;row<h;++row) {
        const uint8_t* from=p.rgba.data()+(size_t(y+row)*p.width+size_t(x))*4;
        uint8_t* to=buffer+size_t(row)*stride;
        if(s->format==Out::Rgba)std::memcpy(to,from,size_t(w)*4);
        else for(int32_t col=0;col<w;++col)convert(from+4*col,to+4*col,s->format);
    }
    return Ok;
}
int32_t wic_FrameGetColorContexts(void*,uint32_t,void**,uint32_t* actual){if(!actual)return Pointer;*actual=0;return Ok;}

// IWICFormatConverter: a source of the host's in another of its formats.
int32_t wic_ConverterInitialize(void* self,void* source,const void* format,uint32_t,void*,double,uint32_t) {
    auto c=static_cast<Source*>(self);
    if(!source||!format)return InvalidArgument;
    if(c->picture)return WrongState;
    if(!isSource(source)){reportOnce("WIC: conversor sobre uma fonte que nao e do host");return UnsupportedPixelFormat;}
    Out out;
    if(!outFormat(format,out)){reportOnce("WIC: conversao para "+text(format)+" nao suportada");return UnsupportedPixelFormat;}
    c->picture=static_cast<Source*>(source)->picture;c->format=out;
    return c->picture?Ok:NotInitialized;
}
int32_t wic_CanConvert(void*,const void* from,const void* to,int32_t* can) {
    if(!from||!to||!can)return Pointer;
    Out a,b;*can=outFormat(from,a)&&outFormat(to,b);return Ok;
}
// CoCreateInstance's WIC classes (Misc.cpp): `known` says the class is one.
int32_t createWicFactory(const void* id,const void* want,void** out,bool& known) {
    known=false;
    if(!id)return 0;
    for(auto& g:FactoryClasses)if(same(id,g))known=true;
    if(!known)return 0;
    if(!out)return Pointer;
    *out=nullptr;
    void* factory;
    win_WICCreateImagingFactory_Proxy(0,&factory);
    int32_t result=wic_QueryInterface(factory,want,out);
    wic_Release(factory);
    return result;
}
}
