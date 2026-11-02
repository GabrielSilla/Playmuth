// The vtables of the host's Windows.Gaming.Input objects (Xinput.cpp) as
// Windows code sees them: one Windows x64 function per method, forwarding to
// the host's System V implementation. Compiled for x86_64-pc-freebsd like
// guest_abi.c. Slots are windows.gaming.input.h's order, after IInspectable's.
#include <stdint.h>
#include "GamepadAbi.h"

void hostGateNamed(const char* name);

#define METHOD(name,ret,params,args) \
    ret wgi_##name params; \
    static __attribute__((ms_abi)) ret ms_##name params {hostGateNamed(#name);return wgi_##name args;}
#define INSPECTABLE (const void*)&ms_QueryInterface,(const void*)&ms_AddRef,(const void*)&ms_Release, \
    (const void*)&ms_GetIids,(const void*)&ms_GetRuntimeClassName,(const void*)&ms_GetTrustLevel

// IUnknown and IInspectable, shared by every object.
METHOD(QueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(AddRef,uint32_t,(void* self),(self))
METHOD(Release,uint32_t,(void* self),(self))
METHOD(GetIids,int32_t,(void* self,uint32_t* count,void** ids),(self,count,ids))
METHOD(GetRuntimeClassName,int32_t,(void* self,void** name),(self,name))
METHOD(GetTrustLevel,int32_t,(void* self,int32_t* level),(self,level))

// IGamepadStatics
METHOD(AddGamepadAdded,int32_t,(void* self,void* handler,int64_t* token),(self,handler,token))
METHOD(RemoveGamepadAdded,int32_t,(void* self,int64_t token),(self,token))
METHOD(AddGamepadRemoved,int32_t,(void* self,void* handler,int64_t* token),(self,handler,token))
METHOD(RemoveGamepadRemoved,int32_t,(void* self,int64_t token),(self,token))
METHOD(GetGamepads,int32_t,(void* self,void** list),(self,list))
const void* const guestGamepadStaticsVtable[GamepadStaticsSlots]={
    INSPECTABLE,(const void*)&ms_AddGamepadAdded,(const void*)&ms_RemoveGamepadAdded,
    (const void*)&ms_AddGamepadRemoved,(const void*)&ms_RemoveGamepadRemoved,(const void*)&ms_GetGamepads,
};

// IVectorView<Gamepad>
METHOD(ListGetAt,int32_t,(void* self,uint32_t index,void** item),(self,index,item))
METHOD(ListGetSize,int32_t,(void* self,uint32_t* size),(self,size))
METHOD(ListIndexOf,int32_t,(void* self,void* item,uint32_t* index,uint8_t* found),(self,item,index,found))
METHOD(ListGetMany,int32_t,(void* self,uint32_t start,uint32_t capacity,void** items,uint32_t* actual),(self,start,capacity,items,actual))
const void* const guestGamepadListVtable[GamepadListSlots]={
    INSPECTABLE,(const void*)&ms_ListGetAt,(const void*)&ms_ListGetSize,(const void*)&ms_ListIndexOf,(const void*)&ms_ListGetMany,
};

// IGamepad. GamepadVibration (32 bytes) is passed by address, as the x64 ABI passes it.
METHOD(GetVibration,int32_t,(void* self,void* vibration),(self,vibration))
METHOD(PutVibration,int32_t,(void* self,const void* vibration),(self,vibration))
METHOD(GetCurrentReading,int32_t,(void* self,void* reading),(self,reading))
const void* const guestGamepadVtable[GamepadSlots]={
    INSPECTABLE,(const void*)&ms_GetVibration,(const void*)&ms_PutVibration,(const void*)&ms_GetCurrentReading,
};
