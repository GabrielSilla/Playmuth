// The vtables of the host's WASAPI objects (Audio.cpp) as Windows code sees
// them: one Windows x64 function per method, forwarding to the host's System V
// implementation. Compiled for x86_64-pc-freebsd like guest_abi.c. Slots are
// mmdeviceapi.h's and audioclient.h's order; a method the host does not
// implement returns E_NOTIMPL and names itself in the log.
#include <stdint.h>
#include "AudioAbi.h"

void hostGateNamed(const char* name);

#define METHOD(name,ret,params,args) \
    ret audio_##name params; \
    static __attribute__((ms_abi)) ret ms_##name params {hostGateNamed(#name);return audio_##name args;}
#define MISSING(object,slot) \
    static __attribute__((ms_abi)) int32_t ms_##object##_##slot(void){hostGateNamed(#object);return audioMissing(#object,slot);}
#define UNKNOWN (const void*)&ms_QueryInterface,(const void*)&ms_AddRef,(const void*)&ms_Release

// IUnknown, shared by every object.
METHOD(QueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(AddRef,uint32_t,(void* self),(self))
METHOD(Release,uint32_t,(void* self),(self))

// IMMDeviceEnumerator
METHOD(EnumAudioEndpoints,int32_t,(void* self,uint32_t flow,uint32_t states,void** collection),(self,flow,states,collection))
METHOD(GetDefaultAudioEndpoint,int32_t,(void* self,uint32_t flow,uint32_t role,void** device),(self,flow,role,device))
METHOD(GetDevice,int32_t,(void* self,const uint16_t* id,void** device),(self,id,device))
METHOD(RegisterEndpointNotificationCallback,int32_t,(void* self,void* client),(self,client))
METHOD(UnregisterEndpointNotificationCallback,int32_t,(void* self,void* client),(self,client))
const void* const guestAudioEnumeratorVtable[AudioEnumeratorSlots]={
    UNKNOWN,(const void*)&ms_EnumAudioEndpoints,(const void*)&ms_GetDefaultAudioEndpoint,(const void*)&ms_GetDevice,
    (const void*)&ms_RegisterEndpointNotificationCallback,(const void*)&ms_UnregisterEndpointNotificationCallback,
};

// IMMDeviceCollection
METHOD(CollectionGetCount,int32_t,(void* self,uint32_t* count),(self,count))
METHOD(CollectionItem,int32_t,(void* self,uint32_t index,void** device),(self,index,device))
const void* const guestAudioCollectionVtable[AudioCollectionSlots]={
    UNKNOWN,(const void*)&ms_CollectionGetCount,(const void*)&ms_CollectionItem,
};

// IMMDevice
METHOD(Activate,int32_t,(void* self,const void* id,uint32_t context,void* parameters,void** out),(self,id,context,parameters,out))
METHOD(OpenPropertyStore,int32_t,(void* self,uint32_t access,void** store),(self,access,store))
METHOD(GetId,int32_t,(void* self,uint16_t** id),(self,id))
METHOD(GetState,int32_t,(void* self,uint32_t* state),(self,state))
const void* const guestAudioDeviceVtable[AudioDeviceSlots]={
    UNKNOWN,(const void*)&ms_Activate,(const void*)&ms_OpenPropertyStore,(const void*)&ms_GetId,(const void*)&ms_GetState,
};

// IPropertyStore
METHOD(StoreGetCount,int32_t,(void* self,uint32_t* count),(self,count))
METHOD(StoreGetAt,int32_t,(void* self,uint32_t index,void* key),(self,index,key))
METHOD(StoreGetValue,int32_t,(void* self,const void* key,void* value),(self,key,value))
MISSING(Store,6)   // SetValue
MISSING(Store,7)   // Commit
const void* const guestAudioStoreVtable[AudioStoreSlots]={
    UNKNOWN,(const void*)&ms_StoreGetCount,(const void*)&ms_StoreGetAt,(const void*)&ms_StoreGetValue,
    (const void*)&ms_Store_6,(const void*)&ms_Store_7,
};

// IAudioClient .. IAudioClient3
METHOD(Initialize,int32_t,(void* self,uint32_t share,uint32_t flags,int64_t duration,int64_t periodicity,const void* format,const void* session),
       (self,share,flags,duration,periodicity,format,session))
METHOD(GetBufferSize,int32_t,(void* self,uint32_t* frames),(self,frames))
METHOD(GetStreamLatency,int32_t,(void* self,int64_t* latency),(self,latency))
METHOD(GetCurrentPadding,int32_t,(void* self,uint32_t* frames),(self,frames))
METHOD(IsFormatSupported,int32_t,(void* self,uint32_t share,const void* format,void** closest),(self,share,format,closest))
METHOD(GetMixFormat,int32_t,(void* self,void** format),(self,format))
METHOD(GetDevicePeriod,int32_t,(void* self,int64_t* period,int64_t* minimum),(self,period,minimum))
METHOD(Start,int32_t,(void* self),(self))
METHOD(Stop,int32_t,(void* self),(self))
METHOD(Reset,int32_t,(void* self),(self))
METHOD(SetEventHandle,int32_t,(void* self,void* event),(self,event))
METHOD(GetService,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(IsOffloadCapable,int32_t,(void* self,uint32_t category,int32_t* capable),(self,category,capable))
METHOD(SetClientProperties,int32_t,(void* self,const void* properties),(self,properties))
METHOD(GetBufferSizeLimits,int32_t,(void* self,const void* format,int32_t event,int64_t* minimum,int64_t* maximum),(self,format,event,minimum,maximum))
MISSING(Client,18)   // GetSharedModeEnginePeriod
MISSING(Client,19)   // GetCurrentSharedModeEnginePeriod
MISSING(Client,20)   // InitializeSharedAudioStream
const void* const guestAudioClientVtable[AudioClientSlots]={
    UNKNOWN,(const void*)&ms_Initialize,(const void*)&ms_GetBufferSize,(const void*)&ms_GetStreamLatency,
    (const void*)&ms_GetCurrentPadding,(const void*)&ms_IsFormatSupported,(const void*)&ms_GetMixFormat,
    (const void*)&ms_GetDevicePeriod,(const void*)&ms_Start,(const void*)&ms_Stop,(const void*)&ms_Reset,
    (const void*)&ms_SetEventHandle,(const void*)&ms_GetService,
    (const void*)&ms_IsOffloadCapable,(const void*)&ms_SetClientProperties,(const void*)&ms_GetBufferSizeLimits,
    (const void*)&ms_Client_18,(const void*)&ms_Client_19,(const void*)&ms_Client_20,
};

// IAudioRenderClient
METHOD(GetBuffer,int32_t,(void* self,uint32_t frames,uint8_t** data),(self,frames,data))
METHOD(ReleaseBuffer,int32_t,(void* self,uint32_t frames,uint32_t flags),(self,frames,flags))
const void* const guestAudioRenderVtable[AudioRenderSlots]={
    UNKNOWN,(const void*)&ms_GetBuffer,(const void*)&ms_ReleaseBuffer,
};

// IAudioClock
METHOD(GetFrequency,int32_t,(void* self,uint64_t* frequency),(self,frequency))
METHOD(GetPosition,int32_t,(void* self,uint64_t* position,uint64_t* counter),(self,position,counter))
METHOD(GetCharacteristics,int32_t,(void* self,uint32_t* characteristics),(self,characteristics))
const void* const guestAudioClockVtable[AudioClockSlots]={
    UNKNOWN,(const void*)&ms_GetFrequency,(const void*)&ms_GetPosition,(const void*)&ms_GetCharacteristics,
};

// ISimpleAudioVolume
METHOD(SetMasterVolume,int32_t,(void* self,float level,const void* context),(self,level,context))
METHOD(GetMasterVolume,int32_t,(void* self,float* level),(self,level))
METHOD(SetMute,int32_t,(void* self,int32_t mute,const void* context),(self,mute,context))
METHOD(GetMute,int32_t,(void* self,int32_t* mute),(self,mute))
const void* const guestAudioSimpleVolumeVtable[AudioSimpleVolumeSlots]={
    UNKNOWN,(const void*)&ms_SetMasterVolume,(const void*)&ms_GetMasterVolume,(const void*)&ms_SetMute,(const void*)&ms_GetMute,
};

// IAudioStreamVolume
METHOD(GetChannelCount,int32_t,(void* self,uint32_t* count),(self,count))
METHOD(SetChannelVolume,int32_t,(void* self,uint32_t channel,float level),(self,channel,level))
METHOD(GetChannelVolume,int32_t,(void* self,uint32_t channel,float* level),(self,channel,level))
METHOD(SetAllVolumes,int32_t,(void* self,uint32_t count,const float* levels),(self,count,levels))
METHOD(GetAllVolumes,int32_t,(void* self,uint32_t count,float* levels),(self,count,levels))
const void* const guestAudioStreamVolumeVtable[AudioStreamVolumeSlots]={
    UNKNOWN,(const void*)&ms_GetChannelCount,(const void*)&ms_SetChannelVolume,(const void*)&ms_GetChannelVolume,
    (const void*)&ms_SetAllVolumes,(const void*)&ms_GetAllVolumes,
};
