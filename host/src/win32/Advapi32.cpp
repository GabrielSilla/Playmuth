// The registry and the kernel-mode display interface (D3DKMT) for the DLLs
// the host loads. vkd3d-proton reads optional settings from the registry and
// uses D3DKMT for resources shared between processes and devices: the host
// has neither, so every key is missing and every D3DKMT call unsupported.
#include "Win32.h"
#include <string>

namespace {
constexpr int32_t ErrorFileNotFound=2,StatusNotSupported=int32_t(0xc00000bb);
}

extern "C" int32_t win_RegOpenKeyA(void*,const char* name,void** result) {
    if(result)*result=nullptr;
    azimuth::win32::log(std::string("RegOpenKeyA: ")+(name?name:"(nulo)")+" nao existe no host");
    return ErrorFileNotFound;
}
extern "C" int32_t win_RegQueryValueExA(void*,const char*,uint32_t*,uint32_t*,uint8_t*,uint32_t*){return ErrorFileNotFound;}
extern "C" int32_t win_RegCloseKey(void*){return 0;}
extern "C" int32_t win_RegNotifyChangeKeyValue(void*,int32_t,uint32_t,void*,int32_t){return ErrorFileNotFound;}

extern "C" int32_t win_D3DKMTCloseAdapter(const void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTCreateDevice(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTDestroyAllocation(const void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTDestroyDevice(const void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTDestroyKeyedMutex(const void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTDestroySynchronizationObject(const void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTEscape(const void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTOpenAdapterFromLuid(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTOpenResource2(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTOpenResourceFromNtHandle(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTOpenSyncObjectFromNtHandle(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTQueryResourceInfo(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTQueryResourceInfoFromNtHandle(void*){return StatusNotSupported;}
extern "C" int32_t win_D3DKMTShareObjects(uint32_t,const void*,void*,uint32_t,void**){return StatusNotSupported;}
