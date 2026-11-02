// Character types, case mapping, ordinal comparison and the locale the
// engine's CRT asks for. The process locale is English (United States),
// LCID 0x409, with code page 1252: what the CRT finds on a Western Windows.
#include "Win32.h"
#include "../guest_abi.h"
#include <cstring>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
constexpr uint32_t ErrorInvalidFlags=1004;
constexpr uint16_t C1Upper=0x1,C1Lower=0x2,C1Digit=0x4,C1Space=0x8,C1Punct=0x10,C1Cntrl=0x20,C1Blank=0x40,C1Xdigit=0x80,C1Alpha=0x100,C1Defined=0x200;
constexpr uint16_t C3Alpha=0x8000;

// Simple case mapping for Latin-1, Latin Extended-A, Greek and Cyrillic.
uint16_t upper(uint16_t c) {
    if(c>='a'&&c<='z')return uint16_t(c-32);
    if(c<0x80)return c;
    if((c>=0xe0&&c<=0xfe&&c!=0xf7))return uint16_t(c-32);
    if(c==0xff)return 0x178;
    if(c>=0x100&&c<=0x17f) {
        bool oddUpper=(c>=0x139&&c<=0x148)||(c>=0x179&&c<=0x17e);
        if(c==0x131||c==0x138||c==0x149||c==0x17f)return c;
        if(oddUpper)return (c%2==0)?uint16_t(c-1):c;
        return (c%2==1)?uint16_t(c-1):c;
    }
    if(c>=0x3b1&&c<=0x3c9&&c!=0x3c2)return uint16_t(c-32);
    if(c==0x3c2)return 0x3a3;
    if(c>=0x430&&c<=0x44f)return uint16_t(c-32);
    if(c>=0x450&&c<=0x45f)return uint16_t(c-80);
    return c;
}
uint16_t lower(uint16_t c) {
    if(c>='A'&&c<='Z')return uint16_t(c+32);
    if(c<0x80)return c;
    if(c>=0xc0&&c<=0xde&&c!=0xd7)return uint16_t(c+32);
    if(c==0x178)return 0xff;
    if(c>=0x100&&c<=0x17f) {
        bool oddUpper=(c>=0x139&&c<=0x148)||(c>=0x179&&c<=0x17e);
        if(c==0x130||c==0x131||c==0x138||c==0x149||c==0x17f)return c==0x130?uint16_t('i'):c;
        if(oddUpper)return (c%2==1)?uint16_t(c+1):c;
        return (c%2==0)?uint16_t(c+1):c;
    }
    if(c>=0x391&&c<=0x3a9&&c!=0x3a2)return uint16_t(c+32);
    if(c>=0x410&&c<=0x42f)return uint16_t(c+32);
    if(c>=0x400&&c<=0x40f)return uint16_t(c+80);
    return c;
}
uint16_t ctype1(uint16_t c) {
    uint16_t t=C1Defined;
    if(c<0x80) {
        if(c>='A'&&c<='Z')t|=C1Upper|C1Alpha;
        else if(c>='a'&&c<='z')t|=C1Lower|C1Alpha;
        else if(c>='0'&&c<='9')t|=C1Digit;
        else if(c==' ')t|=C1Space|C1Blank;
        else if(c=='\t')t|=C1Space|C1Blank|C1Cntrl;
        else if(c>=0x0a&&c<=0x0d)t|=C1Space|C1Cntrl;
        else if(c<0x20||c==0x7f)t|=C1Cntrl;
        else t|=C1Punct;
        if((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))t|=C1Xdigit;
        return t;
    }
    if(c<0xa0)return t|C1Cntrl;
    if(c==0xa0)return t|C1Space|C1Blank;
    if(c==0xd7||c==0xf7||c<0xc0)return t|C1Punct;
    if(upper(c)!=c||c==0xdf)return t|C1Lower|C1Alpha;
    if(lower(c)!=c)return t|C1Upper|C1Alpha;
    return t|C1Alpha;
}
size_t count(const uint16_t* text,int32_t given){return given<0?length(text)+1:size_t(given);}
int32_t output(const std::u16string& value,uint16_t* target,int32_t targetCount) {
    if(!targetCount)return int32_t(value.size());
    if(value.size()>size_t(targetCount)){setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(target,value.data(),value.size()*2);return int32_t(value.size());
}
int32_t mapString(uint32_t flags,const uint16_t* source,int32_t sourceCount,uint16_t* target,int32_t targetCount) {
    constexpr uint32_t Lowercase=0x100,Uppercase=0x200,SortKey=0x400,LinguisticCasing=0x1000000;
    if(!source||targetCount<0||(targetCount&&!target)){setLastError(ErrorInvalidParameter);return 0;}
    if(flags&SortKey){log("LCMapString com LCMAP_SORTKEY ainda sem implementacao");setLastError(ErrorInvalidFlags);return 0;}
    if(flags&~(Lowercase|Uppercase|LinguisticCasing)){log("LCMapString com flags 0x"+std::to_string(flags)+" ainda sem implementacao");setLastError(ErrorInvalidFlags);return 0;}
    std::u16string value(reinterpret_cast<const char16_t*>(source),count(source,sourceCount));
    for(auto& c:value)c=char16_t((flags&Uppercase)?upper(c):(flags&Lowercase)?lower(c):c);
    return output(value,target,targetCount);
}

// LCTYPE values of English (United States). Numbers also answer LOCALE_RETURN_NUMBER.
struct Item {uint32_t type;const char16_t* value;};
const Item enUs[]={
    {0x0001,u"0409"},{0x0002,u"English (United States)"},{0x0003,u"ENU"},{0x0007,u"USA"},{0x000b,u"437"},{0x000e,u"."},
    {0x000f,u","},{0x0010,u"3;0"},{0x0014,u"$"},{0x001d,u"/"},{0x001e,u":"},{0x001f,u"M/d/yyyy"},{0x0020,u"dddd, MMMM d, yyyy"},
    {0x1003,u"h:mm:ss tt"},{0x0028,u"AM"},{0x0029,u"PM"},
    {0x002a,u"Monday"},{0x002b,u"Tuesday"},{0x002c,u"Wednesday"},{0x002d,u"Thursday"},{0x002e,u"Friday"},{0x002f,u"Saturday"},{0x0030,u"Sunday"},
    {0x0031,u"Mon"},{0x0032,u"Tue"},{0x0033,u"Wed"},{0x0034,u"Thu"},{0x0035,u"Fri"},{0x0036,u"Sat"},{0x0037,u"Sun"},
    {0x0038,u"January"},{0x0039,u"February"},{0x003a,u"March"},{0x003b,u"April"},{0x003c,u"May"},{0x003d,u"June"},
    {0x003e,u"July"},{0x003f,u"August"},{0x0040,u"September"},{0x0041,u"October"},{0x0042,u"November"},{0x0043,u"December"},
    {0x0044,u"Jan"},{0x0045,u"Feb"},{0x0046,u"Mar"},{0x0047,u"Apr"},{0x0048,u"May"},{0x0049,u"Jun"},
    {0x004a,u"Jul"},{0x004b,u"Aug"},{0x004c,u"Sep"},{0x004d,u"Oct"},{0x004e,u"Nov"},{0x004f,u"Dec"},
    {0x0059,u"en"},{0x005a,u"US"},{0x005c,u"en-US"},{0x006d,u"en"},{0x1001,u"English"},{0x1002,u"United States"},
    {0x1004,u"1252"},{0x1009,u"6"},{0x1011,u"10000"}};
int32_t localeInfo(uint32_t type,uint16_t* data,int32_t dataCount) {
    constexpr uint32_t ReturnNumber=0x20000000;
    uint32_t base=type&0x0fffffff;
    for(auto const& item:enUs) {
        if(item.type!=base)continue;
        std::u16string value(item.value);
        if(type&ReturnNumber) {
            uint32_t number=0;bool hex=base==0x0001;
            for(auto c:value)number=number*(hex?16:10)+uint32_t(c<='9'?c-'0':(c|0x20)-'a'+10);
            if(!dataCount)return 2;
            if(dataCount<2){setLastError(ErrorInsufficientBuffer);return 0;}
            std::memcpy(data,&number,4);return 2;
        }
        value+=u'\0';
        return output(value,data,dataCount);
    }
    log("GetLocaleInfo: tipo 0x"+std::to_string(type)+" ainda sem implementacao");
    setLastError(ErrorInvalidFlags);return 0;
}
}

extern "C" int32_t win_GetStringTypeW(uint32_t infoType,const uint16_t* source,int32_t sourceCount,uint16_t* types) {
    if(!source||!types||sourceCount==0){setLastError(ErrorInvalidParameter);return 0;}
    size_t n=sourceCount<0?length(source)+1:size_t(sourceCount);
    for(size_t k=0;k<n;++k) {
        auto t=ctype1(source[k]);
        types[k]=infoType==1?t:infoType==4?uint16_t((t&C1Alpha)?C3Alpha:0):0;
    }
    return 1;
}
extern "C" int32_t win_LCMapStringW(uint32_t,uint32_t flags,const uint16_t* source,int32_t sourceCount,uint16_t* target,int32_t targetCount) {
    return mapString(flags,source,sourceCount,target,targetCount);
}
extern "C" int32_t win_LCMapStringEx(const uint16_t*,uint32_t flags,const uint16_t* source,int32_t sourceCount,uint16_t* target,int32_t targetCount,
                                     void*,void*,uint64_t) {
    return mapString(flags,source,sourceCount,target,targetCount);
}
extern "C" int32_t win_CompareStringOrdinal(const uint16_t* first,int32_t firstCount,const uint16_t* second,int32_t secondCount,int32_t ignoreCase) {
    if(!first||!second){setLastError(ErrorInvalidParameter);return 0;}
    size_t a=firstCount<0?length(first):size_t(firstCount),b=secondCount<0?length(second):size_t(secondCount);
    for(size_t n=0;n<a&&n<b;++n) {
        uint16_t x=ignoreCase?upper(first[n]):first[n],y=ignoreCase?upper(second[n]):second[n];
        if(x!=y)return x<y?1:3;
    }
    return a==b?2:a<b?1:3;
}
// The *Ex forms msvcr110 asks for (Windows Vista and later). The host has one
// locale, en-US, and compares strings by code unit (NORM_IGNORECASE folds case).
extern "C" int32_t win_CompareStringEx(const uint16_t*,uint32_t flags,const uint16_t* first,int32_t firstCount,
                                       const uint16_t* second,int32_t secondCount,void*,void*,uint64_t) {
    return win_CompareStringOrdinal(first,firstCount,second,secondCount,flags&1);
}
extern "C" int32_t win_IsValidLocaleName(const uint16_t* name){return name!=nullptr;}
// LOCALE_ENUMPROCEX: BOOL CALLBACK(LPWSTR name, DWORD flags, LPARAM).
extern "C" int32_t win_EnumSystemLocalesEx(void* callback,uint32_t,uint64_t parameter,void*) {
    if(!callback){setLastError(ErrorInvalidParameter);return 0;}
    static uint16_t name[]={'e','n','-','U','S',0};
    guestCall8(callback,reinterpret_cast<uint64_t>(name),2,parameter,0,0,0,0,0);   // LOCALE_SPECIFICDATA
    return 1;
}
// No date or time formats: the C runtime falls back to its own.
extern "C" int32_t win_GetDateFormatEx(const uint16_t*,uint32_t,const void*,const uint16_t*,uint16_t*,int32_t,const uint16_t*){setLastError(ErrorNotSupported);return 0;}
extern "C" int32_t win_GetTimeFormatEx(const uint16_t*,uint32_t,const void*,const uint16_t*,uint16_t*,int32_t){setLastError(ErrorNotSupported);return 0;}
extern "C" uint32_t win_GetUserDefaultLCID(void){return 0x409;}
namespace {const std::u16string userLocale(u"en-US",6);}   // with its terminator
extern "C" int32_t win_GetUserDefaultLocaleName(uint16_t* name,int32_t nameCount){return output(userLocale,name,nameCount);}
extern "C" int32_t win_IsValidLocale(uint32_t locale,uint32_t){return locale==0x409||locale==0x400||locale==0x800||locale==0x7f;}
extern "C" int32_t win_EnumSystemLocalesW(void* callback,uint32_t) {
    if(!callback){setLastError(ErrorInvalidParameter);return 0;}
    uint16_t name[]={'0','0','0','0','0','4','0','9',0};
    guestCallLocaleEnum(callback,name);return 1;
}
extern "C" int32_t win_ResolveLocaleName(const uint16_t*,uint16_t* resolved,int32_t resolvedCount) {
    if(!resolved)return int32_t(userLocale.size());
    return output(userLocale,resolved,resolvedCount);
}
extern "C" int32_t win_GetLocaleInfoW(uint32_t,uint32_t type,uint16_t* data,int32_t dataCount){return localeInfo(type,data,dataCount);}
extern "C" int32_t win_GetLocaleInfoEx(const uint16_t*,uint32_t type,uint16_t* data,int32_t dataCount){return localeInfo(type,data,dataCount);}
// The host's one locale: en-US for any name.
extern "C" uint32_t win_LocaleNameToLCID(const uint16_t* name,uint32_t){return name?0x409:0x400;}
// The host's one locale's name, for any LCID.
extern "C" int32_t win_LCIDToLocaleName(uint32_t,uint16_t* name,int32_t count,uint32_t) {
    static const uint16_t text[]={'e','n','-','U','S',0};
    if(!name||count==0)return 6;
    if(count<6){setLastError(ErrorInsufficientBuffer);return 0;}
    for(int n=0;n<6;++n)name[n]=text[n];
    return 6;
}
