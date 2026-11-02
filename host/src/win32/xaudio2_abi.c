// The vtables of the host's XAudio2 objects (XAudio2.cpp) as Windows code sees
// them: one Windows x64 function per method, forwarding to the host's System V
// implementation. Compiled for x86_64-pc-freebsd like guest_abi.c. Slots are
// xaudio2.h's (2.9) order; voices have no IUnknown.
#include <stdint.h>
#include "XAudio2Abi.h"

void hostGateNamed(const char* name);

#define METHOD(name,ret,params,args) \
    ret xaudio_##name params; \
    static __attribute__((ms_abi)) ret ms_##name params {hostGateNamed(#name);return xaudio_##name args;}
#define VOIDMETHOD(name,params,args) \
    void xaudio_##name params; \
    static __attribute__((ms_abi)) void ms_##name params {hostGateNamed(#name);xaudio_##name args;}

// IXAudio2
METHOD(QueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(AddRef,uint32_t,(void* self),(self))
METHOD(Release,uint32_t,(void* self),(self))
METHOD(RegisterForCallbacks,int32_t,(void* self,void* callback),(self,callback))
VOIDMETHOD(UnregisterForCallbacks,(void* self,void* callback),(self,callback))
METHOD(CreateSourceVoice,int32_t,(void* self,void** voice,const void* format,uint32_t flags,float maxRatio,void* callback,const void* sends,const void* effects),
       (self,voice,format,flags,maxRatio,callback,sends,effects))
METHOD(CreateSubmixVoice,int32_t,(void* self,void** voice,uint32_t channels,uint32_t rate,uint32_t flags,uint32_t stage,const void* sends,const void* effects),
       (self,voice,channels,rate,flags,stage,sends,effects))
METHOD(CreateMasteringVoice,int32_t,(void* self,void** voice,uint32_t channels,uint32_t rate,uint32_t flags,const uint16_t* device,const void* effects,uint32_t category),
       (self,voice,channels,rate,flags,device,effects,category))
METHOD(StartEngine,int32_t,(void* self),(self))
VOIDMETHOD(StopEngine,(void* self),(self))
METHOD(CommitChanges,int32_t,(void* self,uint32_t operation),(self,operation))
VOIDMETHOD(GetPerformanceData,(void* self,void* data),(self,data))
VOIDMETHOD(SetDebugConfiguration,(void* self,const void* configuration,void* reserved),(self,configuration,reserved))
const void* const guestXAudio2EngineVtable[XAudio2EngineSlots]={
    (const void*)&ms_QueryInterface,(const void*)&ms_AddRef,(const void*)&ms_Release,
    (const void*)&ms_RegisterForCallbacks,(const void*)&ms_UnregisterForCallbacks,(const void*)&ms_CreateSourceVoice,
    (const void*)&ms_CreateSubmixVoice,(const void*)&ms_CreateMasteringVoice,(const void*)&ms_StartEngine,(const void*)&ms_StopEngine,
    (const void*)&ms_CommitChanges,(const void*)&ms_GetPerformanceData,(const void*)&ms_SetDebugConfiguration,
};

// IXAudio2Voice, shared by both voices.
VOIDMETHOD(GetVoiceDetails,(void* self,void* details),(self,details))
METHOD(SetOutputVoices,int32_t,(void* self,const void* sends),(self,sends))
METHOD(SetEffectChain,int32_t,(void* self,const void* effects),(self,effects))
METHOD(EnableEffect,int32_t,(void* self,uint32_t index,uint32_t operation),(self,index,operation))
METHOD(DisableEffect,int32_t,(void* self,uint32_t index,uint32_t operation),(self,index,operation))
VOIDMETHOD(GetEffectState,(void* self,uint32_t index,int32_t* enabled),(self,index,enabled))
METHOD(SetEffectParameters,int32_t,(void* self,uint32_t index,const void* parameters,uint32_t size,uint32_t operation),(self,index,parameters,size,operation))
METHOD(GetEffectParameters,int32_t,(void* self,uint32_t index,void* parameters,uint32_t size),(self,index,parameters,size))
METHOD(SetFilterParameters,int32_t,(void* self,const void* parameters,uint32_t operation),(self,parameters,operation))
VOIDMETHOD(GetFilterParameters,(void* self,void* parameters),(self,parameters))
METHOD(SetOutputFilterParameters,int32_t,(void* self,void* destination,const void* parameters,uint32_t operation),(self,destination,parameters,operation))
VOIDMETHOD(GetOutputFilterParameters,(void* self,void* destination,void* parameters),(self,destination,parameters))
METHOD(SetVolume,int32_t,(void* self,float volume,uint32_t operation),(self,volume,operation))
VOIDMETHOD(GetVolume,(void* self,float* volume),(self,volume))
METHOD(SetChannelVolumes,int32_t,(void* self,uint32_t channels,const float* volumes,uint32_t operation),(self,channels,volumes,operation))
VOIDMETHOD(GetChannelVolumes,(void* self,uint32_t channels,float* volumes),(self,channels,volumes))
METHOD(SetOutputMatrix,int32_t,(void* self,void* destination,uint32_t sources,uint32_t destinations,const float* matrix,uint32_t operation),
       (self,destination,sources,destinations,matrix,operation))
VOIDMETHOD(GetOutputMatrix,(void* self,void* destination,uint32_t sources,uint32_t destinations,float* matrix),(self,destination,sources,destinations,matrix))
VOIDMETHOD(DestroyVoice,(void* self),(self))
#define VOICE (const void*)&ms_GetVoiceDetails,(const void*)&ms_SetOutputVoices,(const void*)&ms_SetEffectChain,(const void*)&ms_EnableEffect, \
    (const void*)&ms_DisableEffect,(const void*)&ms_GetEffectState,(const void*)&ms_SetEffectParameters,(const void*)&ms_GetEffectParameters, \
    (const void*)&ms_SetFilterParameters,(const void*)&ms_GetFilterParameters,(const void*)&ms_SetOutputFilterParameters, \
    (const void*)&ms_GetOutputFilterParameters,(const void*)&ms_SetVolume,(const void*)&ms_GetVolume,(const void*)&ms_SetChannelVolumes, \
    (const void*)&ms_GetChannelVolumes,(const void*)&ms_SetOutputMatrix,(const void*)&ms_GetOutputMatrix,(const void*)&ms_DestroyVoice

// IXAudio2SourceVoice
METHOD(Start,int32_t,(void* self,uint32_t flags,uint32_t operation),(self,flags,operation))
METHOD(Stop,int32_t,(void* self,uint32_t flags,uint32_t operation),(self,flags,operation))
METHOD(SubmitSourceBuffer,int32_t,(void* self,const void* buffer,const void* wma),(self,buffer,wma))
METHOD(FlushSourceBuffers,int32_t,(void* self),(self))
METHOD(Discontinuity,int32_t,(void* self),(self))
METHOD(ExitLoop,int32_t,(void* self,uint32_t operation),(self,operation))
VOIDMETHOD(GetState,(void* self,void* state,uint32_t flags),(self,state,flags))
METHOD(SetFrequencyRatio,int32_t,(void* self,float ratio,uint32_t operation),(self,ratio,operation))
VOIDMETHOD(GetFrequencyRatio,(void* self,float* ratio),(self,ratio))
METHOD(SetSourceSampleRate,int32_t,(void* self,uint32_t rate),(self,rate))
const void* const guestXAudio2SourceVtable[XAudio2SourceSlots]={
    VOICE,(const void*)&ms_Start,(const void*)&ms_Stop,(const void*)&ms_SubmitSourceBuffer,(const void*)&ms_FlushSourceBuffers,
    (const void*)&ms_Discontinuity,(const void*)&ms_ExitLoop,(const void*)&ms_GetState,(const void*)&ms_SetFrequencyRatio,
    (const void*)&ms_GetFrequencyRatio,(const void*)&ms_SetSourceSampleRate,
};

// IXAudio2MasteringVoice
METHOD(GetChannelMask,int32_t,(void* self,uint32_t* mask),(self,mask))
const void* const guestXAudio2MasterVtable[XAudio2MasterSlots]={VOICE,(const void*)&ms_GetChannelMask};

// The XAudio2 thread: an engine thread (made by CreateThread) running the host's mixer.
uint32_t xaudio_ThreadMain(void* parameter);
static __attribute__((ms_abi)) uint32_t ms_ThreadMain(void* parameter){hostGateNamed("XAudio2Thread");return xaudio_ThreadMain(parameter);}
const void* const guestXAudio2ThreadStart=(const void*)&ms_ThreadMain;
