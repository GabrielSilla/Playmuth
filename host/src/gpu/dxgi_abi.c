// The vtables of the host's DXGI objects (Dxgi.cpp) as Windows code sees
// them: one Windows x64 function per method, forwarding to the host's System V
// implementation. Compiled for x86_64-pc-freebsd like guest_abi.c. Slots are
// dxgi.h's order; a method the host does not implement returns E_NOTIMPL and
// names itself in the log.
#include <stdint.h>
#include "DxgiAbi.h"

void hostGate(void);

#define METHOD(name,ret,params,args) \
    ret dxgi_##name params; \
    static __attribute__((ms_abi)) ret ms_##name params {hostGate();return dxgi_##name args;}
#define MISSING(object,slot) \
    static __attribute__((ms_abi)) int32_t ms_##object##_##slot(void){hostGate();return dxgiMissing(#object,slot);}

// IUnknown and IDXGIObject, shared by both objects.
METHOD(QueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(AddRef,uint32_t,(void* self),(self))
METHOD(Release,uint32_t,(void* self),(self))
METHOD(SetPrivateData,int32_t,(void* self,const void* id,uint32_t size,const void* data),(self,id,size,data))
METHOD(SetPrivateDataInterface,int32_t,(void* self,const void* id,void* object),(self,id,object))
METHOD(GetPrivateData,int32_t,(void* self,const void* id,uint32_t* size,void* data),(self,id,size,data))
METHOD(GetParent,int32_t,(void* self,const void* id,void** parent),(self,id,parent))

// IDXGIFactory .. IDXGIFactory6
METHOD(EnumAdapters,int32_t,(void* self,uint32_t index,void** adapter),(self,index,adapter))
METHOD(MakeWindowAssociation,int32_t,(void* self,void* window,uint32_t flags),(self,window,flags))
METHOD(GetWindowAssociation,int32_t,(void* self,void** window),(self,window))
METHOD(CreateSwapChain,int32_t,(void* self,void* device,const void* desc,void** chain),(self,device,desc,chain))
MISSING(Factory,11)   // CreateSoftwareAdapter
METHOD(EnumAdapters1,int32_t,(void* self,uint32_t index,void** adapter),(self,index,adapter))
METHOD(IsCurrent,int32_t,(void* self),(self))
METHOD(IsWindowedStereoEnabled,int32_t,(void* self),(self))
METHOD(CreateSwapChainForHwnd,int32_t,(void* self,void* device,void* window,const void* desc,const void* fullscreen,void* output,void** chain),
       (self,device,window,desc,fullscreen,output,chain))
MISSING(Factory,16)   // CreateSwapChainForCoreWindow
MISSING(Factory,17)   // GetSharedResourceAdapterLuid
MISSING(Factory,18)   // RegisterStereoStatusWindow
MISSING(Factory,19)   // RegisterStereoStatusEvent
MISSING(Factory,20)   // UnregisterStereoStatus
MISSING(Factory,21)   // RegisterOcclusionStatusWindow
MISSING(Factory,22)   // RegisterOcclusionStatusEvent
MISSING(Factory,23)   // UnregisterOcclusionStatus
MISSING(Factory,24)   // CreateSwapChainForComposition
METHOD(GetCreationFlags,uint32_t,(void* self),(self))
METHOD(EnumAdapterByLuid,int32_t,(void* self,uint64_t luid,const void* id,void** adapter),(self,luid,id,adapter))
MISSING(Factory,27)   // EnumWarpAdapter
METHOD(CheckFeatureSupport,int32_t,(void* self,uint32_t feature,void* data,uint32_t size),(self,feature,data,size))
METHOD(EnumAdapterByGpuPreference,int32_t,(void* self,uint32_t index,uint32_t preference,const void* id,void** adapter),(self,index,preference,id,adapter))

// IDXGIAdapter .. IDXGIAdapter4
METHOD(EnumOutputs,int32_t,(void* self,uint32_t index,void** output),(self,index,output))
METHOD(GetDesc,int32_t,(void* self,void* desc),(self,desc))
METHOD(CheckInterfaceSupport,int32_t,(void* self,const void* id,int64_t* version),(self,id,version))
METHOD(GetDesc1,int32_t,(void* self,void* desc),(self,desc))
METHOD(GetDesc2,int32_t,(void* self,void* desc),(self,desc))
MISSING(Adapter,12)   // RegisterHardwareContentProtectionTeardownStatusEvent
MISSING(Adapter,13)   // UnregisterHardwareContentProtectionTeardownStatus
METHOD(QueryVideoMemoryInfo,int32_t,(void* self,uint32_t node,uint32_t group,void* info),(self,node,group,info))
METHOD(SetVideoMemoryReservation,int32_t,(void* self,uint32_t node,uint32_t group,uint64_t reservation),(self,node,group,reservation))
MISSING(Adapter,16)   // RegisterVideoMemoryBudgetChangeNotificationEvent
MISSING(Adapter,17)   // UnregisterVideoMemoryBudgetChangeNotification
METHOD(GetDesc3,int32_t,(void* self,void* desc),(self,desc))

#define COMMON (const void*)&ms_QueryInterface,(const void*)&ms_AddRef,(const void*)&ms_Release, \
    (const void*)&ms_SetPrivateData,(const void*)&ms_SetPrivateDataInterface,(const void*)&ms_GetPrivateData,(const void*)&ms_GetParent
const void* const guestDxgiFactoryVtable[DxgiFactorySlots]={
    COMMON,
    (const void*)&ms_EnumAdapters,(const void*)&ms_MakeWindowAssociation,(const void*)&ms_GetWindowAssociation,
    (const void*)&ms_CreateSwapChain,(const void*)&ms_Factory_11,
    (const void*)&ms_EnumAdapters1,(const void*)&ms_IsCurrent,
    (const void*)&ms_IsWindowedStereoEnabled,(const void*)&ms_CreateSwapChainForHwnd,(const void*)&ms_Factory_16,(const void*)&ms_Factory_17,
    (const void*)&ms_Factory_18,(const void*)&ms_Factory_19,(const void*)&ms_Factory_20,(const void*)&ms_Factory_21,
    (const void*)&ms_Factory_22,(const void*)&ms_Factory_23,(const void*)&ms_Factory_24,
    (const void*)&ms_GetCreationFlags,
    (const void*)&ms_EnumAdapterByLuid,(const void*)&ms_Factory_27,
    (const void*)&ms_CheckFeatureSupport,
    (const void*)&ms_EnumAdapterByGpuPreference,
};
const void* const guestDxgiAdapterVtable[DxgiAdapterSlots]={
    COMMON,
    (const void*)&ms_EnumOutputs,(const void*)&ms_GetDesc,(const void*)&ms_CheckInterfaceSupport,
    (const void*)&ms_GetDesc1,
    (const void*)&ms_GetDesc2,
    (const void*)&ms_Adapter_12,(const void*)&ms_Adapter_13,(const void*)&ms_QueryVideoMemoryInfo,
    (const void*)&ms_SetVideoMemoryReservation,(const void*)&ms_Adapter_16,(const void*)&ms_Adapter_17,
    (const void*)&ms_GetDesc3,
};

// IDXGIVkSurfaceFactory, handed to vkd3d-proton's swap chain factory.
METHOD(SurfaceQueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(SurfaceAddRef,uint32_t,(void* self),(self))
METHOD(SurfaceRelease,uint32_t,(void* self),(self))
METHOD(CreateSurface,int32_t,(void* self,void* instance,void* adapter,uint64_t* surface),(self,instance,adapter,surface))
const void* const guestDxgiSurfaceFactoryVtable[DxgiSurfaceFactorySlots]={
    (const void*)&ms_SurfaceQueryInterface,(const void*)&ms_SurfaceAddRef,(const void*)&ms_SurfaceRelease,(const void*)&ms_CreateSurface,
};

// IDXGISwapChain .. IDXGISwapChain4 over vkd3d-proton's IDXGIVkSwapChain (SwapChain.cpp).
METHOD(ChainQueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(ChainAddRef,uint32_t,(void* self),(self))
METHOD(ChainRelease,uint32_t,(void* self),(self))
METHOD(ChainSetPrivateData,int32_t,(void* self,const void* id,uint32_t size,const void* data),(self,id,size,data))
METHOD(ChainSetPrivateDataInterface,int32_t,(void* self,const void* id,void* object),(self,id,object))
METHOD(ChainGetPrivateData,int32_t,(void* self,const void* id,uint32_t* size,void* data),(self,id,size,data))
METHOD(ChainGetParent,int32_t,(void* self,const void* id,void** parent),(self,id,parent))
METHOD(ChainGetDevice,int32_t,(void* self,const void* id,void** device),(self,id,device))
METHOD(Present,int32_t,(void* self,uint32_t interval,uint32_t flags),(self,interval,flags))
METHOD(GetBuffer,int32_t,(void* self,uint32_t index,const void* id,void** buffer),(self,index,id,buffer))
METHOD(SetFullscreenState,int32_t,(void* self,int32_t fullscreen,void* output),(self,fullscreen,output))
METHOD(GetFullscreenState,int32_t,(void* self,int32_t* fullscreen,void** output),(self,fullscreen,output))
METHOD(ChainGetDesc,int32_t,(void* self,void* desc),(self,desc))
METHOD(ResizeBuffers,int32_t,(void* self,uint32_t count,uint32_t width,uint32_t height,uint32_t format,uint32_t flags),(self,count,width,height,format,flags))
METHOD(ResizeTarget,int32_t,(void* self,const void* mode),(self,mode))
METHOD(GetContainingOutput,int32_t,(void* self,void** output),(self,output))
METHOD(GetFrameStatistics,int32_t,(void* self,void* statistics),(self,statistics))
METHOD(GetLastPresentCount,int32_t,(void* self,uint32_t* count),(self,count))
METHOD(ChainGetDesc1,int32_t,(void* self,void* desc),(self,desc))
METHOD(GetFullscreenDesc,int32_t,(void* self,void* desc),(self,desc))
METHOD(GetHwnd,int32_t,(void* self,void** window),(self,window))
MISSING(Chain,21)   // GetCoreWindow
METHOD(Present1,int32_t,(void* self,uint32_t interval,uint32_t flags,const void* parameters),(self,interval,flags,parameters))
METHOD(IsTemporaryMonoSupported,int32_t,(void* self),(self))
METHOD(GetRestrictToOutput,int32_t,(void* self,void** output),(self,output))
METHOD(SetBackgroundColor,int32_t,(void* self,const void* color),(self,color))
METHOD(GetBackgroundColor,int32_t,(void* self,void* color),(self,color))
METHOD(SetRotation,int32_t,(void* self,uint32_t rotation),(self,rotation))
METHOD(GetRotation,int32_t,(void* self,uint32_t* rotation),(self,rotation))
METHOD(SetSourceSize,int32_t,(void* self,uint32_t width,uint32_t height),(self,width,height))
METHOD(GetSourceSize,int32_t,(void* self,uint32_t* width,uint32_t* height),(self,width,height))
METHOD(SetMaximumFrameLatency,int32_t,(void* self,uint32_t latency),(self,latency))
METHOD(GetMaximumFrameLatency,int32_t,(void* self,uint32_t* latency),(self,latency))
METHOD(GetFrameLatencyWaitableObject,void*,(void* self),(self))
MISSING(Chain,34)   // SetMatrixTransform
MISSING(Chain,35)   // GetMatrixTransform
METHOD(GetCurrentBackBufferIndex,uint32_t,(void* self),(self))
METHOD(CheckColorSpaceSupport,int32_t,(void* self,uint32_t space,uint32_t* support),(self,space,support))
METHOD(SetColorSpace1,int32_t,(void* self,uint32_t space),(self,space))
METHOD(ResizeBuffers1,int32_t,(void* self,uint32_t count,uint32_t width,uint32_t height,uint32_t format,uint32_t flags,const uint32_t* masks,void* const* queues),
       (self,count,width,height,format,flags,masks,queues))
METHOD(SetHDRMetaData,int32_t,(void* self,uint32_t type,uint32_t size,const void* data),(self,type,size,data))
const void* const guestDxgiSwapChainVtable[DxgiSwapChainSlots]={
    (const void*)&ms_ChainQueryInterface,(const void*)&ms_ChainAddRef,(const void*)&ms_ChainRelease,
    (const void*)&ms_ChainSetPrivateData,(const void*)&ms_ChainSetPrivateDataInterface,(const void*)&ms_ChainGetPrivateData,
    (const void*)&ms_ChainGetParent,(const void*)&ms_ChainGetDevice,
    (const void*)&ms_Present,(const void*)&ms_GetBuffer,(const void*)&ms_SetFullscreenState,(const void*)&ms_GetFullscreenState,
    (const void*)&ms_ChainGetDesc,(const void*)&ms_ResizeBuffers,(const void*)&ms_ResizeTarget,(const void*)&ms_GetContainingOutput,
    (const void*)&ms_GetFrameStatistics,(const void*)&ms_GetLastPresentCount,
    (const void*)&ms_ChainGetDesc1,(const void*)&ms_GetFullscreenDesc,(const void*)&ms_GetHwnd,(const void*)&ms_Chain_21,
    (const void*)&ms_Present1,(const void*)&ms_IsTemporaryMonoSupported,(const void*)&ms_GetRestrictToOutput,
    (const void*)&ms_SetBackgroundColor,(const void*)&ms_GetBackgroundColor,(const void*)&ms_SetRotation,(const void*)&ms_GetRotation,
    (const void*)&ms_SetSourceSize,(const void*)&ms_GetSourceSize,(const void*)&ms_SetMaximumFrameLatency,(const void*)&ms_GetMaximumFrameLatency,
    (const void*)&ms_GetFrameLatencyWaitableObject,(const void*)&ms_Chain_34,(const void*)&ms_Chain_35,
    (const void*)&ms_GetCurrentBackBufferIndex,(const void*)&ms_CheckColorSpaceSupport,(const void*)&ms_SetColorSpace1,(const void*)&ms_ResizeBuffers1,
    (const void*)&ms_SetHDRMetaData,
};
