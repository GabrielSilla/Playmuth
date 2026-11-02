#include "Teb.h"
#include "Platform.h"
#include <cstring>
#include <unistd.h>

namespace azimuth {
namespace {
// Windows x64 offsets.
constexpr size_t TebStackBase=0x08,TebStackLimit=0x10,TebSelf=0x30,TebProcessId=0x40,TebThreadId=0x48,
                 TebTlsPointer=0x58,TebPeb=0x60,TebSize=0x2000;
constexpr size_t PebImageBase=0x10,PebProcessParameters=0x20,PebSize=0x2000;
// RTL_USER_PROCESS_PARAMETERS, in the PEB's second page. The UCRT reads its
// Flags when a _beginthreadex thread starts (__acrt_is_secure_process, bit
// 31): normalized, not a secure process.
constexpr size_t ParametersAt=0x1000,ParametersSize=0x440,ParametersFlags=0x08;
constexpr uint32_t ParametersNormalized=0x1;
template<class T>void put(uint8_t* block,size_t at,T value){std::memcpy(block+at,&value,sizeof value);}
}
ProcessBlock::ProcessBlock(uint64_t imageBase) {
    peb=platform::reserve(0,PebSize);
    auto block=static_cast<uint8_t*>(peb),parameters=block+ParametersAt;
    put(block,PebImageBase,imageBase);
    put(block,PebProcessParameters,parameters);
    put<uint32_t>(parameters,0,ParametersSize);   // MaximumLength
    put<uint32_t>(parameters,4,ParametersSize);   // Length
    put(parameters,ParametersFlags,ParametersNormalized);
}
ProcessBlock::~ProcessBlock(){platform::release(peb,PebSize);}
ThreadBlock::ThreadBlock(const ProcessBlock& process,void* stackBase,void* stackLimit,uint32_t threadId) {
    teb_=static_cast<uint8_t*>(platform::reserve(0,TebSize+TlsSlots*sizeof(void*)+TlsCapacity));
    slots_=reinterpret_cast<void**>(teb_+TebSize);
    id_=threadId;
    tls_=teb_+TebSize+TlsSlots*sizeof(void*);
    slots_[0]=tls_;
    put(teb_,TebStackBase,stackBase);
    put(teb_,TebStackLimit,stackLimit);
    put(teb_,TebSelf,teb_);
    put<uint64_t>(teb_,TebProcessId,uint64_t(getpid()));
    put<uint64_t>(teb_,TebThreadId,threadId);
    put(teb_,TebTlsPointer,slots_);
    put(teb_,TebPeb,process.peb);
}
ThreadBlock::~ThreadBlock(){platform::release(teb_,TebSize+TlsSlots*sizeof(void*)+TlsCapacity);}
int ThreadBlock::enter() const {return platform::setGsBase(teb_);}
}
