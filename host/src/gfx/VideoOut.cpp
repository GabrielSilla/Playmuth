#include "VideoOut.h"
#include "../win32/Win32.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>

#if !defined(__linux__)
extern "C" {
int sceVideoOutOpen(int32_t user,int32_t bus,int32_t index,const void* parameter);
int sceVideoOutClose(int32_t handle);
int sceVideoOutSetFlipRate(int32_t handle,int32_t rate);
void sceVideoOutSetBufferAttribute2(void* attribute,uint64_t pixelFormat,uint32_t tiling,uint32_t width,uint32_t height,
                                    uint64_t option,uint32_t dccControl,uint64_t dccClearColor);
struct SceVideoOutBuffer {void* data;void* metadata;void* reserved[2];};
int sceVideoOutRegisterBuffers2(int32_t handle,int32_t set,int32_t start,SceVideoOutBuffer* buffers,int32_t count,
                                void* attribute,int32_t category,void* option);
int sceVideoOutSubmitFlip(int32_t handle,int32_t index,uint32_t mode,int64_t argument);
int sceVideoOutIsFlipPending(int32_t handle);
int sceSystemServiceHideSplashScreen(void);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t searchStart,int64_t searchEnd,size_t length,size_t alignment,int type,int64_t* physical);
int sceKernelMapDirectMemory(void** address,size_t length,int protection,int flags,int64_t physical,size_t alignment);
}
#endif

namespace azimuth::video {
#if defined(__linux__)
bool present(const uint32_t*,int,int) {
    static bool said=false;
    if(!said){said=true;win32::log("VideoOut: sem tela no Linux");}
    return false;
}
void release(){}
#else
namespace {
constexpr unsigned Width=1920,Height=1080;
constexpr size_t FrameBytes=0x1000000,Alignment=0x200000;
constexpr int WriteCombinedMemory=3,ReadWrite=0x33;
constexpr uint64_t PixelFormatB8G8R8A8Sdr=UINT64_C(0x8000000000000000);
constexpr int BuffersCount=2;
int handle=-1;
uint8_t* memory=nullptr;
int next=0;
int64_t flips=0;
bool failed=false;
bool released=false;
std::mutex lock;   // present and release come from different threads

void fail(const std::string& why){if(!failed){failed=true;win32::log("VideoOut: "+why);}}
// 64 KiB tiles of 128x128 four-byte pixels (PS5_Vulkan src/demo_renderer.cpp, tiled_byte_offset).
inline size_t tiledOffset(unsigned x,unsigned y) {
    const uint32_t offset=((y<<4)&0x70u)^((y<<5)&0xf00u)^((y<<9)&0x1000u)^((y<<8)&0x4000u)^
                          ((x<<2)&0xcu)^((x<<5)&0x380u)^((x<<4)&0x400u)^((x<<6)&0x800u)^((x<<9)&0xa000u);
    const uint32_t perRow=(Width+127u)>>7;
    return (size_t(((y>>7)*perRow)+(x>>7))<<16)+offset;
}
void flush(void* address,size_t length) {
    auto at=static_cast<uint8_t*>(address);auto end=at+length;
    for(;at<end;at+=64)__asm__ volatile("clflush (%0)"::"r"(at):"memory");
    __asm__ volatile("mfence":::"memory");
}
bool open() {
    if(handle>=0)return true;
    if(failed)return false;
    (void)sceSystemServiceHideSplashScreen();
    int video=sceVideoOutOpen(0xff,0,0,nullptr);
    if(video<0){fail("sceVideoOutOpen falhou: "+std::to_string(video));return false;}
    size_t pool=sceKernelGetDirectMemorySize();
    size_t total=FrameBytes*BuffersCount;
    if(pool<total){fail("memoria direta insuficiente: "+std::to_string(pool));return false;}
    int64_t physical=0;
    int result=sceKernelAllocateDirectMemory(0,int64_t(pool),total,Alignment,WriteCombinedMemory,&physical);
    if(result<0){fail("sceKernelAllocateDirectMemory falhou: "+std::to_string(result));return false;}
    void* mapped=nullptr;
    result=sceKernelMapDirectMemory(&mapped,total,ReadWrite,0,physical,Alignment);
    if(result<0){fail("sceKernelMapDirectMemory falhou: "+std::to_string(result));return false;}
    memory=static_cast<uint8_t*>(mapped);
    std::memset(memory,0,total);flush(memory,total);
    SceVideoOutBuffer buffers[BuffersCount]={};
    for(int n=0;n<BuffersCount;++n)buffers[n].data=memory+size_t(n)*FrameBytes;
    uint8_t attribute[80]={};
    (void)sceVideoOutSetFlipRate(video,0);
    sceVideoOutSetBufferAttribute2(attribute,PixelFormatB8G8R8A8Sdr,0,Width,Height,0,0,0);
    result=sceVideoOutRegisterBuffers2(video,0,0,buffers,BuffersCount,attribute,0,nullptr);
    if(result<0){fail("sceVideoOutRegisterBuffers2 falhou: 0x"+std::to_string(unsigned(result)));return false;}
    handle=video;
    win32::log("VideoOut aberto: "+std::to_string(BuffersCount)+" framebuffers de "+std::to_string(Width)+"x"+std::to_string(Height)+
               " (B8G8R8A8 SDR, 64 KiB tiles), "+std::to_string(total>>20)+" MiB de memoria direta");
    return true;
}
}
bool present(const uint32_t* pixels,int width,int height) {
    std::lock_guard<std::mutex> guard(lock);
    if(released||!pixels||width<=0||height<=0||!open())return false;
    // Not over a framebuffer a flip still holds: the next one is free when no flip is pending.
    if(sceVideoOutIsFlipPending(handle)>1)return false;
    uint8_t* frame=memory+size_t(next)*FrameBytes;
    unsigned w=std::min<unsigned>(unsigned(width),Width),h=std::min<unsigned>(unsigned(height),Height);
    for(unsigned y=0;y<h;++y) {
        const uint32_t* row=pixels+size_t(y)*unsigned(width);
        for(unsigned x=0;x<w;++x) {
            uint32_t value=row[x]|0xff000000u;
            std::memcpy(frame+tiledOffset(x,y),&value,4);
        }
    }
    flush(frame,FrameBytes);
    int result=sceVideoOutSubmitFlip(handle,next,1,++flips);
    if(result<0){fail("sceVideoOutSubmitFlip falhou: 0x"+std::to_string(unsigned(result)));return false;}
    if(flips==1)win32::log("VideoOut: primeiro quadro apresentado");
    next=(next+1)%BuffersCount;
    return true;
}
void release() {
    std::lock_guard<std::mutex> guard(lock);
    if(released)return;
    released=true;
    if(handle<0)return;
    int result=sceVideoOutClose(handle);
    handle=-1;
    win32::log("VideoOut: porta do launcher fechada ("+std::to_string(result)+"); a tela passa ao swap chain do jogo");
}
#endif
}
