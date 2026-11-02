// xaudio2_9's XAudio2 for the game (title 0.97: FMOD loads xaudio2_9.dll and
// asks the engine for CreateXAudio2Object, which wraps the desktop XAudio2Create
// and only passes on PCM/float source voices without sends or effects, and the
// mastering voice). One engine thread, made with CreateThread so that it may
// call FMOD's callbacks, mixes every 10 ms: the queued buffers of each started
// source voice (resampled by its rates and frequency ratio, with loops),
// through its volumes and output matrix, into the mastering voice, whose
// frames go to a host stream of the audio output (Audio.cpp). The vtables are
// in xaudio2_abi.c.
#include "XAudio2Abi.h"
#include "Win32.h"
#include "../guest_abi.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

void* hostAudioStreamOpen(uint32_t channels,uint32_t rate,uint32_t frames);
uint32_t hostAudioStreamSpace(void* stream);
void hostAudioStreamWrite(void* stream,const float* frames,uint32_t count);
extern "C" void* win_CreateThread(void* security,uint64_t stack,void* start,void* parameter,uint32_t flags,uint32_t* id);

using azimuth::win32::log;

namespace {
constexpr int32_t Ok=0,NotImplemented=int32_t(0x80004001),NoInterface=int32_t(0x80004002),Pointer=int32_t(0x80004003),
                  Fail=int32_t(0x80004005),InvalidArgument=int32_t(0x80070057),InvalidCall=int32_t(0x88960001);
constexpr uint32_t DefaultChannels=2,DefaultRate=48000,QuantumMs=10,EndOfStream=0x40,LoopInfinite=255,NoSamplesPlayed=0x100,
                  MaxBuffers=64;
struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
constexpr Guid Unknown{0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
constexpr Guid EngineId{0x2b02e3cf,0x2e0b,0x4ec3,{0xbe,0x45,0x1b,0x2a,0x3f,0xe7,0x21,0x0d}};   // IXAudio2 (2.9)
bool same(const void* a,const Guid& b){return !std::memcmp(a,&b,sizeof b);}
std::mutex reportLock;
std::set<std::string> reported;
void reportOnce(const std::string& line){std::lock_guard<std::mutex> guard(reportLock);if(reported.insert(line).second)log(line);}

// A Windows COM-style callback method (IXAudio2VoiceCallback, IXAudio2EngineCallback: no IUnknown).
void callback(void* object,unsigned slot,uint64_t a=0,uint64_t b=0) {
    if(!object)return;
    auto vtable=*static_cast<const void* const* const*>(object);
    guestCall8(vtable[slot],reinterpret_cast<uint64_t>(object),a,b,0,0,0,0,0);
}
enum VoiceCallback {PassStart=0,PassEnd=1,StreamEnd=2,BufferStart=3,BufferEnd=4,LoopEnd=5};
enum EngineCallback {ProcessingStart=0,ProcessingEnd=1};

// XAUDIO2_BUFFER (48 bytes).
struct BufferDesc {uint32_t flags,bytes;const uint8_t* data;uint32_t playBegin,playLength,loopBegin,loopLength,loopCount;uint32_t pad;void* context;};
static_assert(sizeof(BufferDesc)==48,"XAUDIO2_BUFFER");
struct Buffer {BufferDesc d;uint32_t at=0,end=0,loopEnd=0,loopsLeft=0;bool started=false;};

struct Engine;
struct Voice {
    const void* const* vtable;Engine* engine=nullptr;
    uint32_t channels=2,rate=DefaultRate,flags=0;
    float volume=1;std::vector<float> channelVolumes;
    std::vector<float> matrix;bool matrixSet=false;   // to the mastering voice: [destination * channels + source]
};
struct Source:Voice {
    bool isFloat=false;uint32_t bits=16,block=4;
    void* callback=nullptr;
    std::deque<Buffer> queue;std::vector<void*> flushed;
    bool started=false;float ratio=1,maxRatio=2;
    uint64_t played=0;double phase=1;float previous[8]{},next[8]{};
};
struct Master:Voice {void* stream=nullptr;};
struct Engine {
    const void* const* vtable;std::atomic<uint32_t> references{1};
    std::vector<void*> callbacks;Master* master=nullptr;std::vector<Source*> sources;
    bool started=false;
};
// Every engine and voice belongs to `lock`, held by the mixer while it calls back
// (recursive: callbacks submit buffers).
std::recursive_mutex lock;
std::vector<Engine*> engines;
std::once_flag threadStarted;

bool readFormat(const void* p,Source& s) {
    if(!p)return false;
    auto b=static_cast<const uint8_t*>(p);
    uint16_t tag,channels,block,bits;uint32_t rate;
    std::memcpy(&tag,b,2);std::memcpy(&channels,b+2,2);std::memcpy(&rate,b+4,4);std::memcpy(&block,b+12,2);std::memcpy(&bits,b+14,2);
    if(!(tag==1&&(bits==8||bits==16||bits==24||bits==32))&&!(tag==3&&bits==32))return false;
    if(!channels||channels>8||!rate||block!=channels*bits/8)return false;
    s.isFloat=tag==3;s.bits=bits;s.block=block;s.channels=channels;s.rate=rate;
    return true;
}
float sample(const Source& s,const uint8_t* p) {
    if(s.isFloat){float f;std::memcpy(&f,p,4);return f;}
    switch(s.bits) {
        case 8:return (int(p[0])-128)/128.0f;
        case 16:{int16_t v;std::memcpy(&v,p,2);return v/32768.0f;}
        case 24:{int32_t v=int32_t(uint32_t(p[0])<<8|uint32_t(p[1])<<16|uint32_t(p[2])<<24);return float(v/2147483648.0);}
        default:{int32_t v;std::memcpy(&v,p,4);return float(v/2147483648.0);}
    }
}
// The default matrix to `to` channels, as XAudio2 sets it up.
void defaultMatrix(Voice& v,uint32_t to) {
    v.matrix.assign(size_t(to)*v.channels,0.0f);
    if(v.channels==1){for(uint32_t d=0;d<std::min(to,2u);++d)v.matrix[d]=1.0f;return;}
    if(to==1){for(uint32_t s=0;s<v.channels;++s)v.matrix[s]=1.0f/float(v.channels);return;}
    for(uint32_t c=0;c<std::min(v.channels,to);++c)v.matrix[size_t(c)*v.channels+c]=1.0f;
}

// The next frame of a source voice (its buffers' callbacks on the way), or silence.
void take(Source& s,float* frame) {
    for(;;) {
        if(s.queue.empty()){std::fill(frame,frame+8,0.0f);return;}
        auto& b=s.queue.front();
        if(!b.started){b.started=true;callback(s.callback,BufferStart,reinterpret_cast<uint64_t>(b.d.context));}
        if(b.loopsLeft&&b.at>=b.loopEnd) {
            if(b.loopsLeft!=LoopInfinite)--b.loopsLeft;
            b.at=b.d.loopBegin;
            callback(s.callback,LoopEnd,reinterpret_cast<uint64_t>(b.d.context));
        }
        if(b.at<b.end) {
            auto p=b.d.data+size_t(b.at)*s.block;
            for(uint32_t c=0;c<s.channels;++c)frame[c]=sample(s,p+size_t(c)*(s.bits/8));
            ++b.at;++s.played;
            return;
        }
        void* context=b.d.context;bool last=b.d.flags&EndOfStream;
        s.queue.pop_front();
        callback(s.callback,BufferEnd,reinterpret_cast<uint64_t>(context));
        if(last)callback(s.callback,StreamEnd);
    }
}
// One quantum of the mix into `out` (frames of the master's channels).
void render(Engine& e,std::vector<float>& out,uint32_t frames) {
    auto& m=*e.master;
    out.assign(size_t(frames)*m.channels,0.0f);
    for(auto cb:e.callbacks)callback(cb,ProcessingStart);
    std::vector<float> line;
    for(auto s:std::vector<Source*>(e.sources)) {
        for(auto context:s->flushed)callback(s->callback,BufferEnd,reinterpret_cast<uint64_t>(context));
        s->flushed.clear();
        if(!s->started)continue;
        double step=double(s->rate)*s->ratio/double(m.rate);
        callback(s->callback,PassStart,uint64_t(double(frames)*step+1)*s->block);
        if(!s->matrixSet||s->matrix.size()!=size_t(m.channels)*s->channels)defaultMatrix(*s,m.channels);
        for(uint32_t i=0;i<frames;++i) {
            while(s->phase>=1){std::memcpy(s->previous,s->next,sizeof s->next);take(*s,s->next);s->phase-=1;}
            float f[8];
            for(uint32_t c=0;c<s->channels;++c)
                f[c]=(s->previous[c]+(s->next[c]-s->previous[c])*float(s->phase))*s->volume*(c<s->channelVolumes.size()?s->channelVolumes[c]:1.0f);
            s->phase+=step;
            float* to=out.data()+size_t(i)*m.channels;
            for(uint32_t d=0;d<m.channels;++d)
                for(uint32_t c=0;c<s->channels;++c)to[d]+=f[c]*s->matrix[size_t(d)*s->channels+c];
        }
        callback(s->callback,PassEnd);
    }
    for(uint32_t i=0;i<frames;++i)
        for(uint32_t d=0;d<m.channels;++d)out[size_t(i)*m.channels+d]*=m.volume*(d<m.channelVolumes.size()?m.channelVolumes[d]:1.0f);
    for(auto cb:e.callbacks)callback(cb,ProcessingEnd);
}
Engine* engine(void* self){return static_cast<Engine*>(self);}
Voice* voice(void* self){return static_cast<Voice*>(self);}
Source* source(void* self){return static_cast<Source*>(self);}
}

extern "C" {
// xaudio2_9's export.
int32_t win_XAudio2Create(void** out,uint32_t,uint32_t) {
    if(!out)return Pointer;
    auto e=new Engine;e->vtable=guestXAudio2EngineVtable;
    {std::lock_guard<std::recursive_mutex> guard(lock);engines.push_back(e);}
    std::call_once(threadStarted,[]{
        uint32_t id=0;
        if(!win_CreateThread(nullptr,0,const_cast<void*>(guestXAudio2ThreadStart),nullptr,0,&id))log("XAudio2: a thread do mixer nao foi criada");
    });
    log("XAudio2: motor criado");
    *out=e;return Ok;
}

// The mixer, on its engine thread.
uint32_t xaudio_ThreadMain(void*) {
    log("XAudio2: thread do mixer iniciada ("+std::to_string(QuantumMs)+" ms por passo)");
    std::vector<float> mix;
    for(;;) {
        bool rendered=false;
        {
            std::lock_guard<std::recursive_mutex> guard(lock);
            for(auto e:engines) {
                if(!e->started||!e->master||!e->master->stream)continue;
                uint32_t frames=e->master->rate*QuantumMs/1000;
                if(hostAudioStreamSpace(e->master->stream)<frames)continue;
                render(*e,mix,frames);
                hostAudioStreamWrite(e->master->stream,mix.data(),frames);
                rendered=true;
            }
        }
        if(!rendered)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// IXAudio2
int32_t xaudio_QueryInterface(void* self,const void* want,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!want)return Pointer;
    if(!same(want,Unknown)&&!same(want,EngineId))return NoInterface;
    ++engine(self)->references;*out=self;return Ok;
}
uint32_t xaudio_AddRef(void* self){return ++engine(self)->references;}
// The engine stays (its voices may still be named by the game); it stops mixing.
uint32_t xaudio_Release(void* self) {
    auto e=engine(self);
    uint32_t left=--e->references;
    if(!left){std::lock_guard<std::recursive_mutex> guard(lock);e->started=false;}
    return left;
}
int32_t xaudio_RegisterForCallbacks(void* self,void* cb) {
    if(!cb)return InvalidArgument;
    std::lock_guard<std::recursive_mutex> guard(lock);
    engine(self)->callbacks.push_back(cb);return Ok;
}
void xaudio_UnregisterForCallbacks(void* self,void* cb) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto& list=engine(self)->callbacks;list.erase(std::remove(list.begin(),list.end(),cb),list.end());
}
int32_t xaudio_CreateSourceVoice(void* self,void** out,const void* format,uint32_t flags,float maxRatio,void* cb,const void* sends,const void* effects) {
    if(!out)return Pointer;
    *out=nullptr;
    auto s=new Source;s->vtable=guestXAudio2SourceVtable;s->engine=engine(self);
    if(!readFormat(format,*s)){delete s;reportOnce("XAudio2: formato de voz nao suportado");return InvalidArgument;}
    if(effects)reportOnce("XAudio2: cadeia de efeitos ignorada");
    if(sends)reportOnce("XAudio2: envios de voz ignorados; a voz toca na voz principal");
    s->flags=flags;s->maxRatio=maxRatio;s->callback=cb;s->channelVolumes.assign(s->channels,1.0f);
    {std::lock_guard<std::recursive_mutex> guard(lock);s->engine->sources.push_back(s);}
    static std::atomic<unsigned> said{0};
    if(said++<8)log("XAudio2: voz de "+std::to_string(s->rate)+" Hz, "+std::to_string(s->channels)+" canais, "+std::to_string(s->bits)+
                    (s->isFloat?" bits float":" bits PCM")+(cb?", com callback":""));
    *out=s;return Ok;
}
int32_t xaudio_CreateSubmixVoice(void*,void** out,uint32_t,uint32_t,uint32_t,uint32_t,const void*,const void*) {
    if(out)*out=nullptr;
    reportOnce("XAudio2: CreateSubmixVoice ainda sem implementacao");
    return NotImplemented;
}
int32_t xaudio_CreateMasteringVoice(void* self,void** out,uint32_t channels,uint32_t rate,uint32_t flags,const uint16_t*,const void*,uint32_t) {
    if(!out)return Pointer;
    *out=nullptr;
    auto e=engine(self);
    std::lock_guard<std::recursive_mutex> guard(lock);
    if(e->master)return InvalidCall;
    auto m=new Master;m->vtable=guestXAudio2MasterVtable;m->engine=e;m->flags=flags;
    m->channels=channels?std::min(channels,8u):DefaultChannels;m->rate=rate?rate:DefaultRate;
    m->channelVolumes.assign(m->channels,1.0f);
    // Four quanta of latency.
    m->stream=hostAudioStreamOpen(m->channels,m->rate,m->rate*QuantumMs/1000*4);
    e->master=m;
    log("XAudio2: voz principal de "+std::to_string(m->channels)+" canais a "+std::to_string(m->rate)+" Hz");
    *out=m;return Ok;
}
int32_t xaudio_StartEngine(void* self){std::lock_guard<std::recursive_mutex> guard(lock);engine(self)->started=true;return Ok;}
void xaudio_StopEngine(void* self){std::lock_guard<std::recursive_mutex> guard(lock);engine(self)->started=false;}
// Changes take effect at once: an operation set has nothing left to commit.
int32_t xaudio_CommitChanges(void*,uint32_t){return Ok;}
// XAUDIO2_PERFORMANCE_DATA (64 bytes): counts of voices, the latency.
void xaudio_GetPerformanceData(void* self,void* data) {
    if(!data)return;
    std::memset(data,0,64);
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto e=engine(self);
    uint32_t active=0;for(auto s:e->sources)active+=s->started;
    uint32_t values[3]={e->master?e->master->rate*QuantumMs/1000*4:0,0,active};
    std::memcpy(static_cast<uint8_t*>(data)+28,values,12);   // CurrentLatencyInSamples, Glitches, ActiveSourceVoiceCount
    uint32_t total=uint32_t(e->sources.size());std::memcpy(static_cast<uint8_t*>(data)+40,&total,4);
}
void xaudio_SetDebugConfiguration(void*,const void*,void*){}

// IXAudio2Voice. XAUDIO2_VOICE_DETAILS: CreationFlags, ActiveFlags, InputChannels, InputSampleRate.
void xaudio_GetVoiceDetails(void* self,void* details) {
    if(!details)return;
    auto v=voice(self);uint32_t d[4]={v->flags,v->flags,v->channels,v->rate};std::memcpy(details,d,16);
}
int32_t xaudio_SetOutputVoices(void*,const void* sends) {
    if(sends){uint32_t count;std::memcpy(&count,sends,4);if(count)reportOnce("XAudio2: SetOutputVoices ignorado; a voz toca na voz principal");}
    return Ok;
}
int32_t xaudio_SetEffectChain(void*,const void* effects){if(effects)reportOnce("XAudio2: SetEffectChain ignorado");return Ok;}
int32_t xaudio_EnableEffect(void*,uint32_t,uint32_t){return Ok;}
int32_t xaudio_DisableEffect(void*,uint32_t,uint32_t){return Ok;}
void xaudio_GetEffectState(void*,uint32_t,int32_t* enabled){if(enabled)*enabled=0;}
int32_t xaudio_SetEffectParameters(void*,uint32_t,const void*,uint32_t,uint32_t){return Ok;}
int32_t xaudio_GetEffectParameters(void*,uint32_t,void*,uint32_t){return Fail;}
// Filters are accepted and not applied. XAUDIO2_FILTER_PARAMETERS: Type, Frequency, OneOverQ.
int32_t xaudio_SetFilterParameters(void*,const void*,uint32_t){reportOnce("XAudio2: filtros aceitos e nao aplicados");return Ok;}
void xaudio_GetFilterParameters(void*,void* parameters) {
    if(!parameters)return;
    uint32_t type=0;float values[2]={1.0f,1.0f};std::memcpy(parameters,&type,4);std::memcpy(static_cast<uint8_t*>(parameters)+4,values,8);
}
int32_t xaudio_SetOutputFilterParameters(void*,void*,const void*,uint32_t){return Ok;}
void xaudio_GetOutputFilterParameters(void* self,void*,void* parameters){xaudio_GetFilterParameters(self,parameters);}
int32_t xaudio_SetVolume(void* self,float volume,uint32_t){std::lock_guard<std::recursive_mutex> guard(lock);voice(self)->volume=volume;return Ok;}
void xaudio_GetVolume(void* self,float* volume){if(volume)*volume=voice(self)->volume;}
int32_t xaudio_SetChannelVolumes(void* self,uint32_t channels,const float* volumes,uint32_t) {
    auto v=voice(self);
    if(!volumes||channels!=v->channels)return InvalidArgument;
    std::lock_guard<std::recursive_mutex> guard(lock);v->channelVolumes.assign(volumes,volumes+channels);return Ok;
}
void xaudio_GetChannelVolumes(void* self,uint32_t channels,float* volumes) {
    auto v=voice(self);
    if(volumes)for(uint32_t c=0;c<channels;++c)volumes[c]=c<v->channelVolumes.size()?v->channelVolumes[c]:1.0f;
}
int32_t xaudio_SetOutputMatrix(void* self,void*,uint32_t sources,uint32_t destinations,const float* matrix,uint32_t) {
    auto v=voice(self);
    if(!matrix||sources!=v->channels||!destinations)return InvalidArgument;
    std::lock_guard<std::recursive_mutex> guard(lock);
    v->matrix.assign(matrix,matrix+size_t(sources)*destinations);v->matrixSet=true;
    return Ok;
}
void xaudio_GetOutputMatrix(void* self,void*,uint32_t sources,uint32_t destinations,float* matrix) {
    auto v=voice(self);
    if(!matrix)return;
    std::lock_guard<std::recursive_mutex> guard(lock);
    if(!v->matrixSet||v->matrix.size()!=size_t(sources)*destinations)defaultMatrix(*v,destinations);
    for(size_t n=0;n<size_t(sources)*destinations&&n<v->matrix.size();++n)matrix[n]=v->matrix[n];
}
void xaudio_DestroyVoice(void* self) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto v=voice(self);auto e=v->engine;
    if(v->vtable==guestXAudio2SourceVtable) {
        e->sources.erase(std::remove(e->sources.begin(),e->sources.end(),static_cast<Source*>(v)),e->sources.end());
        delete static_cast<Source*>(v);
    }else{
        // The mastering voice's stream stays with the audio output, silent.
        if(e->master==v)e->master=nullptr;
        delete static_cast<Master*>(v);
    }
}

// IXAudio2SourceVoice
int32_t xaudio_Start(void* self,uint32_t,uint32_t){std::lock_guard<std::recursive_mutex> guard(lock);source(self)->started=true;return Ok;}
int32_t xaudio_Stop(void* self,uint32_t,uint32_t){std::lock_guard<std::recursive_mutex> guard(lock);source(self)->started=false;return Ok;}
int32_t xaudio_SubmitSourceBuffer(void* self,const void* desc,const void* wma) {
    auto s=source(self);
    if(!desc)return InvalidArgument;
    if(wma)reportOnce("XAudio2: buffer WMA nao suportado");
    Buffer b;std::memcpy(&b.d,desc,sizeof b.d);
    uint32_t total=s->block?b.d.bytes/s->block:0;
    if(!b.d.data||!total)return InvalidArgument;
    b.at=b.d.playBegin;b.end=b.d.playLength?b.d.playBegin+b.d.playLength:total;
    if(b.end>total||b.at>=b.end)return InvalidArgument;
    b.loopsLeft=b.d.loopCount;
    b.loopEnd=b.d.loopLength?b.d.loopBegin+b.d.loopLength:b.end;
    if(b.loopsLeft&&(b.d.loopBegin>=b.loopEnd||b.loopEnd>b.end))return InvalidArgument;
    std::lock_guard<std::recursive_mutex> guard(lock);
    if(s->queue.size()>=MaxBuffers)return InvalidCall;
    s->queue.push_back(b);
    return Ok;
}
// Buffers not yet started leave the queue; their OnBufferEnd comes on the next pass.
int32_t xaudio_FlushSourceBuffers(void* self) {
    auto s=source(self);
    std::lock_guard<std::recursive_mutex> guard(lock);
    std::deque<Buffer> kept;
    for(auto& b:s->queue) {
        if(b.started&&s->started)kept.push_back(b);   // the one playing finishes
        else s->flushed.push_back(b.d.context);
    }
    s->queue.swap(kept);
    return Ok;
}
int32_t xaudio_Discontinuity(void* self) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto s=source(self);if(!s->queue.empty())s->queue.back().d.flags|=EndOfStream;return Ok;
}
int32_t xaudio_ExitLoop(void* self,uint32_t) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto s=source(self);if(!s->queue.empty())s->queue.front().loopsLeft=0;return Ok;
}
// XAUDIO2_VOICE_STATE: pCurrentBufferContext, BuffersQueued, SamplesPlayed.
void xaudio_GetState(void* self,void* state,uint32_t flags) {
    if(!state)return;
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto s=source(self);
    void* context=s->queue.empty()?nullptr:s->queue.front().d.context;
    uint32_t queued=uint32_t(s->queue.size());uint64_t played=(flags&NoSamplesPlayed)?0:s->played;
    auto p=static_cast<uint8_t*>(state);std::memset(p,0,24);
    std::memcpy(p,&context,8);std::memcpy(p+8,&queued,4);std::memcpy(p+16,&played,8);
}
int32_t xaudio_SetFrequencyRatio(void* self,float ratio,uint32_t) {
    auto s=source(self);
    if(!(ratio>0))return InvalidArgument;
    std::lock_guard<std::recursive_mutex> guard(lock);
    s->ratio=std::min(ratio,s->maxRatio>0?s->maxRatio:ratio);return Ok;
}
void xaudio_GetFrequencyRatio(void* self,float* ratio){if(ratio)*ratio=source(self)->ratio;}
int32_t xaudio_SetSourceSampleRate(void* self,uint32_t rate) {
    if(!rate)return InvalidArgument;
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto s=source(self);if(!s->queue.empty())return InvalidCall;
    s->rate=rate;return Ok;
}

// IXAudio2MasteringVoice
int32_t xaudio_GetChannelMask(void* self,uint32_t* mask) {
    if(!mask)return Pointer;
    uint32_t channels=voice(self)->channels;
    *mask=channels==1?0x4:channels==2?0x3:channels==4?0x33:channels==6?0x3f:channels==8?0x63f:(1u<<channels)-1;
    return Ok;
}
}
