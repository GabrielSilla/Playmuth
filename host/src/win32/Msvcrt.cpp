// msvcrt.dll for the DLLs the host loads (vkd3d-proton is built by mingw-w64
// against it). Where the Windows C library differs from the host's, the
// Windows form is kept: long is 32 bits, wchar_t 16, a FILE* is an opaque
// handle (the host's FILE, or one of __iob_func's three), va_list points to
// 8-byte slots. stdout and stderr become lines of the host's log.
#include "Win32.h"
#include "WriteWatch.h"
#include "WatchedIo.h"
#include "RepeatedAudioLog.h"
#include "../guest_abi.h"
#include "../HostHeap.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
// ---- FILE ----------------------------------------------------------------
// __iob_func's three FILE structures (msvcrt's 48-byte _iobuf): only their
// addresses matter. Every other FILE* is the host's own.
uint8_t standardFiles[3][48];
struct Stream {std::mutex lock;std::string pending;const char* name;};
Stream outStream{{},{},"stdout"},errStream{{},{},"stderr"};
RepeatedAudioLog repeatedAudio;
Stream* logStream(void* file) {
    if(file==standardFiles[1])return &outStream;
    if(file==standardFiles[2])return &errStream;
    return nullptr;
}
// Text written to stdout or stderr: whole lines go to the log.
void toLog(Stream& s,const char* data,size_t size) {
    std::lock_guard<std::mutex> lock(s.lock);
    s.pending.append(data,size);
    size_t end;
    while((end=s.pending.find('\n'))!=std::string::npos) {
        auto line=s.pending.substr(0,end);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(&s!=&errStream||repeatedAudio.accept(line))log(std::string("[")+s.name+"] "+line);
        s.pending.erase(0,end+1);
    }
}
FILE* hostFile(void* file) {
    if(file==standardFiles[0])return stdin;
    return static_cast<FILE*>(file);
}
// A Windows path the DLL names, served by the host's mounts; empty if outside them.
std::string pathFor(const std::u16string& windowsPath) {
    return hostPath(fullPath(windowsPath));
}
std::string mode(const char* windowsMode) {
    std::string m;for(auto c=windowsMode;*c;++c)if(*c!='t'&&*c!='N'&&*c!='D'&&*c!='T'&&*c!='S'&&*c!='R')m+=*c;
    return m;
}

// ---- printf with a Windows va_list -----------------------------------------
// Each conversion is handed to the host's snprintf with its own argument,
// read from the next 8-byte slot with the Windows sizes.
std::string format(const char* text,const uint64_t* args) {
    std::string out;
    auto next=[&]{return *args++;};
    for(auto p=text;*p;) {
        if(*p!='%'){out+=*p++;continue;}
        auto start=p++;
        if(*p=='%'){out+='%';++p;continue;}
        std::string spec="%";
        while(*p&&std::strchr("-+ #0'",*p))spec+=*p++;
        if(*p=='*'){spec+=std::to_string(int32_t(next()));++p;}else while(std::isdigit(uint8_t(*p)))spec+=*p++;
        if(*p=='.') {
            spec+=*p++;
            if(*p=='*'){spec+=std::to_string(int32_t(next()));++p;}else while(std::isdigit(uint8_t(*p)))spec+=*p++;
        }
        // Length: 64-bit for ll, I64, j, z, t, I (size_t) and L on integers;
        // long (l) is 32 bits on Windows; l/w on c and s means wide.
        int bits=32;bool wide=false;
        for(;;) {
            if(p[0]=='I'&&p[1]=='6'&&p[2]=='4'){bits=64;p+=3;}
            else if(p[0]=='I'&&p[1]=='3'&&p[2]=='2'){bits=32;p+=3;}
            else if(*p=='I'||*p=='j'||*p=='z'||*p=='t'){bits=64;++p;}
            else if(p[0]=='l'&&p[1]=='l'){bits=64;p+=2;}
            else if(*p=='l'||*p=='w'){wide=true;++p;}
            else if(p[0]=='h'&&p[1]=='h'){bits=8;p+=2;}
            else if(*p=='h'){bits=16;++p;}
            else if(*p=='L'){bits=64;++p;}
            else break;
        }
        char conversion=*p;if(!conversion){out.append(start);break;}
        ++p;
        char buffer[512];
        switch(conversion) {
            case 'd':case 'i': {
                uint64_t raw=next();
                long long value=bits==64?(long long)raw:bits==16?(long long)int16_t(raw):bits==8?(long long)int8_t(raw):(long long)int32_t(raw);
                std::snprintf(buffer,sizeof buffer,(spec+"lld").c_str(),value);out+=buffer;break;
            }
            case 'u':case 'x':case 'X':case 'o': {
                uint64_t raw=next();
                unsigned long long value=bits==64?raw:bits==16?uint16_t(raw):bits==8?uint8_t(raw):uint32_t(raw);
                std::snprintf(buffer,sizeof buffer,(spec+"ll"+conversion).c_str(),value);out+=buffer;break;
            }
            case 'p': {
                // msvcrt prints pointers as 16 uppercase hex digits.
                std::snprintf(buffer,sizeof buffer,"%016llX",(unsigned long long)next());out+=buffer;break;
            }
            case 'c':case 'C': {
                auto value=uint32_t(next());
                if(wide||conversion=='C')value&=0xffff;
                out+=char(value<0x80?value:'?');break;
            }
            case 's':case 'S': {
                auto pointer=reinterpret_cast<const void*>(next());
                std::string value;
                if(!pointer)value="(null)";
                else if(wide||conversion=='S'){auto w=static_cast<const uint16_t*>(pointer);value=utf8(w,length(w));}
                else value=static_cast<const char*>(pointer);
                std::snprintf(buffer,sizeof buffer,(spec+"s").c_str(),value.c_str());out+=buffer;break;
            }
            case 'f':case 'F':case 'e':case 'E':case 'g':case 'G':case 'a':case 'A': {
                uint64_t raw=next();double value;std::memcpy(&value,&raw,8);
                std::snprintf(buffer,sizeof buffer,(spec+conversion).c_str(),value);out+=buffer;break;
            }
            case 'n':next();break;   // never written through
            default:out.append(start,p);break;
        }
    }
    return out;
}

thread_local int32_t lastErrno=0;
std::recursive_mutex crtLocks[64];
std::mutex environmentLock;
}

// The environment the DLLs see through getenv and GetEnvironmentVariableA
// (Kernel32.cpp): empty until they set something.
namespace azimuth::win32 {
std::vector<std::pair<std::string,std::string>>& dllEnvironment(std::unique_lock<std::mutex>& lock) {
    static std::vector<std::pair<std::string,std::string>> values;
    lock=std::unique_lock<std::mutex>(environmentLock);
    return values;
}
}

// ---- Memory and strings ------------------------------------------------------
// The host's allocator (HostHeap.h): big blocks and blocks malloc refuses
// come from mmap.
std::string azimuth::win32::repeatedAudioReport(){
    auto counts=repeatedAudio.take();
    return "Avisos de audio: "+std::to_string(counts.occurrences)+" repetidos XMA, "+std::to_string(counts.suppressed)+" compactados (~10 s)";
}
extern "C" void* win_malloc(uint64_t size){void* b=azimuth::heapAlloc(size,16,false);if(!b)lastErrno=ENOMEM;return b;}
extern "C" void* win_calloc(uint64_t count,uint64_t size) {
    if(count&&size>SIZE_MAX/count){lastErrno=ENOMEM;return nullptr;}
    void* b=azimuth::heapAlloc(count*size,16,true);if(!b)lastErrno=ENOMEM;return b;
}
extern "C" void win_free(void* block){azimuth::heapFree(block);}
extern "C" void* win_realloc(void* block,uint64_t size) {
    if(!block)return win_malloc(size);
    if(!size){azimuth::heapFree(block);return nullptr;}
    size_t known=azimuth::heapUsable(block);
    if(size<=known)return block;
    void* grown=azimuth::heapAlloc(size,16,false);
    if(!grown){lastErrno=ENOMEM;return nullptr;}
    std::memcpy(grown,block,known<size?known:size);
    azimuth::heapFree(block);
    return grown;
}
extern "C" void* win__aligned_malloc(uint64_t size,uint64_t alignment) {
    if(alignment<sizeof(void*))alignment=sizeof(void*);
    void* b=azimuth::heapAlloc(size,alignment,false);
    if(!b)lastErrno=(alignment>4096||(alignment&(alignment-1)))?EINVAL:ENOMEM;
    return b;
}
extern "C" void win__aligned_free(void* block){azimuth::heapFree(block);}
extern "C" void* win_memchr(const void* block,int32_t value,uint64_t size){return const_cast<void*>(std::memchr(block,value,size));}
extern "C" int32_t win_memcmp(const void* a,const void* b,uint64_t size){return std::memcmp(a,b,size);}
extern "C" void* win_memcpy(void* to,const void* from,uint64_t size){return trackedMemcpy(to,from,size);}
extern "C" void* win_memmove(void* to,const void* from,uint64_t size){return trackedMemmove(to,from,size);}
extern "C" void* win_memset(void* to,int32_t value,uint64_t size){return trackedMemset(to,value,size);}
extern "C" char* win_strchr(const char* text,int32_t c){return const_cast<char*>(std::strchr(text,c));}
extern "C" char* win_strrchr(const char* text,int32_t c){return const_cast<char*>(std::strrchr(text,c));}
extern "C" char* win_strstr(const char* text,const char* part){return const_cast<char*>(std::strstr(text,part));}
extern "C" int32_t win_strcmp(const char* a,const char* b){return std::strcmp(a,b);}
extern "C" int32_t win_strncmp(const char* a,const char* b,uint64_t size){return std::strncmp(a,b,size);}
extern "C" int32_t win_strcoll(const char* a,const char* b){return std::strcmp(a,b);}   // "C" locale
extern "C" char* win_strcpy(char* to,const char* from){return std::strcpy(to,from);}
extern "C" char* win_strncpy(char* to,const char* from,uint64_t size){return std::strncpy(to,from,size);}
extern "C" char* win_strncat(char* to,const char* from,uint64_t size){return std::strncat(to,from,size);}
extern "C" uint64_t win_strlen(const char* text){return std::strlen(text);}
extern "C" uint64_t win_strxfrm(char* to,const char* from,uint64_t size) {
    auto needed=std::strlen(from);
    if(to&&size>needed)std::memcpy(to,from,needed+1);
    return needed;
}
extern "C" char* win_strerror(int32_t error){return std::strerror(error);}
// struct tm has the same nine ints on both sides.
extern "C" uint64_t win_strftime(char* to,uint64_t size,const char* format,const void* time){return std::strftime(to,size,format,static_cast<const std::tm*>(time));}
extern "C" int32_t win_strtol(const char* text,char** end,int32_t base) {
    errno=0;long long value=std::strtoll(text,end,base);
    if(value>INT32_MAX){lastErrno=ERANGE;return INT32_MAX;}
    if(value<INT32_MIN){lastErrno=ERANGE;return INT32_MIN;}
    if(errno)lastErrno=errno;
    return int32_t(value);
}
extern "C" uint32_t win_strtoul(const char* text,char** end,int32_t base) {
    errno=0;unsigned long long value=std::strtoull(text,end,base);
    if(value>UINT32_MAX&&value<~0ull-UINT32_MAX){lastErrno=ERANGE;return UINT32_MAX;}
    if(errno)lastErrno=errno;
    return uint32_t(value);
}
extern "C" int64_t win__strtoi64(const char* text,char** end,int32_t base){errno=0;auto v=std::strtoll(text,end,base);if(errno)lastErrno=errno;return v;}
extern "C" uint64_t win__strtoui64(const char* text,char** end,int32_t base){errno=0;auto v=std::strtoull(text,end,base);if(errno)lastErrno=errno;return v;}
namespace {bool byte(int32_t c){return c>=0&&c<256;}}
extern "C" int32_t win_tolower(int32_t c){return byte(c)?std::tolower(c):c;}
extern "C" int32_t win_isalnum(int32_t c){return byte(c)&&std::isalnum(c);}
extern "C" int32_t win_isalpha(int32_t c){return byte(c)&&std::isalpha(c);}
extern "C" int32_t win_isprint(int32_t c){return byte(c)&&std::isprint(c);}
extern "C" int32_t win_isspace(int32_t c){return byte(c)&&std::isspace(c);}
extern "C" int32_t win_isxdigit(int32_t c){return byte(c)&&std::isxdigit(c);}
extern "C" uint64_t win_wcslen(const uint16_t* text){return length(text);}
extern "C" int32_t win_wcscoll(const uint16_t* a,const uint16_t* b) {
    while(*a&&*a==*b){++a;++b;}
    return *a<*b?-1:*a>*b?1:0;
}
extern "C" uint64_t win_wcsxfrm(uint16_t* to,const uint16_t* from,uint64_t size) {
    auto needed=length(from);
    if(to&&size>needed)std::memcpy(to,from,(needed+1)*2);
    return needed;
}
extern "C" uint64_t win_wcsftime(uint16_t* to,uint64_t size,const uint16_t* format,const void* time) {
    char narrow[512];std::string f=utf8(format,length(format));
    auto got=std::strftime(narrow,sizeof narrow,f.c_str(),static_cast<const std::tm*>(time));
    if(!got||got>=size)return 0;
    for(size_t n=0;n<=got;++n)to[n]=uint8_t(narrow[n]);
    return got;
}
extern "C" uint16_t win_towlower(uint16_t c){return c>='A'&&c<='Z'?uint16_t(c+32):c;}
extern "C" uint16_t win_towupper(uint16_t c){return c>='a'&&c<='z'?uint16_t(c-32):c;}
// _UPPER 1, _LOWER 2, _DIGIT 4, _SPACE 8, _PUNCT 0x10, _CONTROL 0x20, _BLANK 0x40, _HEX 0x80, _ALPHA 0x100.
extern "C" int32_t win_iswctype(uint16_t c,uint16_t mask) {
    if(c>=0x80)return 0;
    int32_t bits=0;
    if(std::isupper(c))bits|=0x1|0x100;
    if(std::islower(c))bits|=0x2|0x100;
    if(std::isdigit(c))bits|=0x4;
    if(std::isspace(c))bits|=0x8;
    if(std::ispunct(c))bits|=0x10;
    if(std::iscntrl(c))bits|=0x20;
    if(c==' '||c=='\t')bits|=0x40;
    if(std::isxdigit(c))bits|=0x80;
    return bits&mask;
}
// The comparator is Windows code: the order is computed on indices, then applied.
extern "C" void win_qsort(void* base,uint64_t count,uint64_t size,const void* compare) {
    if(count<2||!size)return;
    auto bytes=static_cast<uint8_t*>(base);
    std::vector<uint64_t> order(count);for(uint64_t n=0;n<count;++n)order[n]=n;
    std::stable_sort(order.begin(),order.end(),[&](uint64_t a,uint64_t b){return guestCallCompare(compare,bytes+a*size,bytes+b*size)<0;});
    std::vector<uint8_t> sorted(count*size);
    for(uint64_t n=0;n<count;++n)std::memcpy(sorted.data()+n*size,bytes+order[n]*size,size);
    std::memcpy(bytes,sorted.data(),sorted.size());
}
extern "C" char* win_getenv(const char* name) {
    std::unique_lock<std::mutex> lock;
    for(auto& v:dllEnvironment(lock))if(v.first==name)return &v.second[0];
    return nullptr;
}
extern "C" char* win_setlocale(int32_t,const char*){static char c[]="C";return c;}
// struct lconv: ten char* fields (decimal_point first), eight chars in one
// 8-byte slot, then the eight wide strings of newer CRTs; every string is
// empty but the decimal point.
extern "C" void* win_localeconv(void) {
    static char point[]=".",empty[]="";
    static void* conv[24]={point,empty,empty,empty,empty,empty,empty,empty,empty,empty};
    static uint16_t widePoint[]={'.',0},wideEmpty[]={0};
    static bool filled=[]{
        std::memset(&conv[10],0x7f,8);   // the eight char fields: CHAR_MAX ("not available")
        conv[11]=widePoint;for(int n=12;n<19;++n)conv[n]=wideEmpty;
        return true;
    }();
    (void)filled;
    return conv;
}
extern "C" uint32_t win____lc_codepage_func(void){return 0;}   // "C" locale
extern "C" int32_t win____mb_cur_max_func(void){return 1;}
extern "C" int32_t* win__errno(void){return &lastErrno;}

// ---- Startup and errors -------------------------------------------------------
extern "C" void win__initterm(void* const* first,void* const* last) {
    for(;first<last;++first)if(*first)guestCallVoid(*first);
}
extern "C" void win__amsg_exit(int32_t code){stopGuest("msvcrt _amsg_exit("+std::to_string(code)+") numa DLL carregada");}
extern "C" void win_abort(void){stopGuest("abort() numa DLL carregada");}
extern "C" void win__assert(const char* message,const char* file,uint32_t line) {
    stopGuest(std::string("assert numa DLL carregada: ")+(message?message:"?")+" ("+(file?file:"?")+":"+std::to_string(line)+")");
}
extern "C" void win__lock(int32_t lock){crtLocks[uint32_t(lock)%64].lock();}
extern "C" void win__unlock(int32_t lock){crtLocks[uint32_t(lock)%64].unlock();}

// ---- Streams ----------------------------------------------------------------
extern "C" void* win___iob_func(void){return standardFiles;}
extern "C" void* win_fopen(const char* path,const char* windowsMode) {
    auto host=pathFor(utf16(path));
    if(host.empty()){lastErrno=ENOENT;log(std::string("msvcrt fopen fora das unidades: ")+path);return nullptr;}
    FILE* file=std::fopen(host.c_str(),mode(windowsMode).c_str());
    if(!file)lastErrno=errno;
    return file;
}
extern "C" void* win__wfopen(const uint16_t* path,const uint16_t* windowsMode) {
    auto narrowMode=utf8(windowsMode,length(windowsMode));
    auto host=pathFor(fromGuest(path));
    if(host.empty()){lastErrno=ENOENT;log("msvcrt _wfopen fora das unidades: "+utf8(path,length(path)));return nullptr;}
    FILE* file=std::fopen(host.c_str(),mode(narrowMode.c_str()).c_str());
    if(!file)lastErrno=errno;
    return file;
}
extern "C" void* win__fdopen(int32_t fd,const char* windowsMode){return fdopen(fd,mode(windowsMode).c_str());}
extern "C" int32_t win_fclose(void* file){return logStream(file)||file==standardFiles[0]?0:std::fclose(hostFile(file));}
extern "C" int32_t win_fflush(void* file){return !file||logStream(file)?0:std::fflush(hostFile(file));}
extern "C" uint64_t win_fwrite(const void* data,uint64_t size,uint64_t count,void* file) {
    if(auto s=logStream(file)){toLog(*s,static_cast<const char*>(data),size*count);return count;}
    return std::fwrite(data,size,count,hostFile(file));
}
extern "C" int32_t win_fputc(int32_t c,void* file){char b=char(c);return win_fwrite(&b,1,1,file)==1?uint8_t(b):-1;}
extern "C" int32_t win_putc(int32_t c,void* file){return win_fputc(c,file);}
extern "C" int32_t win_fputs(const char* text,void* file){auto n=std::strlen(text);return win_fwrite(text,1,n,file)==n?0:-1;}
extern "C" uint64_t win_fread(void* data,uint64_t size,uint64_t count,void* file){
    if(logStream(file)||!size||!count)return 0;
    if(count>~uint64_t(0)/size){lastErrno=EINVAL;return 0;}
    auto bytes=size*count;
    if(!watchedBuffer(data,bytes))return std::fread(data,size,count,hostFile(file));
    WatchedIoBuffer bounce;uint64_t total=0;
    while(total<bytes){
        auto step=size_t(std::min<uint64_t>(bytes-total,bounce.capacity()));
        auto began=fastMonotonicNs();
        auto got=std::fread(bounce.data(),1,step,hostFile(file));int saved=errno;
        auto readDone=fastMonotonicNs();
        if(got)copyToWatched(static_cast<char*>(data)+total,bounce.data(),got);
        auto copied=fastMonotonicNs();recordWatchedIo(got,uint64_t(readDone-began),uint64_t(copied-readDone),true);
        errno=saved;
        total+=got;if(got<step)break;
    }
    return total/size;
}
extern "C" int32_t win_getc(void* file){return logStream(file)?-1:std::fgetc(hostFile(file));}
extern "C" int32_t win_ungetc(int32_t c,void* file){return logStream(file)?-1:std::ungetc(c,hostFile(file));}
extern "C" char* win_fgets(char* text,int32_t size,void* file){return logStream(file)?nullptr:std::fgets(text,size,hostFile(file));}
extern "C" int32_t win_fseek(void* file,int32_t offset,int32_t origin){return logStream(file)?-1:std::fseek(hostFile(file),offset,origin);}
extern "C" int32_t win_ftell(void* file) {
    if(logStream(file))return -1;
    long position=std::ftell(hostFile(file));
    return position>INT32_MAX?-1:int32_t(position);
}
extern "C" void win_rewind(void* file){if(!logStream(file))std::rewind(hostFile(file));}
extern "C" int32_t win_fgetpos(void* file,int64_t* position) {
    if(logStream(file))return -1;
    long at=std::ftell(hostFile(file));if(at<0)return -1;
    *position=at;return 0;
}
extern "C" int32_t win_fsetpos(void* file,const int64_t* position){return logStream(file)?-1:std::fseek(hostFile(file),long(*position),SEEK_SET);}
// _IOFBF 0, _IOLBF 0x40, _IONBF 4.
extern "C" int32_t win_setvbuf(void* file,char* buffer,int32_t windowsMode,uint64_t size) {
    if(logStream(file))return 0;
    int hostMode=windowsMode==4?_IONBF:windowsMode==0x40?_IOLBF:_IOFBF;
    return std::setvbuf(hostFile(file),buffer,hostMode,size);
}
extern "C" int32_t win__fileno(void* file) {
    if(file==standardFiles[0])return 0;
    if(file==standardFiles[1])return 1;
    if(file==standardFiles[2])return 2;
    return fileno(hostFile(file));
}
extern "C" uint16_t win_fputwc(uint16_t c,void* file) {
    auto text=utf8(&c,1);
    return win_fwrite(text.data(),1,text.size(),file)==text.size()?c:0xffff;
}
extern "C" uint16_t win_putwc(uint16_t c,void* file){return win_fputwc(c,file);}
extern "C" uint16_t win_getwc(void* file){int c=win_getc(file);return c<0?0xffff:uint16_t(c);}
extern "C" uint16_t win_ungetwc(uint16_t c,void* file){return win_ungetc(c<0x80?c:'?',file)<0?0xffff:c;}
extern "C" int32_t win_vfprintf(void* file,const char* text,const void* arguments) {
    auto out=format(text,static_cast<const uint64_t*>(arguments));
    return win_fwrite(out.data(),1,out.size(),file)==out.size()?int32_t(out.size()):-1;
}

// ---- Descriptors ------------------------------------------------------------
// _O_WRONLY 1, _O_RDWR 2, _O_APPEND 8, _O_CREAT 0x100, _O_TRUNC 0x200, _O_EXCL 0x400.
extern "C" int32_t win__open(const char* path,int32_t flags,int32_t mode) {
    auto host=pathFor(utf16(path));
    if(host.empty()){lastErrno=ENOENT;log(std::string("msvcrt _open fora das unidades: ")+path);return -1;}
    int hostFlags=(flags&2)?O_RDWR:(flags&1)?O_WRONLY:O_RDONLY;
    if(flags&8)hostFlags|=O_APPEND;
    if(flags&0x100)hostFlags|=O_CREAT;
    if(flags&0x200)hostFlags|=O_TRUNC;
    if(flags&0x400)hostFlags|=O_EXCL;
    int fd=::open(host.c_str(),hostFlags,(mode&0x80)?0666:0444);
    if(fd<0)lastErrno=errno;
    return fd;
}
extern "C" int32_t win__close(int32_t fd){return fd<=2?0:(::close(fd));}
extern "C" int32_t win__read(int32_t fd,void* data,uint32_t size){auto got=watchedRead(fd,data,size);if(got<0)lastErrno=errno;return int32_t(got);}
extern "C" int32_t win__write(int32_t fd,const void* data,uint32_t size) {
    if(fd==1||fd==2){toLog(fd==1?outStream:errStream,static_cast<const char*>(data),size);return int32_t(size);}
    auto put=::write(fd,data,size);if(put<0)lastErrno=errno;return int32_t(put);
}
extern "C" int64_t win__lseeki64(int32_t fd,int64_t offset,int32_t origin){auto at=::lseek(fd,offset,origin);if(at<0)lastErrno=errno;return at;}
extern "C" int64_t win__filelengthi64(int32_t fd){struct stat st;if(::fstat(fd,&st)){lastErrno=errno;return -1;}return st.st_size;}
// struct _stat64: dev u32, ino u16, mode u16, nlink i16, uid i16, gid i16,
// rdev u32 at 16, size i64 at 24, atime/mtime/ctime i64 at 32/40/48.
extern "C" int32_t win__fstat64(int32_t fd,void* status) {
    struct stat st;
    if(::fstat(fd,&st)){lastErrno=errno;return -1;}
    auto p=static_cast<uint8_t*>(status);std::memset(p,0,56);
    uint16_t m=S_ISDIR(st.st_mode)?0x4000:S_ISREG(st.st_mode)?0x8000:0x2000;
    m|=0x100;if(st.st_mode&S_IWUSR)m|=0x80;
    int64_t size=st.st_size,at=st.st_atime,mt=st.st_mtime,ct=st.st_ctime;int16_t links=1;
    std::memcpy(p+6,&m,2);std::memcpy(p+8,&links,2);
    std::memcpy(p+24,&size,8);std::memcpy(p+32,&at,8);std::memcpy(p+40,&mt,8);std::memcpy(p+48,&ct,8);
    return 0;
}

// ---- For xg_x.dll, the game's graphics library (title 0.63) ------------------
// vsprintf_s: the whole text, or an empty buffer and -1 when it does not fit
// (the CRT's invalid-parameter handler would end the process instead).
extern "C" int32_t win_vsprintf_s(char* buffer,uint64_t size,const char* text,const void* arguments) {
    if(!buffer||!size||!text)return -1;
    auto out=format(text,static_cast<const uint64_t*>(arguments));
    if(out.size()+1>size){buffer[0]=0;return -1;}
    std::memcpy(buffer,out.c_str(),out.size()+1);return int32_t(out.size());
}
extern "C" double win_sqrt(double value){return std::sqrt(value);}
// Structured exception filters run only while an exception is dispatched,
// which the host does not do yet: they let it pass on.
extern "C" int32_t win___C_specific_handler(void*,void*,void*,void*){return 1;}   // ExceptionContinueSearch
extern "C" int32_t win__XcptFilter(uint32_t,void*){return 0;}                     // EXCEPTION_CONTINUE_SEARCH

// ---- The Universal CRT's own entry points (title 1.03) ----------------------
// FFmpeg's DLLs (avutil-60, avcodec-62, which the engine loads for SHAPE's
// audio) are built against the UCRT: their api-ms-win-crt-* imports are served
// here with msvcrt's (Kernel32.cpp and Modules.cpp map the names).
namespace {
// _onexit_table_t: first, last, end (arrays of _PVFV).
struct OnexitTable {void** first;void** last;void** end;};
std::mutex onexitLock;
OnexitTable processExit{};
int32_t registerOnexit(OnexitTable* table,void* function) {
    std::lock_guard<std::mutex> guard(onexitLock);
    size_t used=size_t(table->last-table->first),size=size_t(table->end-table->first);
    if(used==size) {
        size_t bigger=size?size*2:8;
        auto fresh=static_cast<void**>(azimuth::heapAlloc(bigger*sizeof(void*),16,true));
        if(!fresh)return -1;
        if(used)std::memcpy(fresh,table->first,used*sizeof(void*));
        if(table->first)azimuth::heapFree(table->first);
        table->first=fresh;table->last=fresh+used;table->end=fresh+bigger;
    }
    *table->last++=function;
    return 0;
}
int32_t argc=1;
char programName[]="azimuth.exe";
char* argvNarrow[]={programName,nullptr};
char16_t programNameWide[]=u"azimuth.exe";
char16_t* argvWide[]={programNameWide,nullptr};
char** environNarrow=nullptr;uint16_t** environWide=nullptr;
int32_t windowsDaylight=0,timezoneSeconds=0;
char tzStandard[]="UTC",tzDaylight[]="UTC";
char* tzNames[]={tzStandard,tzDaylight};
// The UCRT's printf options: _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR.
constexpr uint64_t StandardSnprintf=2;
// struct tm on Windows: nine ints.
void toWindowsTm(const std::tm& t,void* out) {
    int32_t v[9]={t.tm_sec,t.tm_min,t.tm_hour,t.tm_mday,t.tm_mon,t.tm_year,t.tm_wday,t.tm_yday,t.tm_isdst};
    std::memcpy(out,v,sizeof v);
}
std::tm fromWindowsTm(const void* in) {
    int32_t v[9];std::memcpy(v,in,sizeof v);
    std::tm t{};t.tm_sec=v[0];t.tm_min=v[1];t.tm_hour=v[2];t.tm_mday=v[3];t.tm_mon=v[4];t.tm_year=v[5];t.tm_wday=v[6];t.tm_yday=v[7];t.tm_isdst=v[8];
    return t;
}
// A wide printf format as a narrow one: %s and %c take wide arguments there.
std::string narrowWideFormat(const uint16_t* text) {
    std::string out=utf8(text,length(text)),fixed;
    for(size_t n=0;n<out.size();++n) {
        fixed+=out[n];
        if(out[n]!='%')continue;
        size_t m=n+1;
        while(m<out.size()&&std::strchr("-+ #0'.*0123456789",out[m]))fixed+=out[m++];
        if(m<out.size()&&(out[m]=='s'||out[m]=='c'))fixed+='l';
        n=m-1;
    }
    return fixed;
}
}
extern "C" void* win___acrt_iob_func(uint32_t index){return index<3?standardFiles[index]:nullptr;}
extern "C" int32_t win___stdio_common_vfprintf(uint64_t,void* file,const char* text,void*,const void* arguments) {
    return win_vfprintf(file,text,arguments);
}
extern "C" int32_t win___stdio_common_vfwprintf(uint64_t,void* file,const uint16_t* text,void*,const void* arguments) {
    if(!text)return -1;
    auto narrow=narrowWideFormat(text);
    return win_vfprintf(file,narrow.c_str(),arguments);
}
extern "C" int32_t win___stdio_common_vsprintf(uint64_t options,char* buffer,uint64_t count,const char* text,void*,const void* arguments) {
    if(!text)return -1;
    auto out=format(text,static_cast<const uint64_t*>(arguments));
    if(!buffer||!count)return int32_t(out.size());
    if(out.size()<count){std::memcpy(buffer,out.c_str(),out.size()+1);return int32_t(out.size());}
    if(options&StandardSnprintf){std::memcpy(buffer,out.data(),count-1);buffer[count-1]=0;return int32_t(out.size());}
    std::memcpy(buffer,out.data(),count);
    return -1;
}
// sscanf with a Windows va_list of pointers: each conversion is read by the
// host's sscanf from where the last one stopped (%n gives how far).
extern "C" int32_t win___stdio_common_vsscanf(uint64_t,const char* input,uint64_t count,const char* text,void*,const void* arguments) {
    if(!input||!text)return -1;
    std::string source(input,count==~0ull?std::strlen(input):std::min<uint64_t>(count,std::strlen(input)));
    auto args=static_cast<const uint64_t*>(arguments);
    size_t at=0;int32_t assigned=0;
    for(auto p=text;*p;) {
        if(std::isspace(uint8_t(*p))){while(at<source.size()&&std::isspace(uint8_t(source[at])))++at;++p;continue;}
        if(*p!='%'||p[1]=='%') {
            char want=*p=='%'?'%':*p;p+=*p=='%'?2:1;
            if(at>=source.size()||source[at]!=want)return assigned;
            ++at;continue;
        }
        std::string spec="%";++p;
        bool skip=false;if(*p=='*'){skip=true;spec+='*';++p;}
        while(std::isdigit(uint8_t(*p)))spec+=*p++;
        int bits=32;bool isDouble=false;
        for(;;) {
            if(p[0]=='l'&&p[1]=='l'){bits=64;p+=2;}
            else if(p[0]=='I'&&p[1]=='6'&&p[2]=='4'){bits=64;p+=3;}
            else if(*p=='l'){bits=32;isDouble=true;++p;}   // long is 32 bits; %lf is a double
            else if(p[0]=='h'&&p[1]=='h'){bits=8;p+=2;}
            else if(*p=='h'){bits=16;++p;}
            else if(*p=='z'||*p=='j'||*p=='t'||*p=='I'){bits=64;++p;}
            else break;
        }
        char conversion=*p++;
        if(!conversion)break;
        std::string piece=source.substr(at);int used=0;
        if(conversion=='n'){if(!skip)*reinterpret_cast<int32_t*>(*args++)=int32_t(at);continue;}
        if(std::strchr("diuxXo",conversion)) {
            long long value=0;unsigned long long uvalue=0;bool isSigned=conversion=='d'||conversion=='i';
            int got=isSigned?std::sscanf(piece.c_str(),(spec+"ll"+conversion+"%n").c_str(),&value,&used)
                            :std::sscanf(piece.c_str(),(spec+"ll"+conversion+"%n").c_str(),&uvalue,&used);
            if(got<1)return assigned?assigned:(at>=source.size()?-1:0);
            if(!isSigned)value=(long long)uvalue;
            if(!skip) {
                void* to=reinterpret_cast<void*>(*args++);
                if(bits==64)std::memcpy(to,&value,8);else if(bits==16){int16_t v=int16_t(value);std::memcpy(to,&v,2);}
                else if(bits==8){int8_t v=int8_t(value);std::memcpy(to,&v,1);}else{int32_t v=int32_t(value);std::memcpy(to,&v,4);}
                ++assigned;
            }
        }else if(std::strchr("feEgGaA",conversion)) {
            double value=0;
            if(std::sscanf(piece.c_str(),(spec+"l"+conversion+"%n").c_str(),&value,&used)<1)return assigned?assigned:(at>=source.size()?-1:0);
            if(!skip){void* to=reinterpret_cast<void*>(*args++);if(isDouble)std::memcpy(to,&value,8);else{float f=float(value);std::memcpy(to,&f,4);}++assigned;}
        }else if(conversion=='s'||conversion=='c'||conversion=='[') {
            std::string set;
            if(conversion=='[') {
                set="[";if(*p=='^')set+=*p++;if(*p==']')set+=*p++;
                while(*p&&*p!=']')set+=*p++;
                if(*p)set+=*p++;
            }
            std::string fmt=spec+(conversion=='['?set:std::string(1,conversion))+"%n";
            std::vector<char> storage(piece.size()+2,0);
            if(std::sscanf(piece.c_str(),fmt.c_str(),storage.data(),&used)<1)return assigned?assigned:(at>=source.size()?-1:0);
            if(!skip) {
                auto to=reinterpret_cast<char*>(*args++);
                size_t n=conversion=='c'?size_t(used):std::strlen(storage.data())+1;
                std::memcpy(to,storage.data(),n);++assigned;
            }
        }else return assigned;
        at+=size_t(used);
    }
    return assigned;
}
extern "C" void* win__get_osfhandle(int32_t){return reinterpret_cast<void*>(intptr_t(-1));}
extern "C" int32_t win__isatty(int32_t){return 0;}
extern "C" int32_t win__sopen(const char* path,int32_t flags,int32_t,int32_t mode){return win__open(path,flags,mode);}
extern "C" int32_t win__wsopen(const uint16_t* path,int32_t flags,int32_t,int32_t mode) {
    if(!path)return -1;
    auto narrow=utf8(path,length(path));
    return win__open(narrow.c_str(),flags,mode);
}
extern "C" int32_t* win___p___argc(void){return &argc;}
extern "C" char*** win___p___argv(void){static char** p=argvNarrow;return &p;}
extern "C" uint16_t*** win___p___wargv(void){static auto p=reinterpret_cast<uint16_t**>(argvWide);return &p;}
extern "C" char*** win___p__environ(void) {
    if(!environNarrow){environNarrow=static_cast<char**>(azimuth::heapAlloc(sizeof(char*),16,true));}
    return &environNarrow;
}
extern "C" uint16_t*** win___p__wenviron(void) {
    if(!environWide){environWide=static_cast<uint16_t**>(azimuth::heapAlloc(sizeof(uint16_t*),16,true));}
    return &environWide;
}
extern "C" int32_t win__configure_narrow_argv(int32_t){return 0;}
extern "C" int32_t win__configure_wide_argv(int32_t){return 0;}
extern "C" int32_t win__initialize_narrow_environment(void){return 0;}
extern "C" int32_t win__initialize_wide_environment(void){return 0;}
extern "C" int32_t win__initialize_onexit_table(void* table) {
    if(!table)return -1;
    std::memset(table,0,sizeof(OnexitTable));return 0;
}
extern "C" int32_t win__register_onexit_function(void* table,void* function) {
    if(!table||!function)return -1;
    return registerOnexit(static_cast<OnexitTable*>(table),function);
}
// The table's functions, last registered first (a DLL detaching).
extern "C" int32_t win__execute_onexit_table(void* table) {
    if(!table)return -1;
    OnexitTable t;
    {std::lock_guard<std::mutex> guard(onexitLock);t=*static_cast<OnexitTable*>(table);std::memset(table,0,sizeof t);}
    for(auto f=t.last;f>t.first;)guestCallVoid(*--f);
    if(t.first)azimuth::heapFree(t.first);
    return 0;
}
// Process exit never comes (the console ends the title): kept, never run.
extern "C" int32_t win__crt_atexit(void* function){return function?registerOnexit(&processExit,function):-1;}
extern "C" int32_t win__crt_at_quick_exit(void*){return 0;}
extern "C" int32_t win__set_new_mode(int32_t){return 0;}
extern "C" void win__exit(int32_t code){stopGuest("_exit("+std::to_string(code)+") numa DLL carregada");}
extern "C" uint64_t win__beginthreadex(void* security,uint32_t stack,void* start,void* argument,uint32_t flags,uint32_t* id) {
    return reinterpret_cast<uint64_t>(win_CreateThread(security,stack,start,argument,flags,id));
}
extern "C" void* win__aligned_realloc(void* block,uint64_t size,uint64_t alignment) {
    if(!block)return win__aligned_malloc(size,alignment);
    if(!size){win__aligned_free(block);return nullptr;}
    void* fresh=win__aligned_malloc(size,alignment);
    if(!fresh)return nullptr;
    std::memcpy(fresh,block,std::min<uint64_t>(size,azimuth::heapUsable(block)));
    win__aligned_free(block);
    return fresh;
}
// Math (doubles pass in XMM registers both ways; the adapters keep them).
extern "C" double win_acos(double x){return std::acos(x);}
extern "C" double win_asin(double x){return std::asin(x);}
extern "C" double win_atan(double x){return std::atan(x);}
extern "C" double win_atan2(double y,double x){return std::atan2(y,x);}
extern "C" double win_cos(double x){return std::cos(x);}
extern "C" double win_cosh(double x){return std::cosh(x);}
extern "C" double win_exp(double x){return std::exp(x);}
extern "C" double win_exp2(double x){return std::exp2(x);}
extern "C" double win_fabs(double x){return std::fabs(x);}
extern "C" double win_floor(double x){return std::floor(x);}
extern "C" double win_fmax(double x,double y){return std::fmax(x,y);}
extern "C" double win_frexp(double x,int32_t* exponent){int e=0;double m=std::frexp(x,&e);if(exponent)*exponent=e;return m;}
extern "C" double win_hypot(double x,double y){return std::hypot(x,y);}
extern "C" double win_log(double x){return std::log(x);}
extern "C" double win_log10(double x){return std::log10(x);}
extern "C" double win_pow(double x,double y){return std::pow(x,y);}
extern "C" double win_round(double x){return std::round(x);}
extern "C" double win_sin(double x){return std::sin(x);}
extern "C" float win_sinf(float x){return std::sin(x);}
extern "C" double win_sinh(double x){return std::sinh(x);}
extern "C" double win_tan(double x){return std::tan(x);}
extern "C" double win_tanh(double x){return std::tanh(x);}
// Strings and conversions.
extern "C" uint64_t win_strcspn(const char* text,const char* set){return std::strcspn(text,set);}
extern "C" uint64_t win_strspn(const char* text,const char* set){return std::strspn(text,set);}
extern "C" uint16_t* win_wcscpy(uint16_t* to,const uint16_t* from){size_t n=length(from);std::memcpy(to,from,(n+1)*2);return to;}
extern "C" uint16_t* win_wcscat(uint16_t* to,const uint16_t* from){win_wcscpy(to+length(to),from);return to;}
extern "C" double win_strtod(const char* text,char** end){errno=0;double v=std::strtod(text,end);if(errno)lastErrno=errno;return v;}
extern "C" int64_t win_strtoll(const char* text,char** end,int32_t base){return win__strtoi64(text,end,base);}
extern "C" uint64_t win_strtoull(const char* text,char** end,int32_t base){return win__strtoui64(text,end,base);}
extern "C" int32_t win_rand(void){return std::rand()&0x7fff;}
extern "C" int32_t win_rand_s(uint32_t* value) {
    if(!value)return 22;   // EINVAL
    return win_BCryptGenRandom(nullptr,reinterpret_cast<uint8_t*>(value),4,2)>=0?0:22;   // BCRYPT_USE_SYSTEM_PREFERRED_RNG
}
extern "C" void* win_bsearch(const void* key,const void* base,uint64_t count,uint64_t size,const void* compare) {
    auto p=static_cast<const uint8_t*>(base);
    uint64_t low=0,high=count;
    while(low<high) {
        uint64_t mid=(low+high)/2;
        int32_t c=guestCallCompare(compare,key,p+mid*size);
        if(!c)return const_cast<uint8_t*>(p+mid*size);
        if(c<0)high=mid;else low=mid+1;
    }
    return nullptr;
}
// Time: UTC, no daylight saving (the console's clock is what time() gives).
extern "C" int32_t* win___daylight(void){return &windowsDaylight;}
extern "C" int32_t* win___timezone(void){return &timezoneSeconds;}
extern "C" char** win___tzname(void){return tzNames;}
extern "C" void win__tzset(void){}
extern "C" int32_t win_clock(void) {
    static const auto start=std::chrono::steady_clock::now();
    return int32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());   // CLOCKS_PER_SEC 1000
}
extern "C" int32_t win__gmtime64_s(void* out,const int64_t* time) {
    if(!out||!time)return 22;
    std::time_t t=std::time_t(*time);std::tm result{};
    if(!gmtime_r(&t,&result))return 22;
    toWindowsTm(result,out);return 0;
}
extern "C" int32_t win__localtime64_s(void* out,const int64_t* time){return win__gmtime64_s(out,time);}
extern "C" int64_t win__mktime64(void* windowsTm) {
    if(!windowsTm)return -1;
    std::tm t=fromWindowsTm(windowsTm);
    std::time_t result=timegm(&t);
    toWindowsTm(t,windowsTm);
    return int64_t(result);
}
