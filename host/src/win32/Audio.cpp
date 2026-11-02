// The host's WASAPI (AudioAbi.h): MMDeviceEnumerator with one render device,
// and IAudioClient streams mixed by one output thread into 48 kHz stereo grains
// for sceAudioOut (a paced silence on Linux). The vtables are in audio_abi.c.
//
// The engine wraps the real MMDeviceEnumerator in its own object for the
// game's Xbox audio interface (title 0.89: with CoCreateInstance refused, the
// game used the missing enumerator and faulted at address 0).
#include "AudioAbi.h"
#include "Win32.h"
#include "../Platform.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#if !defined(__linux__)
// libSceAudioOut, as fh-ps5 opens it on the console: the system user's main
// port, 256 frames of 48 kHz 16-bit stereo; each output blocks for one grain.
extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user,int32_t type,int32_t index,uint32_t grain,uint32_t rate,uint32_t format);
int sceAudioOutOutput(int32_t port,const void* samples);
}
#endif
extern "C" void* win_CoTaskMemAlloc(uint64_t size);
extern "C" int32_t win_SetEvent(void* event);

using azimuth::win32::log;

namespace {
constexpr int32_t Ok=0,False=1,NotImplemented=int32_t(0x80004001),NoInterface=int32_t(0x80004002),Pointer=int32_t(0x80004003),
                  InvalidArgument=int32_t(0x80070057),OutOfMemory=int32_t(0x8007000e),NotFound=int32_t(0x80070490),
                  NotInitialized=int32_t(0x88890001),AlreadyInitialized=int32_t(0x88890002),NotStopped=int32_t(0x88890005),
                  BufferTooLarge=int32_t(0x88890006),OutOfOrder=int32_t(0x88890007),UnsupportedFormat=int32_t(0x88890008),
                  InvalidSize=int32_t(0x88890009),EventHandleNotSet=int32_t(0x88890014);
constexpr uint32_t EventCallback=0x40000,BufferSilent=2,Render=0,Capture=1,All=2,Active=1;
constexpr uint32_t OutRate=48000,Grain=256;
constexpr int64_t Period=100000,MinimumPeriod=30000;   // 10 ms and 3 ms, in 100 ns units

struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
constexpr Guid Unknown{0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
constexpr Guid EnumeratorId{0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
constexpr Guid CollectionId{0x0bd7a1be,0x7a1a,0x44db,{0x83,0x97,0xcc,0x53,0x92,0x38,0x7b,0x5e}};
constexpr Guid DeviceId{0xd666063f,0x1587,0x4e43,{0x81,0xf1,0xb9,0x48,0xe8,0x07,0x36,0x3f}};
constexpr Guid StoreId{0x886d8eeb,0x8cf2,0x4446,{0x8d,0x02,0xcd,0xba,0x1d,0xbd,0xcf,0x99}};
constexpr Guid ClientIds[]={
    {0x1cb9ad4c,0xdbfa,0x4c32,{0xb1,0x78,0xc2,0xf5,0x68,0xa7,0x03,0xb2}},   // IAudioClient
    {0x726778cd,0xf60a,0x4eda,{0x82,0xde,0xe4,0x76,0x10,0xcd,0x78,0xaa}},   // IAudioClient2
};
constexpr Guid RenderId{0xf294acfc,0x3146,0x4483,{0xa7,0xbf,0xad,0xdc,0xa7,0xc2,0x60,0xe2}};
constexpr Guid ClockId{0xcd63314f,0x3fba,0x4a1b,{0x81,0x2c,0xef,0x96,0x35,0x87,0x28,0xe7}};
constexpr Guid SimpleVolumeId{0x87ce5498,0x68d6,0x44e5,{0x92,0x15,0x6d,0xa4,0x7e,0xf8,0x83,0xd8}};
constexpr Guid StreamVolumeId{0x93014887,0x242d,0x4068,{0x8a,0x15,0xcf,0x5e,0x93,0xb9,0x0f,0xe3}};
constexpr Guid EnumeratorClass{0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
constexpr Guid FloatFormat{0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
constexpr Guid PcmFormat{0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
// PROPERTYKEYs the device's store answers.
constexpr Guid FriendlyNameKey{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}};   // pid 14
constexpr Guid DeviceFormatKey{0xf19f064d,0x082c,0x4e27,{0xbc,0x73,0x68,0x82,0xa1,0xbb,0x8e,0x4c}};   // pid 0
const char16_t DeviceName[]=u"PlayStation 5";
const char16_t DeviceIdText[]=u"{0.0.0.00000000}.{a2d5ps50-0000-0000-0000-000000000001}";

bool same(const void* a,const Guid& b){return !std::memcmp(a,&b,sizeof b);}
std::string text(const void* p) {
    Guid g;std::memcpy(&g,p,sizeof g);char s[40];
    std::snprintf(s,sizeof s,"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.a,g.b,g.c,g.d[0],g.d[1],g.d[2],g.d[3],g.d[4],g.d[5],g.d[6],g.d[7]);
    return s;
}
std::mutex reportLock;
std::set<std::string> reported;
void reportOnce(const std::string& line){std::lock_guard<std::mutex> guard(reportLock);if(reported.insert(line).second)log(line);}

// WAVEFORMATEX(TENSIBLE) as the host reads it.
struct Format {uint32_t channels=0,rate=0,bits=0,block=0;bool isFloat=false;};
bool readFormat(const void* p,Format& f) {
    if(!p)return false;
    auto b=static_cast<const uint8_t*>(p);
    uint16_t tag,channels,block,bits,extra;uint32_t rate;
    std::memcpy(&tag,b,2);std::memcpy(&channels,b+2,2);std::memcpy(&rate,b+4,4);
    std::memcpy(&block,b+12,2);std::memcpy(&bits,b+14,2);std::memcpy(&extra,b+16,2);
    if(tag==0xfffe) {
        if(extra<22)return false;
        if(same(b+24,FloatFormat))tag=3;else if(same(b+24,PcmFormat))tag=1;else return false;
    }
    f.isFloat=tag==3;
    if(!(tag==1&&(bits==16||bits==32||bits==24))&&!(tag==3&&bits==32))return false;
    if(!channels||channels>8||rate<8000||rate>192000||block!=channels*bits/8)return false;
    f.channels=channels;f.rate=rate;f.bits=bits;f.block=block;
    return true;
}
// The mix format: 48 kHz stereo 32-bit float, WAVEFORMATEXTENSIBLE (40 bytes).
void writeMixFormat(void* p) {
    auto b=static_cast<uint8_t*>(p);std::memset(b,0,40);
    uint16_t tag=0xfffe,channels=2,block=8,bits=32,extra=22,valid=32;uint32_t rate=OutRate,bytes=OutRate*8,mask=3;
    std::memcpy(b,&tag,2);std::memcpy(b+2,&channels,2);std::memcpy(b+4,&rate,4);std::memcpy(b+8,&bytes,4);
    std::memcpy(b+12,&block,2);std::memcpy(b+14,&bits,2);std::memcpy(b+16,&extra,2);
    std::memcpy(b+18,&valid,2);std::memcpy(b+20,&mask,4);std::memcpy(b+24,&FloatFormat,16);
}
void* newMixFormat(){void* p=win_CoTaskMemAlloc(40);if(p)writeMixFormat(p);return p;}
char16_t* newString(const char16_t* s) {
    size_t n=std::char_traits<char16_t>::length(s)+1;
    auto p=static_cast<char16_t*>(win_CoTaskMemAlloc(n*2));
    if(p)std::memcpy(p,s,n*2);
    return p;
}

// The COM layout: the vtable pointer first (no C++ virtual functions here).
struct Object {const void* const* vtable;std::atomic<uint32_t> references{1};};
struct Client;
// The enumerator, collection, device and store hold nothing; the services
// (render client, clock, volumes) hold their stream.
struct Part:Object {Client* client=nullptr;};
struct Collection:Object {bool device=false;};
struct Client:Object {
    bool initialized=false,started=false,eventDriven=false;
    Format format;
    uint32_t bufferFrames=0;
    std::vector<uint8_t> ring,staging;
    uint32_t readAt=0,queued=0,pending=0;   // frames; `pending` is between GetBuffer and ReleaseBuffer
    void* event=nullptr;
    uint64_t played=0;                         // frames taken by the output, silence included
    double phase=1;float previous[8]{},next[8]{};
    float volume=1;bool muted=false;std::vector<float> channelVolume;
};
// Streams and their state belong to `streams`'s lock.
std::mutex streamsLock;
std::vector<Client*> streams;
std::once_flag outputStarted;

Object* object(void* self){return static_cast<Object*>(self);}
Client* client(void* self){return static_cast<Client*>(self);}
Part* part(void* self){return static_cast<Part*>(self);}
bool isClient(Object* o){return o->vtable==guestAudioClientVtable;}
bool isPart(Object* o) {
    return o->vtable==guestAudioRenderVtable||o->vtable==guestAudioClockVtable||
           o->vtable==guestAudioSimpleVolumeVtable||o->vtable==guestAudioStreamVolumeVtable;
}
Object* newObject(const void* const* vtable){auto o=new Object;o->vtable=vtable;return o;}
Part* newPart(const void* const* vtable,Client* c){auto p=new Part;p->vtable=vtable;p->client=c;++c->references;return p;}
void destroy(Object* o) {
    if(isClient(o)) {
        auto c=static_cast<Client*>(o);
        {std::lock_guard<std::mutex> guard(streamsLock);streams.erase(std::remove(streams.begin(),streams.end(),c),streams.end());}
        delete c;
    }else if(isPart(o)) {
        auto c=static_cast<Part*>(o)->client;delete static_cast<Part*>(o);
        if(!--c->references)destroy(c);
    }else if(o->vtable==guestAudioCollectionVtable)delete static_cast<Collection*>(o);
    else delete o;
}
bool supports(Object* o,const void* want) {
    if(same(want,Unknown))return true;
    auto v=o->vtable;
    if(v==guestAudioEnumeratorVtable)return same(want,EnumeratorId);
    if(v==guestAudioCollectionVtable)return same(want,CollectionId);
    if(v==guestAudioDeviceVtable)return same(want,DeviceId);
    if(v==guestAudioStoreVtable)return same(want,StoreId);
    if(v==guestAudioClientVtable)return same(want,ClientIds[0])||same(want,ClientIds[1]);
    if(v==guestAudioRenderVtable)return same(want,RenderId);
    if(v==guestAudioClockVtable)return same(want,ClockId);
    if(v==guestAudioSimpleVolumeVtable)return same(want,SimpleVolumeId);
    return v==guestAudioStreamVolumeVtable&&same(want,StreamVolumeId);
}
// Hands out `o` as `want`, taking over the caller's reference to it.
int32_t handOut(Object* o,const void* want,void** out) {
    if(!want){destroy(o);return Pointer;}
    if(!supports(o,want)){reportOnce("WASAPI: objeto sem a interface "+text(want));destroy(o);return NoInterface;}
    *out=o;return Ok;
}

// One frame of `c` at `readAt` as floats, `c->format.channels` of them.
void decode(const Client* c,uint32_t frame,float* out) {
    auto p=c->ring.data()+size_t(frame)*c->format.block;
    for(uint32_t n=0;n<c->format.channels;++n) {
        if(c->format.isFloat){std::memcpy(&out[n],p+4*n,4);continue;}
        switch(c->format.bits) {
            case 16:{int16_t v;std::memcpy(&v,p+2*n,2);out[n]=v/32768.0f;break;}
            case 24:{int32_t v=int32_t(uint32_t(p[3*n])<<8|uint32_t(p[3*n+1])<<16|uint32_t(p[3*n+2])<<24);out[n]=float(v/2147483648.0);break;}
            default:{int32_t v;std::memcpy(&v,p+4*n,4);out[n]=float(v/2147483648.0);break;}
        }
    }
}
// The next frame of the stream into `frame` (silence when it has run dry).
void take(Client* c,float* frame) {
    ++c->played;
    if(!c->queued){std::fill(frame,frame+8,0.0f);return;}
    decode(c,c->readAt,frame);
    for(uint32_t n=0;n<c->format.channels&&n<c->channelVolume.size();++n)frame[n]*=c->channelVolume[n];
    c->readAt=(c->readAt+1)%c->bufferFrames;--c->queued;
}
// Adds `frames` frames of the stream to `mix` (stereo), resampled to OutRate
// (linear) and folded to two channels.
void mixInto(Client* c,float* mix,uint32_t frames) {
    double step=double(c->format.rate)/OutRate;
    float gain=c->muted?0.0f:c->volume;
    uint32_t channels=c->format.channels;
    for(uint32_t i=0;i<frames;++i) {
        while(c->phase>=1){std::memcpy(c->previous,c->next,sizeof c->next);take(c,c->next);c->phase-=1;}
        float f[8];
        for(uint32_t n=0;n<channels;++n)f[n]=c->previous[n]+(c->next[n]-c->previous[n])*float(c->phase);
        c->phase+=step;
        float left,right;
        if(channels==1)left=right=f[0];
        else {
            left=f[0];right=f[1];
            // FL FR FC LFE BL BR (SL SR): centre and surrounds folded in at -3 dB.
            if(channels>=3){left+=0.707f*f[2];right+=0.707f*f[2];}
            if(channels>=6){left+=0.707f*f[4];right+=0.707f*f[5];}
            if(channels>=8){left+=0.707f*f[6];right+=0.707f*f[7];}
        }
        mix[2*i]+=left*gain;mix[2*i+1]+=right*gain;
    }
}

// One grain to the console's port; blocks until it is queued (< 0: failed).
int outputGrain(int32_t port,const int16_t* samples) {
#if !defined(__linux__)
    return sceAudioOutOutput(port,samples);
#else
    (void)port;(void)samples;return -1;
#endif
}

// The output thread: one grain per turn, paced by sceAudioOut (or the clock).
void outputLoop() {
    if(auto scheduling=azimuth::platform::shareCpu(true);!scheduling.empty())log("Agendamento do audio: "+scheduling);
    int32_t port=-1;
#if !defined(__linux__)
    int init=sceAudioOutInit();
    if(init<0&&init!=int(0x8026000e))log("Audio: sceAudioOutInit falhou ("+std::to_string(init)+"); saida em silencio");
    else {
        port=sceAudioOutOpen(0xff,0,0,Grain,OutRate,1);
        log(port<0?"Audio: sceAudioOutOpen falhou ("+std::to_string(port)+"); saida em silencio":"Audio: porta "+std::to_string(port)+" aberta, 48 kHz estereo");
    }
#endif
    float mix[2*Grain];int16_t samples[2*Grain] __attribute__((aligned(64)));
    std::vector<void*> events;
    auto due=std::chrono::steady_clock::now();
    unsigned failures=0;
    for(;;) {
        std::fill(mix,mix+2*Grain,0.0f);events.clear();
        {
            std::lock_guard<std::mutex> guard(streamsLock);
            for(auto c:streams) {
                if(!c->started)continue;
                mixInto(c,mix,Grain);
                if(c->event)events.push_back(c->event);
            }
        }
        for(auto e:events)win_SetEvent(e);
        for(uint32_t n=0;n<2*Grain;++n)samples[n]=int16_t(std::lround(std::clamp(mix[n],-1.0f,1.0f)*32767.0f));
        if(port>=0&&outputGrain(port,samples)>=0)continue;
        if(port>=0&&failures++==0)log("Audio: sceAudioOutOutput falhou; saida passa a ser so o relogio");
        due+=std::chrono::microseconds(1000000ull*Grain/OutRate);
        auto now=std::chrono::steady_clock::now();
        if(due<now-std::chrono::milliseconds(100))due=now;   // after a stall, no burst to catch up
        else std::this_thread::sleep_until(due);
    }
}
}

// A stream the host itself feeds (XAudio2.cpp): 32-bit float frames of
// `channels` at `rate`, a ring of `frames`, mixed like the WASAPI ones.
void* hostAudioStreamOpen(uint32_t channels,uint32_t rate,uint32_t frames) {
    auto c=new Client;c->vtable=guestAudioClientVtable;
    c->format.channels=channels;c->format.rate=rate;c->format.bits=32;c->format.block=4*channels;c->format.isFloat=true;
    c->bufferFrames=frames;c->ring.assign(size_t(frames)*c->format.block,0);
    c->channelVolume.assign(channels,1.0f);c->initialized=true;c->started=true;
    {std::lock_guard<std::mutex> guard(streamsLock);streams.push_back(c);}
    std::call_once(outputStarted,[]{std::thread(outputLoop).detach();});
    return c;
}
uint32_t hostAudioStreamSpace(void* stream) {
    auto c=static_cast<Client*>(stream);
    std::lock_guard<std::mutex> guard(streamsLock);
    return c->bufferFrames-c->queued;
}
// Queues `count` frames (at most the space left).
void hostAudioStreamWrite(void* stream,const float* frames,uint32_t count) {
    auto c=static_cast<Client*>(stream);
    std::lock_guard<std::mutex> guard(streamsLock);
    count=std::min(count,c->bufferFrames-c->queued);
    uint32_t at=(c->readAt+c->queued)%c->bufferFrames;
    for(uint32_t done=0;done<count;) {
        uint32_t run=std::min(count-done,c->bufferFrames-at);
        std::memcpy(c->ring.data()+size_t(at)*c->format.block,frames+size_t(done)*c->format.channels,size_t(run)*c->format.block);
        done+=run;at=(at+run)%c->bufferFrames;
    }
    c->queued+=count;
}

// For CoCreateInstance (Misc.cpp): the enumerator, if `id` is its class.
int32_t createAudioEnumerator(const void* id,const void* want,void** out,bool& known) {
    known=id&&same(id,EnumeratorClass);
    if(!known)return 0;
    if(!out)return Pointer;
    *out=nullptr;
    log("WASAPI: MMDeviceEnumerator criado");
    return handOut(newObject(guestAudioEnumeratorVtable),want,out);
}

extern "C" {
int32_t audioMissing(const char* object,uint32_t slot) {
    reportOnce(std::string("WASAPI: metodo ")+object+" #"+std::to_string(slot)+" ainda sem implementacao (E_NOTIMPL)");
    return NotImplemented;
}
int32_t audio_QueryInterface(void* self,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    auto o=object(self);
    if(!supports(o,want)){reportOnce("WASAPI: objeto sem a interface "+text(want));return NoInterface;}
    ++o->references;*out=o;return Ok;
}
uint32_t audio_AddRef(void* self){return ++object(self)->references;}
uint32_t audio_Release(void* self) {
    auto o=object(self);
    uint32_t left=--o->references;
    if(!left)destroy(o);
    return left;
}

// IMMDeviceEnumerator: one active render endpoint, no capture.
int32_t audio_EnumAudioEndpoints(void*,uint32_t flow,uint32_t states,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(flow>All)return InvalidArgument;
    auto collection=new Collection;collection->vtable=guestAudioCollectionVtable;
    // The collection holds the device only for render and active states.
    collection->device=flow!=Capture&&(states&Active);
    *out=collection;return Ok;
}
int32_t audio_GetDefaultAudioEndpoint(void*,uint32_t flow,uint32_t,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(flow!=Render){reportOnce("WASAPI: sem dispositivo de captura (E_NOTFOUND)");return NotFound;}
    *out=newObject(guestAudioDeviceVtable);return Ok;
}
int32_t audio_GetDevice(void*,const uint16_t* id,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!id||std::char_traits<char16_t>::compare(reinterpret_cast<const char16_t*>(id),DeviceIdText,sizeof DeviceIdText/2))return NotFound;
    *out=newObject(guestAudioDeviceVtable);return Ok;
}
// Endpoints never change: notifications are accepted and never sent.
int32_t audio_RegisterEndpointNotificationCallback(void*,void* client){return client?Ok:Pointer;}
int32_t audio_UnregisterEndpointNotificationCallback(void*,void* client){return client?Ok:Pointer;}

// IMMDeviceCollection
int32_t audio_CollectionGetCount(void* self,uint32_t* count) {
    if(!count)return Pointer;
    *count=static_cast<Collection*>(self)->device?1:0;return Ok;
}
int32_t audio_CollectionItem(void* self,uint32_t index,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(index>0||!static_cast<Collection*>(self)->device)return InvalidArgument;
    *out=newObject(guestAudioDeviceVtable);return Ok;
}

// IMMDevice
int32_t audio_Activate(void*,const void* want,uint32_t,void*,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    if(!same(want,ClientIds[0])&&!same(want,ClientIds[1])){reportOnce("WASAPI: Activate de "+text(want)+" nao suportado");return NoInterface;}
    auto c=new Client;c->vtable=guestAudioClientVtable;
    *out=c;return Ok;
}
int32_t audio_OpenPropertyStore(void*,uint32_t,void** out) {
    if(!out)return Pointer;
    *out=newObject(guestAudioStoreVtable);return Ok;
}
int32_t audio_GetId(void*,uint16_t** id) {
    if(!id)return Pointer;
    *id=reinterpret_cast<uint16_t*>(newString(DeviceIdText));
    return *id?Ok:OutOfMemory;
}
int32_t audio_GetState(void*,uint32_t* state){if(!state)return Pointer;*state=Active;return Ok;}

// IPropertyStore: the friendly name and the device format; anything else is empty.
// PROPERTYKEY: GUID then pid; PROPVARIANT: vt at 0, value at 8.
int32_t audio_StoreGetCount(void*,uint32_t* count){if(!count)return Pointer;*count=2;return Ok;}
int32_t audio_StoreGetAt(void*,uint32_t index,void* key) {
    if(!key)return Pointer;
    if(index>1)return InvalidArgument;
    uint32_t pid=index?0:14;
    std::memcpy(key,index?&DeviceFormatKey:&FriendlyNameKey,16);std::memcpy(static_cast<uint8_t*>(key)+16,&pid,4);
    return Ok;
}
int32_t audio_StoreGetValue(void*,const void* key,void* value) {
    if(!key||!value)return Pointer;
    auto v=static_cast<uint8_t*>(value);std::memset(v,0,24);
    uint32_t pid;std::memcpy(&pid,static_cast<const uint8_t*>(key)+16,4);
    if(same(key,FriendlyNameKey)&&pid==14) {
        uint16_t type=31;void* name=newString(DeviceName);   // VT_LPWSTR
        std::memcpy(v,&type,2);std::memcpy(v+8,&name,8);
    }else if(same(key,DeviceFormatKey)&&pid==0) {
        uint16_t type=65;uint32_t size=40;void* data=newMixFormat();   // VT_BLOB
        std::memcpy(v,&type,2);std::memcpy(v+8,&size,4);std::memcpy(v+16,&data,8);
    }
    return Ok;
}

// IAudioClient
int32_t audio_Initialize(void* self,uint32_t share,uint32_t flags,int64_t duration,int64_t,const void* format,const void*) {
    auto c=client(self);
    Format f;
    if(!readFormat(format,f)){reportOnce("WASAPI: Initialize com formato nao suportado");return UnsupportedFormat;}
    std::lock_guard<std::mutex> guard(streamsLock);
    if(c->initialized)return AlreadyInitialized;
    // The buffer: what was asked, at least two device periods.
    if(duration<2*Period)duration=2*Period;
    c->format=f;c->bufferFrames=uint32_t((uint64_t(duration)*f.rate+9999999)/10000000);
    c->ring.assign(size_t(c->bufferFrames)*f.block,0);c->staging.assign(c->ring.size(),0);
    c->channelVolume.assign(f.channels,1.0f);
    c->eventDriven=flags&EventCallback;c->initialized=true;
    streams.push_back(c);
    log("WASAPI: fluxo de "+std::to_string(f.rate)+" Hz, "+std::to_string(f.channels)+" canais, "+std::to_string(f.bits)+
        (f.isFloat?" bits float":" bits PCM")+", buffer de "+std::to_string(c->bufferFrames)+" quadros"+
        (c->eventDriven?", por evento":"")+(share?", modo exclusivo":""));
    std::call_once(outputStarted,[]{std::thread(outputLoop).detach();});
    return Ok;
}
int32_t audio_GetBufferSize(void* self,uint32_t* frames) {
    if(!frames)return Pointer;
    std::lock_guard<std::mutex> guard(streamsLock);
    if(!client(self)->initialized)return NotInitialized;
    *frames=client(self)->bufferFrames;return Ok;
}
int32_t audio_GetStreamLatency(void* self,int64_t* latency) {
    if(!latency)return Pointer;
    if(!client(self)->initialized)return NotInitialized;
    *latency=Period;return Ok;
}
int32_t audio_GetCurrentPadding(void* self,uint32_t* frames) {
    if(!frames)return Pointer;
    std::lock_guard<std::mutex> guard(streamsLock);
    if(!client(self)->initialized)return NotInitialized;
    *frames=client(self)->queued;return Ok;
}
// Every format readFormat takes is mixed (resampled and folded) by the host.
int32_t audio_IsFormatSupported(void*,uint32_t,const void* format,void** closest) {
    if(closest)*closest=nullptr;
    if(!format)return Pointer;
    Format f;
    if(readFormat(format,f))return Ok;
    if(closest)*closest=newMixFormat();
    return closest?False:UnsupportedFormat;
}
int32_t audio_GetMixFormat(void*,void** format) {
    if(!format)return Pointer;
    *format=newMixFormat();return *format?Ok:OutOfMemory;
}
int32_t audio_GetDevicePeriod(void*,int64_t* period,int64_t* minimum) {
    if(!period&&!minimum)return Pointer;
    if(period)*period=Period;
    if(minimum)*minimum=MinimumPeriod;
    return Ok;
}
int32_t audio_Start(void* self) {
    auto c=client(self);
    std::lock_guard<std::mutex> guard(streamsLock);
    if(!c->initialized)return NotInitialized;
    if(c->started)return NotStopped;
    if(c->eventDriven&&!c->event)return EventHandleNotSet;
    c->started=true;return Ok;
}
int32_t audio_Stop(void* self) {
    auto c=client(self);
    std::lock_guard<std::mutex> guard(streamsLock);
    if(!c->initialized)return NotInitialized;
    if(!c->started)return False;
    c->started=false;return Ok;
}
int32_t audio_Reset(void* self) {
    auto c=client(self);
    std::lock_guard<std::mutex> guard(streamsLock);
    if(!c->initialized)return NotInitialized;
    if(c->started)return NotStopped;
    c->readAt=0;c->queued=0;c->played=0;c->phase=1;
    std::fill(std::begin(c->previous),std::end(c->previous),0.0f);std::fill(std::begin(c->next),std::end(c->next),0.0f);
    return Ok;
}
int32_t audio_SetEventHandle(void* self,void* event) {
    if(!event)return InvalidArgument;
    std::lock_guard<std::mutex> guard(streamsLock);
    client(self)->event=event;return Ok;
}
int32_t audio_GetService(void* self,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    auto c=client(self);
    if(!c->initialized)return NotInitialized;
    const void* const* vtable=same(want,RenderId)?guestAudioRenderVtable:same(want,ClockId)?guestAudioClockVtable:
                              same(want,SimpleVolumeId)?guestAudioSimpleVolumeVtable:same(want,StreamVolumeId)?guestAudioStreamVolumeVtable:nullptr;
    if(!vtable){reportOnce("WASAPI: GetService de "+text(want)+" nao suportado");return NoInterface;}
    *out=newPart(vtable,c);return Ok;
}
int32_t audio_IsOffloadCapable(void*,uint32_t,int32_t* capable){if(!capable)return Pointer;*capable=0;return Ok;}
int32_t audio_SetClientProperties(void*,const void* properties){return properties?Ok:Pointer;}
int32_t audio_GetBufferSizeLimits(void*,const void* format,int32_t,int64_t* minimum,int64_t* maximum) {
    if(!minimum||!maximum)return Pointer;
    Format f;
    if(!readFormat(format,f))return UnsupportedFormat;
    *minimum=2*Period;*maximum=20000000;return Ok;
}

// IAudioRenderClient: the game writes into `staging`; ReleaseBuffer queues it.
int32_t audio_GetBuffer(void* self,uint32_t frames,uint8_t** data) {
    if(!data)return Pointer;
    *data=nullptr;
    auto c=part(self)->client;
    std::lock_guard<std::mutex> guard(streamsLock);
    if(c->pending)return OutOfOrder;
    if(frames>c->bufferFrames-c->queued)return BufferTooLarge;
    c->pending=frames;
    if(frames)*data=c->staging.data();
    return Ok;
}
int32_t audio_ReleaseBuffer(void* self,uint32_t frames,uint32_t flags) {
    auto c=part(self)->client;
    std::lock_guard<std::mutex> guard(streamsLock);
    if(frames>c->pending)return InvalidSize;
    c->pending=0;
    uint32_t at=(c->readAt+c->queued)%c->bufferFrames;
    for(uint32_t done=0;done<frames;) {
        uint32_t run=std::min(frames-done,c->bufferFrames-at);
        auto to=c->ring.data()+size_t(at)*c->format.block;
        if(flags&BufferSilent)std::memset(to,0,size_t(run)*c->format.block);
        else std::memcpy(to,c->staging.data()+size_t(done)*c->format.block,size_t(run)*c->format.block);
        done+=run;at=(at+run)%c->bufferFrames;
    }
    c->queued+=frames;
    return Ok;
}

// IAudioClock: positions in the stream's frames.
int32_t audio_GetFrequency(void* self,uint64_t* frequency) {
    if(!frequency)return Pointer;
    *frequency=part(self)->client->format.rate;return Ok;
}
int32_t audio_GetPosition(void* self,uint64_t* position,uint64_t* counter) {
    if(!position)return Pointer;
    {std::lock_guard<std::mutex> guard(streamsLock);*position=part(self)->client->played;}
    // The performance counter in 100 ns units, as Windows gives it.
    if(counter)*counter=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100);
    return Ok;
}
int32_t audio_GetCharacteristics(void*,uint32_t* characteristics){if(!characteristics)return Pointer;*characteristics=0;return Ok;}

// ISimpleAudioVolume
int32_t audio_SetMasterVolume(void* self,float level,const void*) {
    if(!(level>=0&&level<=1))return InvalidArgument;
    std::lock_guard<std::mutex> guard(streamsLock);part(self)->client->volume=level;return Ok;
}
int32_t audio_GetMasterVolume(void* self,float* level){if(!level)return Pointer;*level=part(self)->client->volume;return Ok;}
int32_t audio_SetMute(void* self,int32_t mute,const void*){std::lock_guard<std::mutex> guard(streamsLock);part(self)->client->muted=mute;return Ok;}
int32_t audio_GetMute(void* self,int32_t* mute){if(!mute)return Pointer;*mute=part(self)->client->muted;return Ok;}

// IAudioStreamVolume
int32_t audio_GetChannelCount(void* self,uint32_t* count){if(!count)return Pointer;*count=part(self)->client->format.channels;return Ok;}
int32_t audio_SetChannelVolume(void* self,uint32_t channel,float level) {
    auto c=part(self)->client;
    if(channel>=c->format.channels||!(level>=0&&level<=1))return InvalidArgument;
    std::lock_guard<std::mutex> guard(streamsLock);c->channelVolume[channel]=level;return Ok;
}
int32_t audio_GetChannelVolume(void* self,uint32_t channel,float* level) {
    auto c=part(self)->client;
    if(!level)return Pointer;
    if(channel>=c->format.channels)return InvalidArgument;
    *level=c->channelVolume[channel];return Ok;
}
int32_t audio_SetAllVolumes(void* self,uint32_t count,const float* levels) {
    auto c=part(self)->client;
    if(!levels)return Pointer;
    if(count!=c->format.channels)return InvalidArgument;
    std::lock_guard<std::mutex> guard(streamsLock);
    for(uint32_t n=0;n<count;++n){if(!(levels[n]>=0&&levels[n]<=1))return InvalidArgument;c->channelVolume[n]=levels[n];}
    return Ok;
}
int32_t audio_GetAllVolumes(void* self,uint32_t count,float* levels) {
    auto c=part(self)->client;
    if(!levels)return Pointer;
    if(count!=c->format.channels)return InvalidArgument;
    std::copy(c->channelVolume.begin(),c->channelVolume.end(),levels);return Ok;
}
}
