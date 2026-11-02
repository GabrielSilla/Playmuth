#include "Win32.h"
#include <algorithm>
#include <cstring>
#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT
using namespace azimuth::win32;
extern "C" void* win_CommandLineToArgvW(const uint16_t* command,int32_t* count){
    if(!command||!count){setLastError(ErrorInvalidParameter);return nullptr;}
    std::u16string line(reinterpret_cast<const char16_t*>(command));
    if(line.empty())line=u"E:\\PC\\FH2\\azimuth.exe";
    std::vector<std::u16string> args;size_t at=0;
    if(line[0]==u' '||line[0]==u'\t')args.emplace_back();
    while(at<line.size()){
        while(at<line.size()&&(line[at]==u' '||line[at]==u'\t'))++at;
        if(at==line.size())break;
        std::u16string arg;bool quoted=false;
        while(at<line.size()){
            if(!quoted&&(line[at]==u' '||line[at]==u'\t'))break;
            size_t slash=0;while(at<line.size()&&line[at]==u'\\'){++at;++slash;}
            if(at<line.size()&&line[at]==u'"'){
                arg.append(slash/2,u'\\');
                if(slash%2)arg+=u'"';else quoted=!quoted;
                ++at;
            }else {arg.append(slash,u'\\');if(at<line.size()){if(!quoted&&(line[at]==u' '||line[at]==u'\t'))break;arg+=line[at++];}}
        }
        args.push_back(std::move(arg));
    }
    size_t bytes=(args.size()+1)*sizeof(void*);for(auto& arg:args)bytes+=(arg.size()+1)*2;
    auto array=static_cast<uint16_t**>(win_HeapAlloc(nullptr,0,bytes));if(!array)return nullptr;
    auto text=reinterpret_cast<uint16_t*>(array+args.size()+1);
    for(size_t n=0;n<args.size();++n){array[n]=text;std::memcpy(text,args[n].c_str(),(args[n].size()+1)*2);text+=args[n].size()+1;}
    array[args.size()]=nullptr;*count=int32_t(args.size());return array;
}
extern "C" int32_t win_GetUserPreferredUILanguages(uint32_t flags,uint32_t* count,uint16_t* text,uint32_t* size){
    if(!count||!size){setLastError(ErrorInvalidParameter);return 0;}
    const char16_t* languages=(flags&4)?u"0409\0":u"en-US\0";
    uint32_t needed=(flags&4)?6:7;auto capacity=*size;*size=needed;*count=1;
    if(!text)return 1;
    if(capacity<needed){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(text,languages,needed*2);return 1;
}
extern "C" int32_t win_EqualRect(const int32_t* a,const int32_t* b){return a&&b&&!std::memcmp(a,b,16);}
extern "C" int32_t win_IntersectRect(int32_t* out,const int32_t* a,const int32_t* b){
    if(!out||!a||!b)return 0;
    int32_t result[]={std::max(a[0],b[0]),std::max(a[1],b[1]),std::min(a[2],b[2]),std::min(a[3],b[3])};
    if(result[0]>=result[2]||result[1]>=result[3]){std::memset(out,0,16);return 0;}
    std::memcpy(out,result,16);return 1;
}
extern "C" int32_t win_PtInRect(const int32_t* r,uint64_t point){int32_t x=int32_t(point),y=int32_t(point>>32);return r&&x>=r[0]&&x<r[2]&&y>=r[1]&&y<r[3];}
extern "C" int32_t win_GetDisplayConfigBufferSizes(uint32_t,uint32_t* paths,uint32_t* modes){if(paths)*paths=0;if(modes)*modes=0;return 50;}
extern "C" int32_t win_QueryDisplayConfig(uint32_t,uint32_t*,void*,uint32_t*,void*,void*){return 50;}
extern "C" int32_t win_DisplayConfigGetDeviceInfo(void*){return 50;}
extern "C" void* win_RegisterDeviceNotificationW(void*,void*,uint32_t){setLastError(120);return nullptr;}
// DWriteCreateFactory: Dwrite.cpp.
extern "C" int32_t win_DirectInput8Create(void*,uint32_t,const void*,void** out,void*){if(out)*out=nullptr;return int32_t(0x80040154);}
extern "C" void* win_ShellExecuteW(void*,const uint16_t*,const uint16_t*,const uint16_t*,const uint16_t*,int32_t){setLastError(1155);return reinterpret_cast<void*>(31);}
namespace {struct MtaCookie:Object {};struct StringReference {const uint16_t* text;uint32_t size;};}
extern "C" int32_t win_CoIncrementMTAUsage(void** cookie){if(!cookie)return int32_t(0x80004003);*cookie=addHandle(std::make_shared<MtaCookie>());return 0;}
extern "C" int32_t win_CoDecrementMTAUsage(void* cookie){if(!findHandleOf<MtaCookie>(cookie))return int32_t(0x80070057);removeHandle(cookie);return 0;}
extern "C" int32_t win_WindowsCreateStringReference(const uint16_t* source,uint32_t size,void* header,void** string){
    if(!header||!string||(!source&&size))return int32_t(0x80070057);
    *string=nullptr;if(!size)return 0;if(source[size])return int32_t(0x80000017);
    std::memset(header,0,24);*static_cast<StringReference*>(header)={source,size};*string=header;return 0;
}
int32_t gamepadStatics(const void* id,void** out);   // Xinput.cpp
extern "C" int32_t win_RoGetActivationFactory(void* name,const void* id,void** out){
    if(out)*out=nullptr;
    auto reference=static_cast<StringReference*>(name);
    static const std::u16string gamepad=u"Windows.Gaming.Input.Gamepad";
    if(reference&&std::u16string(reinterpret_cast<const char16_t*>(reference->text),reference->size)==gamepad)return gamepadStatics(id,out);
    log("WinRT: classe nao registrada: "+(reference?utf8(reference->text,reference->size):std::string("(vazia)")));
    return int32_t(0x80040154);
}
extern "C" uint32_t win_timeBeginPeriod(uint32_t period){return period>=1&&period<=1000?0:97;}
extern "C" uint32_t win_timeEndPeriod(uint32_t period){return period>=1&&period<=1000?0:97;}
extern "C" int32_t win_TzSpecificLocalTimeToSystemTime(const void*,const uint16_t* in,uint16_t* out){if(!in||!out)return 0;std::memcpy(out,in,16);return 1;}
