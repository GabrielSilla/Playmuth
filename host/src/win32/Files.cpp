// Files and directories. Windows paths are resolved against the current
// directory and served by the host directories mounted under them
// (Process::mounts): E:\PC\FH2 is the engine's folder and E:\ the drive that
// holds it, as on the Xbox. A path outside every mount is refused.
#include "Win32.h"
#include "WriteWatch.h"
#include "../HostHeap.h"
#include "../GuestThreads.h"
#include <mutex>
#include <chrono>
#include <list>
#include <unordered_map>
#include <map>
#include <set>
#include <atomic>
#include <algorithm>
#include <vector>
#include "../guest_abi.h"
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

namespace {
bool listDirectory(const std::string& directory,std::vector<std::string>& names,int& error);
}
namespace azimuth::win32 {
namespace {
char16_t upperAscii(char16_t c){return c>=u'a'&&c<=u'z'?char16_t(c-32):c;}
// The internal disk (/data) tells upper from lower case; Windows and the USB
// drive's exFAT do not (title 1.24: with the game on the internal SSD, its
// "Media\..." must find "media"). Below a mount on /data, each name of a
// path takes the case it has in its folder, from listings read once and
// kept; a name the folder does not hold (a file about to be made) keeps the
// case asked for.
std::mutex caseLock;
std::unordered_map<std::string,std::unordered_map<std::string,std::string>> caseListings;   // folder -> lowercase name -> name
std::string lowerAscii(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
std::string matchCase(const std::string& root,const std::string& rest) {
    std::string path=root;size_t at=0;
    while(at<rest.size()) {
        size_t slash=rest.find('/',at);if(slash==std::string::npos)slash=rest.size();
        std::string name=rest.substr(at,slash-at);
        if(!name.empty()) {
            std::string real;
            {
                std::lock_guard<std::mutex> guard(caseLock);
                auto listing=caseListings.find(path);
                if(listing==caseListings.end()) {
                    std::vector<std::string> names;int error=0;
                    std::unordered_map<std::string,std::string> byLower;
                    if(listDirectory(path,names,error))for(auto const& n:names)byLower.emplace(lowerAscii(n),n);
                    listing=caseListings.emplace(path,std::move(byLower)).first;
                }
                auto found=listing->second.find(lowerAscii(name));
                if(found!=listing->second.end())real=found->second;
            }
            if(real.empty())return path+"/"+rest.substr(at);   // not there: the rest as asked
            path+="/"+real;
        }
        at=slash+1;
    }
    return path;
}
bool sameText(const std::u16string& a,const std::u16string& b) {
    if(a.size()!=b.size())return false;
    for(size_t n=0;n<a.size();++n)if(upperAscii(a[n])!=upperAscii(b[n]))return false;
    return true;
}
unsigned outsideReports=0;
}
std::u16string fromGuest(const uint16_t* text){return text?std::u16string(reinterpret_cast<const char16_t*>(text),length(text)):std::u16string();}
std::u16string fullPath(const std::u16string& requested) {
    std::u16string path=requested;
    for(auto& c:path)if(c==u'/')c=u'\\';
    if(path.rfind(u"\\\\?\\",0)==0)path=path.substr(4);
    auto const& current=process().currentDirectory;
    std::u16string root,rest;
    if(path.size()>=2&&path[1]==u':') {
        root=std::u16string(1,upperAscii(path[0]))+u":";rest=path.substr(2);
        if(rest.empty()||rest[0]!=u'\\')rest=(sameText(root,current.substr(0,2))?current.substr(2):u"")+u"\\"+rest;
    }else if(!path.empty()&&path[0]==u'\\') {
        root=current.substr(0,2);rest=path;
    }else {
        root=current.substr(0,2);rest=current.substr(2)+u"\\"+path;
    }
    std::vector<std::u16string> parts;size_t at=0;
    while(at<=rest.size()) {
        auto end=rest.find(u'\\',at);if(end==std::u16string::npos)end=rest.size();
        auto part=rest.substr(at,end-at);at=end+1;
        if(part.empty()||part==u".")continue;
        if(part==u".."){if(!parts.empty())parts.pop_back();continue;}
        parts.push_back(part);
    }
    std::u16string full=root;
    for(auto const& part:parts)full+=u"\\"+part;
    if(parts.empty()||(!path.empty()&&path.back()==u'\\'))full+=u"\\";
    return full;
}
std::string hostPath(const std::u16string& full) {
    for(auto const& mount:process().mounts) {
        auto const& prefix=mount.guest;
        if(full.size()<prefix.size()||!sameText(full.substr(0,prefix.size()),prefix))continue;
        if(full.size()>prefix.size()&&full[prefix.size()]!=u'\\')continue;
        auto rest=full.substr(prefix.size());
        for(auto& c:rest)if(c==u'\\')c=u'/';
        auto host=mount.host+utf8(reinterpret_cast<const uint16_t*>(rest.data()),rest.size());
        while(host.size()>1&&host.back()=='/')host.pop_back();
        if(mount.host.rfind("/data/",0)==0&&host.size()>mount.host.size())
            host=matchCase(mount.host,host.substr(mount.host.size()+1));
        return host;
    }
    if(outsideReports++<20)log("Caminho fora das unidades montadas: "+utf8(reinterpret_cast<const uint16_t*>(full.data()),full.size()));
    setLastError(ErrorPathNotFound);return {};
}
}

using namespace azimuth::win32;

namespace {
constexpr uint32_t InvalidAttributes=0xffffffff,AttributeReadOnly=0x1,AttributeDirectory=0x10,AttributeArchive=0x20;
constexpr uint32_t GenericRead=0x80000000,GenericWrite=0x40000000,GenericAll=0x10000000,FileReadData=0x1,FileWriteData=0x2,FileAppendData=0x4;
constexpr uint32_t CreateNew=1,CreateAlways=2,OpenExisting=3,OpenAlways=4,TruncateExisting=5;
constexpr uint32_t FlagBackupSemantics=0x02000000,FlagDeleteOnClose=0x04000000;
constexpr int64_t UnixToFileTime=116444736000000000LL;
void* invalid(){return reinterpret_cast<void*>(InvalidHandle);}
std::string utf8Of(const std::u16string& text){return utf8(reinterpret_cast<const uint16_t*>(text.data()),text.size());}

// A Windows file handle is not a descriptor (title 1.06): the console ran out
// at about 256 with the game holding 239 files open (165 of them its car
// archives), and the engine could then not load FFmpeg. A File keeps its path,
// how to open it again and its own position; its descriptor lives in a cache
// of at most MaxDescriptors, the least recently used closed when one is
// needed, never one an operation is using (Descriptor pins it).
constexpr size_t MaxDescriptors=96;
struct File;
std::mutex descriptorsLock;
std::vector<File*> cached;   // files with a descriptor open
uint64_t useClock=0;
struct File:Object {
    bool directory=false,deleteOnClose=false;
    std::u16string guest;std::string host;
    int reopenFlags=O_RDONLY;            // open's flags without O_CREAT, O_TRUNC, O_EXCL
    std::atomic<uint64_t> position{0};   // the Windows file pointer
    int fd=-1;unsigned users=0;uint64_t lastUse=0;   // under descriptorsLock
    // The I/O completion port the handle is bound to (CreateIoCompletionPort),
    // and SetFileCompletionNotificationModes' flags.
    std::atomic<void*> port{nullptr};uint64_t portKey=0;uint8_t notifyModes=0;
    uint32_t cacheId=0;   // a game file (Mount): its reads go through the block cache
    uint32_t access=0;    // CreateFileW's desired access, for the read log
    ~File() override {
        {
            std::lock_guard<std::mutex> guard(descriptorsLock);
            if(fd>=0){close(fd);cached.erase(std::remove(cached.begin(),cached.end(),this),cached.end());}
        }
        if(deleteOnClose&&!host.empty())(directory?rmdir:unlink)(host.c_str());
    }
};
// Closes the least recently used descriptor no operation holds. Under descriptorsLock.
bool evictOne() {
    File* oldest=nullptr;
    for(auto f:cached)if(!f->users&&(!oldest||f->lastUse<oldest->lastUse))oldest=f;
    if(!oldest)return false;
    close(oldest->fd);oldest->fd=-1;
    cached.erase(std::remove(cached.begin(),cached.end(),oldest),cached.end());
    return true;
}
// open() with room made in the cache first, and again on EMFILE. Under descriptorsLock.
int openCached(const std::string& path,int flags) {
    while(cached.size()>=MaxDescriptors&&evictOne()){}
    for(;;) {
        int fd=open(path.c_str(),flags|O_CLOEXEC,0666);
        if(fd>=0||errno!=EMFILE||!evictOne())return fd;
    }
}
// The file's descriptor for one operation (opened again if the cache let it go).
struct Descriptor {
    File& file;int fd=-1;
    explicit Descriptor(File& f):file(f) {
        std::lock_guard<std::mutex> guard(descriptorsLock);
        if(file.fd<0) {
            file.fd=openCached(file.host,file.reopenFlags);
            if(file.fd>=0)cached.push_back(&file);
        }
        fd=file.fd;
        if(fd>=0){++file.users;file.lastUse=++useClock;}
    }
    ~Descriptor(){if(fd>=0){std::lock_guard<std::mutex> guard(descriptorsLock);--file.users;}}
    Descriptor(const Descriptor&)=delete;
};
// Opens a new File the first time (with O_CREAT/O_TRUNC as asked), into the cache.
bool openFirst(File& file,int flags) {
    std::lock_guard<std::mutex> guard(descriptorsLock);
    file.fd=openCached(file.host,flags);
    if(file.fd<0)return false;
    file.reopenFlags=flags&~(O_CREAT|O_TRUNC|O_EXCL);
    cached.push_back(&file);file.lastUse=++useClock;
    return true;
}
// Once, when open() says EMFILE even with the cache emptied: what holds descriptors.
void reportDescriptors() {
    static std::atomic<bool> said{false};
    if(said.exchange(true))return;
    size_t inCache;{std::lock_guard<std::mutex> guard(descriptorsLock);inCache=cached.size();}
    log("Descritores esgotados (EMFILE) com "+std::to_string(inCache)+" no cache de arquivos do host");
}
struct FoundEntry {std::u16string name;struct stat info;};
struct Search:Object {std::vector<FoundEntry> entries;size_t next=0;};

int64_t fileTime(const struct timespec& t){return int64_t(t.tv_sec)*10000000+t.tv_nsec/100+UnixToFileTime;}
#if defined(__linux__)
const struct timespec& modified(const struct stat& s){return s.st_mtim;}
const struct timespec& accessed(const struct stat& s){return s.st_atim;}
const struct timespec& created(const struct stat& s){return s.st_ctim;}
#else
const struct timespec& modified(const struct stat& s){return s.st_mtim;}
const struct timespec& accessed(const struct stat& s){return s.st_atim;}
const struct timespec& created(const struct stat& s){return s.st_birthtim;}
#endif
uint32_t attributes(const struct stat& s) {
    uint32_t value=S_ISDIR(s.st_mode)?AttributeDirectory:AttributeArchive;
    if(!(s.st_mode&S_IWUSR))value|=AttributeReadOnly;
    return value;
}
// ENOENT is "file not found" when the folder exists, "path not found" otherwise.
uint32_t missingError(const std::string& host) {
    auto slash=host.find_last_of('/');
    if(slash==std::string::npos||slash==0)return ErrorFileNotFound;
    struct stat parent;return stat(host.substr(0,slash).c_str(),&parent)==0&&S_ISDIR(parent.st_mode)?ErrorFileNotFound:ErrorPathNotFound;
}
void failWith(int error,const std::string& host){setLastError(error==ENOENT?missingError(host):errorFromErrno(error));}
unsigned failureReports=0;
void reportFailure(const char* call,const std::u16string& guest) {
    if(failureReports++<60)log(std::string(call)+": "+utf8Of(guest)+" -> erro "+std::to_string(win_GetLastError()));
}
// stat for the game's own content, the package's Mount folder, which nothing
// writes (title 1.17): the game checks whether each asset exists loose before
// its zip, 170 000 times a second on the thread that also runs the loading
// fiber, and each check was a stat on the USB drive. Results, absent files
// included, are kept by lowercase path (the drive's file system ignores case);
// a CreateFileW that writes there forgets its entry.
std::mutex statLock;
std::unordered_map<std::string,std::pair<int,struct stat>> statCache;
bool readOnlyContent(const std::string& host) {
    auto at=host.find("/Mount");
    return at!=std::string::npos&&(at+6==host.size()||host[at+6]=='/');
}
std::string statKey(std::string host){for(auto& c:host)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return host;}
// 0, or the errno of the stat.
int statOf(const std::string& host,struct stat& info) {
    if(!readOnlyContent(host))return stat(host.c_str(),&info)?errno:0;
    auto key=statKey(host);
    {
        std::lock_guard<std::mutex> guard(statLock);
        auto found=statCache.find(key);
        if(found!=statCache.end()){info=found->second.second;return found->second.first;}
    }
    struct stat fresh{};
    int error=stat(host.c_str(),&fresh)?errno:0;
    {std::lock_guard<std::mutex> guard(statLock);statCache[key]={error,fresh};}
    info=fresh;
    return error;
}
void forgetStat(const std::string& host) {
    if(!readOnlyContent(host))return;
    std::lock_guard<std::mutex> guard(statLock);statCache.erase(statKey(host));
}
// Reads of the game's own files go through a cache of 128 KiB blocks, up to
// 256 MiB, least recently used out (title 1.21: the game's file thread spent
// all its time in ReadFile, ~1 ms per small read from the USB drive, and the
// loading waited on it). Large reads go straight to the file. The Mount
// folder is never written, so the blocks never go stale.
constexpr uint64_t CacheBlock=128u<<10;
constexpr size_t CacheBlocks=2048,DirectRead=512u<<10;
struct Block {uint8_t* data;uint32_t length;std::list<uint64_t>::iterator recent;};
std::mutex cacheLock;
std::unordered_map<uint64_t,Block> blocks;
std::list<uint64_t> recentBlocks;   // most recent first
std::unordered_map<std::string,uint32_t> cacheIds;
uint32_t cacheIdOf(const std::string& host) {
    std::lock_guard<std::mutex> guard(cacheLock);
    auto& id=cacheIds[statKey(host)];
    if(!id)id=uint32_t(cacheIds.size());
    return id;
}
// Copies from a cached block; -1 when it is not cached.
int64_t fromCache(uint64_t key,uint64_t inBlock,uint8_t* out,size_t count) {
    std::lock_guard<std::mutex> guard(cacheLock);
    auto found=blocks.find(key);
    if(found==blocks.end())return -1;
    recentBlocks.splice(recentBlocks.begin(),recentBlocks,found->second.recent);
    if(inBlock>=found->second.length)return 0;
    size_t n=std::min<size_t>(count,found->second.length-inBlock);
    std::memcpy(out,found->second.data+inBlock,n);
    return int64_t(n);
}
void toCache(uint64_t key,uint8_t* data,uint32_t length) {
    std::lock_guard<std::mutex> guard(cacheLock);
    if(blocks.count(key)){azimuth::heapFree(data);return;}
    while(blocks.size()>=CacheBlocks) {
        auto oldest=recentBlocks.back();recentBlocks.pop_back();
        auto found=blocks.find(oldest);
        azimuth::heapFree(found->second.data);blocks.erase(found);
    }
    recentBlocks.push_front(key);
    blocks[key]=Block{data,length,recentBlocks.begin()};
}
void forgetCache() {
    std::lock_guard<std::mutex> guard(cacheLock);
    for(auto& entry:blocks)azimuth::heapFree(entry.second.data);
    blocks.clear();recentBlocks.clear();
}
// pread of a game file through the cache: the bytes read, or -1 (errno set).
int64_t readCached(uint32_t id,int fd,uint8_t* out,size_t count,uint64_t start) {
    if(count>=DirectRead) {
        size_t total=0;
        while(total<count) {
            auto n=watchedPread(fd,out+total,count-total,off_t(start+total));
            if(n<0)return total?int64_t(total):-1;
            if(!n)break;
            total+=size_t(n);
        }
        return int64_t(total);
    }
    size_t total=0;
    while(total<count) {
        uint64_t at=start+total,index=at/CacheBlock,inBlock=at%CacheBlock;
        uint64_t key=uint64_t(id)<<40|index;
        int64_t got=fromCache(key,inBlock,out+total,count-total);
        if(got<0) {
            auto data=static_cast<uint8_t*>(azimuth::heapAlloc(CacheBlock,64,false));
            if(!data)return total?int64_t(total):-1;
            size_t length=0;
            while(length<CacheBlock) {
                auto n=pread(fd,data+length,CacheBlock-length,off_t(index*CacheBlock+length));
                if(n<0){azimuth::heapFree(data);return total?int64_t(total):-1;}
                if(!n)break;
                length+=size_t(n);
            }
            got=inBlock<length?int64_t(std::min<size_t>(count-total,length-inBlock)):0;
            if(got)std::memcpy(out+total,data+inBlock,size_t(got));
            toCache(key,data,uint32_t(length));
        }
        if(!got)break;   // end of file
        total+=size_t(got);
    }
    return int64_t(total);
}
// Resolves a guest name to its full Windows path and host path; false (error set) when it cannot.
bool resolve(const uint16_t* name,std::u16string& guest,std::string& host) {
    if(!name||!*name){setLastError(ErrorInvalidParameter);return false;}
    guest=fullPath(fromGuest(name));host=hostPath(guest);
    return !host.empty();
}
// Windows wildcards, case-insensitive: '*' any run, '?' one character.
bool matches(const char16_t* mask,const char16_t* name) {
    if(!*mask)return !*name;
    if(*mask==u'*'){for(;;++name){if(matches(mask+1,name))return true;if(!*name)return false;}}
    if(!*name)return false;
    auto a=*mask,b=*name;if(a>=u'a'&&a<=u'z')a=char16_t(a-32);if(b>=u'a'&&b<=u'z')b=char16_t(b-32);
    return (*mask==u'?'||a==b)&&matches(mask+1,name+1);
}
void fillFindData(const FoundEntry& entry,void* data) {
    auto p=static_cast<uint8_t*>(data);std::memset(p,0,592);
    uint32_t attrs=attributes(entry.info);std::memcpy(p,&attrs,4);
    int64_t times[3]={fileTime(created(entry.info)),fileTime(accessed(entry.info)),fileTime(modified(entry.info))};
    std::memcpy(p+4,times,24);
    uint64_t size=S_ISDIR(entry.info.st_mode)?0:uint64_t(entry.info.st_size);
    uint32_t high=uint32_t(size>>32),low=uint32_t(size);std::memcpy(p+28,&high,4);std::memcpy(p+32,&low,4);
    auto count=entry.name.size()<259?entry.name.size():259;
    std::memcpy(p+44,entry.name.data(),count*2);
}
#if !defined(__linux__)
extern "C" int sceKernelGetdents(int fd,char* buffer,int size);
#endif
// The names in a directory. opendir/readdir first; on the console, when that
// is refused, the kernel's own getdents on a descriptor of the folder (the
// PS4/PS5 dirent: d_fileno u32, d_reclen u16, d_type u8, d_namlen u8, d_name).
bool listDirectory(const std::string& directory,std::vector<std::string>& names,int& error) {
    if(DIR* listing=opendir(directory.c_str())) {
        while(dirent* item=readdir(listing))names.emplace_back(item->d_name);
        closedir(listing);
        return true;
    }
    error=errno;
#if !defined(__linux__)
    int fd=open(directory.c_str(),O_RDONLY|O_DIRECTORY);
    if(fd<0){log("listar "+directory+": opendir errno "+std::to_string(error)+", open errno "+std::to_string(errno));return false;}
    std::vector<char> buffer(65536);
    for(;;) {
        int got=sceKernelGetdents(fd,buffer.data(),int(buffer.size()));
        if(got<0){log("listar "+directory+": getdents "+std::to_string(got));close(fd);return false;}
        if(!got)break;
        for(int at=0;at+8<=got;) {
            uint16_t record;std::memcpy(&record,buffer.data()+at+4,2);
            if(record<8||at+record>got)break;
            uint32_t fileno;std::memcpy(&fileno,buffer.data()+at,4);
            if(fileno)names.emplace_back(buffer.data()+at+8);
            at+=record;
        }
    }
    close(fd);
    log("listar "+directory+": opendir falhou (errno "+std::to_string(error)+"), getdents leu "+std::to_string(names.size())+" nomes");
    return true;
#else
    return false;
#endif
}
void* startSearch(const uint16_t* pattern,void* data) {
    std::u16string guest;std::string host;
    if(!resolve(pattern,guest,host)){reportFailure("FindFirstFile",guest);return invalid();}
    auto slash=guest.find_last_of(u'\\');
    auto mask=guest.substr(slash+1);if(mask.empty())mask=u"*";
    auto directory=host.substr(0,host.find_last_of('/'));if(directory.empty())directory="/";
    auto search=std::make_shared<Search>();
    std::vector<std::string> names;int error=0;
    if(!listDirectory(directory,names,error)) {
        // The host's errno, not only the Windows error it becomes: a title may
        // be refused a listing a payload gets (title 0.27: erro 5 on the game's Mount folder).
        static unsigned shown=0;
        if(shown++<8)log("FindFirstFile: listar "+directory+" falhou, errno "+std::to_string(error));
        failWith(error,directory);reportFailure("FindFirstFile",guest);return invalid();
    }
    for(auto const& name8:names) {
        auto name=utf16(name8);
        if(!matches(mask.c_str(),name.c_str()))continue;
        FoundEntry entry{name,{}};
        if(stat((directory+"/"+name8).c_str(),&entry.info))continue;
        search->entries.push_back(std::move(entry));
    }
    if(search->entries.empty()){setLastError(ErrorFileNotFound);reportFailure("FindFirstFile",guest);return invalid();}
    fillFindData(search->entries[0],data);search->next=1;
    return addHandle(search);
}
int copyContents(const std::string& from,const std::string& to,bool failIfExists) {
    int in=open(from.c_str(),O_RDONLY|O_CLOEXEC);if(in<0)return errno;
    int out=open(to.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|(failIfExists?O_EXCL:0),0666);
    if(out<0){int error=errno;close(in);return error;}
    char buffer[65536];int error=0;
    for(;;) {
        auto n=read(in,buffer,sizeof buffer);if(n<0){error=errno;break;}if(!n)break;
        for(ssize_t done=0;done<n;){auto w=write(out,buffer+done,size_t(n-done));if(w<0){error=errno;break;}done+=w;}
        if(error)break;
    }
    if(!error&&fsync(out))error=errno;
    close(in);close(out);return error;
}
}

extern "C" uint32_t win_GetFullPathNameW(const uint16_t* name,uint32_t count,uint16_t* path,uint16_t** filePart) {
    if(filePart)*filePart=nullptr;
    if(!name||!*name||(count&&!path)){setLastError(ErrorInvalidParameter);return 0;}
    auto full=fullPath(fromGuest(name));
    if(full.size()>32766){setLastError(ErrorFilenameExcedRange);return 0;}
    if(count<=full.size())return uint32_t(full.size()+1);
    std::memcpy(path,full.data(),full.size()*2);path[full.size()]=0;
    auto slash=full.find_last_of(u'\\');
    if(filePart&&slash!=std::u16string::npos&&slash+1<full.size())*filePart=path+slash+1;
    return uint32_t(full.size());
}
extern "C" uint32_t win_GetFullPathNameA(const char* name,uint32_t count,char* path,char** filePart) {
    if(filePart)*filePart=nullptr;
    if(!name||!*name||(count&&!path)){setLastError(ErrorInvalidParameter);return 0;}
    auto full=utf8Of(fullPath(utf16(name)));
    if(count<=full.size())return uint32_t(full.size()+1);
    std::memcpy(path,full.c_str(),full.size()+1);
    auto slash=full.find_last_of('\\');
    if(filePart&&slash!=std::string::npos&&slash+1<full.size())*filePart=path+slash+1;
    return uint32_t(full.size());
}
// The current directory is the engine's folder, fixed: changing it is accepted
// for a folder that exists and otherwise ignored (the host resolves relative
// paths from process().currentDirectory).
extern "C" uint32_t win_GetCurrentDirectoryW(uint32_t count,uint16_t* buffer) {
    auto const& current=process().currentDirectory;
    if(count<=current.size()||!buffer)return uint32_t(current.size()+1);
    std::memcpy(buffer,current.data(),current.size()*2);buffer[current.size()]=0;return uint32_t(current.size());
}
extern "C" uint32_t win_GetCurrentDirectoryA(uint32_t count,char* buffer) {
    auto current=utf8Of(process().currentDirectory);
    if(count<=current.size()||!buffer)return uint32_t(current.size()+1);
    std::memcpy(buffer,current.c_str(),current.size()+1);return uint32_t(current.size());
}
extern "C" int32_t win_SetCurrentDirectoryW(const uint16_t* name) {
    std::u16string guest;std::string host;struct stat info;
    if(!resolve(name,guest,host)||stat(host.c_str(),&info)||!S_ISDIR(info.st_mode)){setLastError(ErrorPathNotFound);return 0;}
    if(guest!=process().currentDirectory)log("SetCurrentDirectory para "+utf8Of(guest)+" ignorado: a pasta atual continua a do motor");
    return 1;
}
extern "C" int32_t win_SetCurrentDirectoryA(const char* name) {
    if(!name){setLastError(ErrorInvalidParameter);return 0;}
    auto wide=utf16(name);return win_SetCurrentDirectoryW(reinterpret_cast<const uint16_t*>(wide.c_str()));
}
// The temporary folder is the engine's data\tmp.
extern "C" uint32_t win_GetTempPathW(uint32_t count,uint16_t* buffer) {
    std::u16string path=u"E:\\PC\\FH2\\data\\tmp\\";
    if(count<=path.size()||!buffer)return uint32_t(path.size()+1);
    std::memcpy(buffer,path.data(),path.size()*2);buffer[path.size()]=0;return uint32_t(path.size());
}
extern "C" uint32_t win_GetTempPath2W(uint32_t count,uint16_t* buffer){return win_GetTempPathW(count,buffer);}
extern "C" uint32_t win_GetTempPathA(uint32_t count,char* buffer) {
    std::string path="E:\\PC\\FH2\\data\\tmp\\";
    if(count<=path.size()||!buffer)return uint32_t(path.size()+1);
    std::memcpy(buffer,path.c_str(),path.size()+1);return uint32_t(path.size());
}
extern "C" uint32_t win_GetDriveTypeW(const uint16_t*){return 3;}   // DRIVE_FIXED
extern "C" uint32_t win_GetLogicalDrives(void){return 1u<<4;}       // E:
extern "C" int32_t win_SetFileAttributesW(const uint16_t* name,uint32_t) {
    std::u16string guest;std::string host;struct stat info;
    if(!resolve(name,guest,host)||stat(host.c_str(),&info)){setLastError(ErrorFileNotFound);return 0;}
    return 1;   // the host's files have no Windows attributes to keep
}
// One process: nothing else locks its files.
extern "C" int32_t win_LockFileEx(void*,uint32_t,uint32_t,uint32_t,uint32_t,void*){return 1;}
extern "C" int32_t win_UnlockFileEx(void*,uint32_t,uint32_t,uint32_t,void*){return 1;}
extern "C" uint32_t win_GetFileAttributesW(const uint16_t* name) {
    std::u16string guest;std::string host;
    if(!resolve(name,guest,host)){reportFailure("GetFileAttributesW",guest);return InvalidAttributes;}
    struct stat info;
    if(int error=statOf(host,info)){failWith(error,host);reportFailure("GetFileAttributesW",guest);return InvalidAttributes;}
    return attributes(info);
}
extern "C" int32_t win_GetFileAttributesExW(const uint16_t* name,uint32_t level,void* data) {
    if(level!=0||!data){setLastError(ErrorInvalidParameter);return 0;}
    std::u16string guest;std::string host;
    if(!resolve(name,guest,host)){reportFailure("GetFileAttributesExW",guest);return 0;}
    struct stat info;
    if(int error=statOf(host,info)){failWith(error,host);reportFailure("GetFileAttributesExW",guest);return 0;}
    // WIN32_FILE_ATTRIBUTE_DATA: attributes, three FILETIMEs, size high, size low.
    auto p=static_cast<uint8_t*>(data);uint32_t attrs=attributes(info);std::memcpy(p,&attrs,4);
    int64_t times[3]={fileTime(created(info)),fileTime(accessed(info)),fileTime(modified(info))};std::memcpy(p+4,times,24);
    uint64_t size=S_ISDIR(info.st_mode)?0:uint64_t(info.st_size);
    uint32_t high=uint32_t(size>>32),low=uint32_t(size);std::memcpy(p+28,&high,4);std::memcpy(p+32,&low,4);
    return 1;
}
extern "C" void* win_CreateFileW(const uint16_t* name,uint32_t access,uint32_t,void*,uint32_t disposition,uint32_t flags,void*) {
    auto file=std::make_shared<File>();
    if(!resolve(name,file->guest,file->host)){reportFailure("CreateFileW",file->guest);return invalid();}
    struct stat info;int missing=statOf(file->host,info);bool exists=missing==0;
    if(exists&&S_ISDIR(info.st_mode)) {
        if(!(flags&FlagBackupSemantics)||disposition==CreateNew){setLastError(disposition==CreateNew?ErrorFileExists:ErrorAccessDenied);reportFailure("CreateFileW",file->guest);return invalid();}
        file->directory=true;
        if(!openFirst(*file,O_RDONLY|O_DIRECTORY)){if(errno==EMFILE)reportDescriptors();failWith(errno,file->host);reportFailure("CreateFileW",file->guest);return invalid();}
        file->deleteOnClose=flags&FlagDeleteOnClose;setLastError(ErrorSuccess);
        return addHandle(file);
    }
    bool write=access&(GenericWrite|GenericAll|FileWriteData|FileAppendData);
    bool read=access&(GenericRead|GenericAll|FileReadData);
    if(write||disposition==CreateNew||disposition==CreateAlways||disposition==OpenAlways)forgetStat(file->host);
    // A game file known to be absent fails without another open on the drive.
    else if(!exists&&readOnlyContent(file->host)){failWith(missing,file->host);reportFailure("CreateFileW",file->guest);return invalid();}
    int mode=write?(read?O_RDWR:O_WRONLY):O_RDONLY;
    switch(disposition) {
        case CreateNew:mode|=O_CREAT|O_EXCL;break;
        case CreateAlways:mode|=O_CREAT|O_TRUNC;break;
        case OpenExisting:break;
        case OpenAlways:mode|=O_CREAT;break;
        case TruncateExisting:mode|=O_TRUNC;break;
        default:setLastError(ErrorInvalidParameter);return invalid();
    }
    if((mode&O_CREAT)&&!write)mode=(mode&~O_RDONLY)|O_RDWR;
    if(!openFirst(*file,mode)){if(errno==EMFILE)reportDescriptors();failWith(errno,file->host);reportFailure("CreateFileW",file->guest);return invalid();}
    // Any game file, whatever access it was opened with (title 1.22: the
    // reads that mattered missed the cache when only read-only opens used it).
    file->access=access;
    if(readOnlyContent(file->host))file->cacheId=cacheIdOf(file->host);
    file->deleteOnClose=flags&FlagDeleteOnClose;
    setLastError(exists&&(disposition==CreateAlways||disposition==OpenAlways)?ErrorAlreadyExists:ErrorSuccess);
    return addHandle(file);
}
extern "C" int32_t win_CloseHandle(void* handle) {
    auto value=reinterpret_cast<intptr_t>(handle);
    if(value==-1||value==-2)return 1;   // the current process and thread pseudo-handles
    if(removeHandle(handle))return 1;
    setLastError(ErrorInvalidHandle);return 0;
}
extern "C" uint32_t win_GetFileType(void* handle) {
    if(findHandleOf<File>(handle)){setLastError(ErrorSuccess);return 1;}   // FILE_TYPE_DISK
    setLastError(ErrorInvalidHandle);return 0;
}
extern "C" int32_t win_SetEvent(void*);   // Threads.cpp
// OVERLAPPED: Internal, InternalHigh, Offset, OffsetHigh, hEvent. Overlapped
// transfers complete before the call returns. On a handle bound to an I/O
// completion port, as on Windows, the completion is also a packet on the port
// (unless hEvent's low bit is set or FILE_SKIP_COMPLETION_PORT_ON_SUCCESS),
// and the call then answers ERROR_IO_PENDING, the asynchronous path every
// caller handles. Title 1.16: the port association was dropped, no packet ever
// came, and the game's world streaming, which waits on its port, never loaded
// the scenery.
namespace {
constexpr uint32_t ErrorIoPending=997;
constexpr uint8_t SkipPortOnSuccess=0x1,SkipSetEvent=0x2;
uint64_t overlappedOffset(const void* overlapped){uint32_t low,high;std::memcpy(&low,static_cast<const uint8_t*>(overlapped)+16,4);std::memcpy(&high,static_cast<const uint8_t*>(overlapped)+20,4);return uint64_t(high)<<32|low;}
// Fills the OVERLAPPED, signals its event, queues the packet; true when a packet went to a port.
bool completeOverlapped(File& file,void* overlapped,uint64_t status,uint64_t count,bool success) {
    auto p=static_cast<uint8_t*>(overlapped);std::memcpy(p,&status,8);std::memcpy(p+8,&count,8);
    void* stored;std::memcpy(&stored,p+24,8);
    bool noPacket=reinterpret_cast<uintptr_t>(stored)&1;
    void* event=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(stored)&~uintptr_t(1));
    if(event&&!(file.notifyModes&SkipSetEvent&&success))win_SetEvent(event);
    void* port=file.port.load();
    if(!success||!port||noPacket||(file.notifyModes&SkipPortOnSuccess))return false;
    static std::atomic<unsigned> posted{0};
    if(posted++==0)log("E/S assincrona: primeira conclusao entregue a uma porta de conclusao ("+utf8Of(file.guest)+")");
    return win_PostQueuedCompletionStatus(port,uint32_t(count),file.portKey,overlapped)!=0;
}
}
namespace azimuth::win32 {
// CreateIoCompletionPort with a file handle (Threads.cpp): binds it to the port.
bool bindCompletionPort(void* handle,void* port,uint64_t key) {
    auto file=findHandleOf<File>(handle);
    if(!file)return false;
    file->portKey=key;file->port=port;
    return true;
}
}
extern "C" int32_t win_SetFileCompletionNotificationModes(void* handle,uint8_t flags) {
    auto file=findHandleOf<File>(handle);
    if(!file){setLastError(ErrorInvalidHandle);return 0;}
    file->notifyModes=flags;
    return 1;
}
extern "C" int32_t win_ReadFile(void* handle,void* buffer,uint32_t count,uint32_t* done,void* overlapped) {
    auto file=findHandleOf<File>(handle);
    if(!file||file->directory){setLastError(ErrorInvalidHandle);return 0;}
    if(done)*done=0;
    Descriptor d(*file);
    if(d.fd<0){setLastError(errorFromErrno(errno));return 0;}
    uint64_t start=overlapped?overlappedOffset(overlapped):file->position.load();
    size_t total=0;
    auto began=std::chrono::steady_clock::now();
    if(file->cacheId) {
        auto got=readCached(file->cacheId,d.fd,static_cast<uint8_t*>(buffer),count,start);
        if(got<0){setLastError(errorFromErrno(errno));return 0;}
        total=size_t(got);
    }else while(total<count) {
        auto n=watchedPread(d.fd,static_cast<uint8_t*>(buffer)+total,count-total,off_t(start+total));
        if(n<0){setLastError(errorFromErrno(errno));return 0;}
        if(!n)break;
        total+=size_t(n);
    }
    // Which reads miss the cache, and which are slow (title 1.22).
    {
        auto micros=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count();
        static std::atomic<unsigned> uncached{0},slow{0};
        static std::atomic<int64_t> epoch{-1};
        int64_t window=fastMonotonicNs()/10000000000ll;auto previous=epoch.load();
        if(previous!=window&&epoch.compare_exchange_strong(previous,window))slow=0;
        bool logUncached=!file->cacheId&&uncached++<40,logSlow=micros>=5000&&slow++<4;
        if(logUncached||logSlow) {
            char text[160];
            std::snprintf(text,sizeof text,"ReadFile%s: %u bytes em %llu, %lld us, acesso 0x%x%s, ",logSlow?" lento":" fora do cache",count,
                          static_cast<unsigned long long>(start),static_cast<long long>(micros),file->access,file->cacheId?", com cache":"");
            log(text+file->host+"; thread "+std::to_string(azimuth::currentGuestThreadId()));
        }
    }
    if(!overlapped)file->position=start+total;
    if(done)*done=uint32_t(total);
    if(overlapped) {
        if(!total&&count){completeOverlapped(*file,overlapped,0xc0000011,0,false);setLastError(ErrorHandleEof);return 0;}   // STATUS_END_OF_FILE
        if(completeOverlapped(*file,overlapped,0,total,true)){setLastError(ErrorIoPending);return 0;}
    }
    return 1;
}
extern "C" int32_t win_WriteFile(void* handle,const void* buffer,uint32_t count,uint32_t* done,void* overlapped) {
    auto file=findHandleOf<File>(handle);
    if(!file||file->directory){setLastError(ErrorInvalidHandle);return 0;}
    // A write to a game file (never seen): its cached blocks would go stale, so the whole cache goes.
    if(file->cacheId){forgetCache();file->cacheId=0;log("WriteFile num arquivo do jogo: cache de leitura descartado ("+file->host+")");}
    if(done)*done=0;
    Descriptor d(*file);
    if(d.fd<0){setLastError(errorFromErrno(errno));return 0;}
    uint64_t start=overlapped?overlappedOffset(overlapped):file->position.load();
    if(!overlapped&&(file->reopenFlags&O_APPEND)){struct stat info;if(!fstat(d.fd,&info))start=uint64_t(info.st_size);}
    size_t total=0;
    while(total<count) {
        auto n=pwrite(d.fd,static_cast<const uint8_t*>(buffer)+total,count-total,off_t(start+total));
        if(n<0){setLastError(errorFromErrno(errno));return 0;}
        total+=size_t(n);
    }
    if(!overlapped)file->position=start+total;
    if(done)*done=uint32_t(total);
    if(overlapped&&completeOverlapped(*file,overlapped,0,total,true)){setLastError(ErrorIoPending);return 0;}
    return 1;
}
extern "C" int32_t win_GetFileSizeEx(void* handle,int64_t* size) {
    auto file=findHandleOf<File>(handle);struct stat info;
    if(!file){setLastError(ErrorInvalidHandle);return 0;}
    Descriptor d(*file);
    if(d.fd<0||fstat(d.fd,&info)){setLastError(ErrorInvalidHandle);return 0;}
    *size=info.st_size;return 1;
}
extern "C" int32_t win_SetFilePointerEx(void* handle,int64_t distance,int64_t* position,uint32_t method) {
    auto file=findHandleOf<File>(handle);
    if(!file||method>2){setLastError(file?ErrorInvalidParameter:ErrorInvalidHandle);return 0;}
    int64_t base=0;
    if(method==1)base=int64_t(file->position.load());
    else if(method==2) {
        Descriptor d(*file);struct stat info;
        if(d.fd<0||fstat(d.fd,&info)){setLastError(errorFromErrno(errno));return 0;}
        base=int64_t(info.st_size);
    }
    int64_t at=base+distance;
    if(at<0){setLastError(131);return 0;}   // ERROR_NEGATIVE_SEEK
    file->position=uint64_t(at);
    if(position)*position=at;
    return 1;
}
extern "C" uint32_t win_SetFilePointer(void* handle,int32_t low,int32_t* high,uint32_t method) {
    int64_t distance=high?(int64_t(*high)<<32)|uint32_t(low):int64_t(low),position=0;
    if(!win_SetFilePointerEx(handle,distance,&position,method))return 0xffffffff;
    if(high)*high=int32_t(position>>32);
    setLastError(ErrorSuccess);return uint32_t(position);
}
extern "C" int32_t win_SetEndOfFile(void* handle) {
    auto file=findHandleOf<File>(handle);if(!file){setLastError(ErrorInvalidHandle);return 0;}
    Descriptor d(*file);
    if(d.fd<0||ftruncate(d.fd,off_t(file->position.load()))){setLastError(errorFromErrno(errno));return 0;}
    return 1;
}
extern "C" int32_t win_FlushFileBuffers(void* handle) {
    auto file=findHandleOf<File>(handle);if(!file){setLastError(ErrorInvalidHandle);return 0;}
    Descriptor d(*file);
    if(d.fd<0||fsync(d.fd)){setLastError(errorFromErrno(errno));return 0;}
    return 1;
}
extern "C" int32_t win_GetFileInformationByHandleEx(void* handle,uint32_t kind,void* data,uint32_t size) {
    auto file=findHandleOf<File>(handle);struct stat info;
    if(!file){setLastError(ErrorInvalidHandle);return 0;}
    {Descriptor d(*file);if(d.fd<0||fstat(d.fd,&info)){setLastError(ErrorInvalidHandle);return 0;}}
    auto p=static_cast<uint8_t*>(data);
    if(kind==0&&size>=40) {   // FILE_BASIC_INFO: four LARGE_INTEGER times, attributes
        std::memset(p,0,40);
        int64_t times[4]={fileTime(created(info)),fileTime(accessed(info)),fileTime(modified(info)),fileTime(modified(info))};
        std::memcpy(p,times,32);uint32_t attrs=attributes(info);std::memcpy(p+32,&attrs,4);return 1;
    }
    if(kind==1&&size>=24) {   // FILE_STANDARD_INFO: AllocationSize, EndOfFile, NumberOfLinks, DeletePending, Directory
        std::memset(p,0,24);int64_t sizes[2]={int64_t(info.st_blocks)*512,int64_t(info.st_size)};std::memcpy(p,sizes,16);
        uint32_t links=uint32_t(info.st_nlink);std::memcpy(p+16,&links,4);p[20]=file->deleteOnClose;p[21]=file->directory;return 1;
    }
    if(kind==2&&size>=4) {    // FILE_NAME_INFO: FileNameLength (bytes), the path without the drive
        auto name=file->guest.substr(2);uint32_t bytes=uint32_t(name.size()*2);std::memcpy(p,&bytes,4);
        if(size<4+bytes){setLastError(234);return 0;}   // ERROR_MORE_DATA
        std::memcpy(p+4,name.data(),bytes);return 1;
    }
    log("GetFileInformationByHandleEx: classe "+std::to_string(kind)+" ainda sem implementacao");
    setLastError(ErrorInvalidParameter);return 0;
}
// BY_HANDLE_FILE_INFORMATION (52 bytes): attributes, three FILETIMEs, volume
// serial, size high/low, links, file index high/low.
extern "C" int32_t win_GetFileInformationByHandle(void* handle,void* data) {
    auto file=findHandleOf<File>(handle);struct stat info;
    if(!file||!data){setLastError(ErrorInvalidHandle);return 0;}
    {Descriptor d(*file);if(d.fd<0||fstat(d.fd,&info)){setLastError(ErrorInvalidHandle);return 0;}}
    auto p=static_cast<uint8_t*>(data);std::memset(p,0,52);
    uint32_t attrs=attributes(info);std::memcpy(p,&attrs,4);
    int64_t times[3]={fileTime(created(info)),fileTime(accessed(info)),fileTime(modified(info))};std::memcpy(p+4,times,24);
    uint32_t serial=uint32_t(info.st_dev),high=uint32_t(uint64_t(info.st_size)>>32),low=uint32_t(info.st_size),links=uint32_t(info.st_nlink);
    uint64_t index=uint64_t(info.st_ino);
    std::memcpy(p+28,&serial,4);std::memcpy(p+32,&high,4);std::memcpy(p+36,&low,4);std::memcpy(p+40,&links,4);
    uint32_t indexHigh=uint32_t(index>>32),indexLow=uint32_t(index);std::memcpy(p+44,&indexHigh,4);std::memcpy(p+48,&indexLow,4);
    return 1;
}
extern "C" int32_t win_SetFileInformationByHandle(void* handle,uint32_t kind,void* data,uint32_t size) {
    auto file=findHandleOf<File>(handle);if(!file){setLastError(ErrorInvalidHandle);return 0;}
    auto p=static_cast<uint8_t*>(data);
    if(kind==4&&size>=1){file->deleteOnClose=p[0]!=0;return 1;}   // FILE_DISPOSITION_INFO
    if(kind==6&&size>=8) {                                         // FILE_END_OF_FILE_INFO
        int64_t end;std::memcpy(&end,p,8);
        Descriptor d(*file);
        if(d.fd<0||ftruncate(d.fd,off_t(end))){setLastError(errorFromErrno(errno));return 0;}
        return 1;
    }
    if(kind==5)return 1;   // FILE_ALLOCATION_INFO: a hint
    if(kind==3&&size>=20) { // FILE_RENAME_INFO: flags, root, FileNameLength (bytes), FileName
        uint32_t replace,bytes;std::memcpy(&replace,p,4);std::memcpy(&bytes,p+16,4);
        std::u16string target(reinterpret_cast<const char16_t*>(p+20),bytes/2);
        auto guest=fullPath(target);auto host=hostPath(guest);if(host.empty())return 0;
        struct stat existing;
        if(!(replace&1)&&stat(host.c_str(),&existing)==0){setLastError(ErrorAlreadyExists);return 0;}
        if(rename(file->host.c_str(),host.c_str())){failWith(errno,host);return 0;}
        file->guest=guest;file->host=host;return 1;
    }
    log("SetFileInformationByHandle: classe "+std::to_string(kind)+" ainda sem implementacao");
    setLastError(ErrorInvalidParameter);return 0;
}
extern "C" uint32_t win_GetFinalPathNameByHandleW(void* handle,uint16_t* path,uint32_t count,uint32_t flags) {
    auto file=findHandleOf<File>(handle);if(!file){setLastError(ErrorInvalidHandle);return 0;}
    std::u16string value=(flags&4)?file->guest.substr(2):u"\\\\?\\"+file->guest;   // VOLUME_NAME_NONE / VOLUME_NAME_DOS
    if(count<=value.size())return uint32_t(value.size()+1);
    std::memcpy(path,value.data(),value.size()*2);path[value.size()]=0;
    return uint32_t(value.size());
}
extern "C" void* win_FindFirstFileW(const uint16_t* pattern,void* data){return startSearch(pattern,data);}
extern "C" void* win_FindFirstFileExW(const uint16_t* pattern,uint32_t level,void* data,uint32_t,void*,uint32_t) {
    if(level>1){setLastError(ErrorInvalidParameter);return invalid();}   // FindExInfoStandard or FindExInfoBasic
    return startSearch(pattern,data);
}
extern "C" int32_t win_FindNextFileW(void* handle,void* data) {
    auto search=findHandleOf<Search>(handle);if(!search){setLastError(ErrorInvalidHandle);return 0;}
    if(search->next>=search->entries.size()){setLastError(ErrorNoMoreFiles);return 0;}
    fillFindData(search->entries[search->next++],data);return 1;
}
extern "C" int32_t win_FindClose(void* handle) {
    if(!findHandleOf<Search>(handle)||!removeHandle(handle)){setLastError(ErrorInvalidHandle);return 0;}
    return 1;
}
extern "C" int32_t win_CreateDirectoryW(const uint16_t* name,void*) {
    std::u16string guest;std::string host;if(!resolve(name,guest,host))return 0;
    if(mkdir(host.c_str(),0777)){setLastError(errno==EEXIST?ErrorAlreadyExists:errno==ENOENT?ErrorPathNotFound:errorFromErrno(errno));return 0;}
    return 1;
}
extern "C" int32_t win_RemoveDirectoryW(const uint16_t* name) {
    std::u16string guest;std::string host;if(!resolve(name,guest,host))return 0;
    if(rmdir(host.c_str())){failWith(errno,host);return 0;}
    return 1;
}
extern "C" int32_t win_DeleteFileW(const uint16_t* name) {
    std::u16string guest;std::string host;if(!resolve(name,guest,host))return 0;
    if(unlink(host.c_str())){failWith(errno,host);return 0;}
    return 1;
}
extern "C" int32_t win_MoveFileExW(const uint16_t* from,const uint16_t* to,uint32_t flags) {
    constexpr uint32_t ReplaceExisting=1,CopyAllowed=2;
    std::u16string fromGuest,toGuest;std::string fromHost,toHost;
    if(!resolve(from,fromGuest,fromHost)||!resolve(to,toGuest,toHost))return 0;
    struct stat existing;
    if(!(flags&ReplaceExisting)&&stat(toHost.c_str(),&existing)==0){setLastError(ErrorAlreadyExists);return 0;}
    if(rename(fromHost.c_str(),toHost.c_str())==0)return 1;
    if(errno==EXDEV&&(flags&CopyAllowed)) {
        if(int error=copyContents(fromHost,toHost,false)){setLastError(errorFromErrno(error));return 0;}
        unlink(fromHost.c_str());return 1;
    }
    failWith(errno,fromHost);return 0;
}
extern "C" int32_t win_CopyFileW(const uint16_t* from,const uint16_t* to,int32_t failIfExists) {
    std::u16string fromGuest,toGuest;std::string fromHost,toHost;
    if(!resolve(from,fromGuest,fromHost)||!resolve(to,toGuest,toHost))return 0;
    if(int error=copyContents(fromHost,toHost,failIfExists)){setLastError(error==EEXIST?ErrorFileExists:error==ENOENT?missingError(fromHost):errorFromErrno(error));return 0;}
    return 1;
}
extern "C" int32_t win_GetDiskFreeSpaceExW(const uint16_t* directory,uint64_t* available,uint64_t* total,uint64_t* free) {
    std::u16string guest;std::string host;
    if(directory){if(!resolve(directory,guest,host))return 0;}
    else{guest=process().currentDirectory;host=hostPath(guest);if(host.empty())return 0;}
#if defined(__linux__)
    struct statvfs space;
    if(statvfs(host.c_str(),&space)){failWith(errno,host);return 0;}
    if(available)*available=uint64_t(space.f_bavail)*space.f_frsize;
    if(total)*total=uint64_t(space.f_blocks)*space.f_frsize;
    if(free)*free=uint64_t(space.f_bfree)*space.f_frsize;
#else
    // Not statvfs on the console (title 1.09): the first call, when the game
    // saved from its menu, wrote past the structure the headers declare and the
    // host returned into the middle of a game instruction. The engine only
    // checks its save quota against this, so a fixed answer serves.
    struct stat info;
    if(stat(host.c_str(),&info)){failWith(errno,host);return 0;}
    static std::atomic<bool> said{false};
    if(!said.exchange(true))log("GetDiskFreeSpaceExW: espaco fixo de 256 GiB livres de 1 TiB ("+host+")");
    constexpr uint64_t Free=256ull<<30,Total=1ull<<40;
    if(available)*available=Free;
    if(total)*total=Total;
    if(free)*free=Free;
#endif
    return 1;
}
extern "C" int32_t win_AreFileApisANSI(void){return 1;}

// ---- For the DLLs the host loads (vkd3d-proton): ANSI forms -------------
extern "C" void* win_CreateFileA(const char* name,uint32_t access,uint32_t share,void* security,uint32_t disposition,uint32_t flags,void* templateFile) {
    auto wide=utf16(name?name:"");
    return win_CreateFileW(reinterpret_cast<const uint16_t*>(wide.c_str()),access,share,security,disposition,flags,templateFile);
}
extern "C" int32_t win_DeleteFileA(const char* name) {
    auto wide=utf16(name?name:"");
    return win_DeleteFileW(reinterpret_cast<const uint16_t*>(wide.c_str()));
}
extern "C" int32_t win_MoveFileA(const char* from,const char* to) {
    auto a=utf16(from?from:""),b=utf16(to?to:"");
    return win_MoveFileExW(reinterpret_cast<const uint16_t*>(a.c_str()),reinterpret_cast<const uint16_t*>(b.c_str()),0);
}
// The replacement takes the replaced file's place; no backup is kept.
extern "C" int32_t win_ReplaceFileA(const char* replaced,const char* replacement,const char* backup,uint32_t,void*,void*) {
    constexpr uint32_t ReplaceExisting=0x1;
    if(backup)log(std::string("ReplaceFileA ignora o backup: ")+backup);
    auto a=utf16(replacement?replacement:""),b=utf16(replaced?replaced:"");
    return win_MoveFileExW(reinterpret_cast<const uint16_t*>(a.c_str()),reinterpret_cast<const uint16_t*>(b.c_str()),ReplaceExisting);
}
extern "C" uint32_t win_GetFileSize(void* file,uint32_t* high) {
    int64_t size=0;
    if(!win_GetFileSizeEx(file,&size))return 0xffffffff;
    if(high)*high=uint32_t(uint64_t(size)>>32);
    setLastError(ErrorSuccess);
    return uint32_t(size);
}
// File mappings, read-only: a view is a copy of the file's bytes in host
// memory (vkd3d-proton maps its pipeline cache this way; without it the cache
// written by one run was never read by the next, title 0.53). Writable or
// named mappings are refused as Windows refuses an unsupported request.
namespace {
constexpr uint32_t PageReadOnly=0x02,PageWriteCopy=0x08,PageExecuteRead=0x20;
constexpr uint32_t FileMapWrite=0x2,FileMapCopy=0x1;
// The mapping keeps the file open, as on Windows, after the caller closes its handle.
struct Mapping:Object {std::shared_ptr<File> file;uint64_t size=0;};
std::mutex viewsLock;
std::map<const void*,size_t> views;
void* createMapping(void* handle,uint32_t protect,uint32_t sizeHigh,uint32_t sizeLow,const std::string& name) {
    // No file: shared memory (the engine's ESRAM).
    if(reinterpret_cast<uintptr_t>(handle)==InvalidHandle) {
        if(!name.empty())log("CreateFileMapping de memoria com nome ignora o nome: "+name);
        return createSection((uint64_t(sizeHigh)<<32)|sizeLow,protect);
    }
    auto file=findHandleOf<File>(handle);
    uint32_t access=protect&0xff;
    if(!file||!name.empty()||(access!=PageReadOnly&&access!=PageWriteCopy&&access!=PageExecuteRead)) {
        log("CreateFileMapping nao suportado: protecao "+std::to_string(protect)+(file?"":", sem arquivo")+(name.empty()?"":", nome "+name));
        setLastError(ErrorNotSupported);return nullptr;
    }
    struct stat info;
    if(Descriptor d(*file);d.fd<0||fstat(d.fd,&info)!=0){log("CreateFileMapping: fstat de "+file->host+" falhou, errno "+std::to_string(errno));setLastError(errorFromErrno(errno));return nullptr;}
    uint64_t size=(uint64_t(sizeHigh)<<32)|sizeLow;
    if(!size)size=uint64_t(info.st_size);
    if(!size){log("CreateFileMapping: arquivo vazio "+file->host);setLastError(1006);return nullptr;}   // ERROR_FILE_INVALID
    auto mapping=std::make_shared<Mapping>();
    mapping->file=file;mapping->size=size;
    return addHandle(mapping);
}
}
extern "C" void* win_CreateFileMappingA(void* file,void*,uint32_t protect,uint32_t sizeHigh,uint32_t sizeLow,const char* name) {
    return createMapping(file,protect,sizeHigh,sizeLow,name?name:"");
}
extern "C" void* win_CreateFileMappingW(void* file,void*,uint32_t protect,uint32_t sizeHigh,uint32_t sizeLow,const uint16_t* name) {
    return createMapping(file,protect,sizeHigh,sizeLow,name?utf8(name,length(name)):"");
}
extern "C" void* win_MapViewOfFile(void* handle,uint32_t access,uint32_t offsetHigh,uint32_t offsetLow,uint64_t size) {
    auto mapping=findHandleOf<Mapping>(handle);
    if(!mapping) {
        if(auto view=mapSectionView(handle,(uint64_t(offsetHigh)<<32)|offsetLow,size,access))return view;
        if(!findHandle(handle)){log("MapViewOfFile: handle de mapeamento invalido");setLastError(ErrorInvalidHandle);}
        return nullptr;
    }
    if(access&FileMapWrite&&!(access&FileMapCopy)){log("MapViewOfFile para escrita nao suportado: "+mapping->file->host);setLastError(ErrorAccessDenied);return nullptr;}
    uint64_t offset=(uint64_t(offsetHigh)<<32)|offsetLow;
    if(offset>mapping->size||(size&&size>mapping->size-offset)) {
        log("MapViewOfFile: faixa fora do mapeamento de "+mapping->file->host);setLastError(ErrorInvalidParameter);return nullptr;
    }
    if(!size)size=mapping->size-offset;
    auto view=static_cast<uint8_t*>(azimuth::heapAlloc(size_t(size),4096,true));
    if(!view){log("MapViewOfFile: sem memoria para "+std::to_string(size)+" bytes");setLastError(ErrorNotEnoughMemory);return nullptr;}
    for(uint64_t done=0;done<size;) {
        Descriptor d(*mapping->file);
        ssize_t got=d.fd<0?-1:pread(d.fd,view+done,size_t(size-done),off_t(offset+done));
        if(got<0&&errno==EINTR)continue;
        if(got<0)log("MapViewOfFile: leitura de "+mapping->file->host+" falhou, errno "+std::to_string(errno));
        if(got<=0)break;   // past the end of the file: the rest stays zero, as Windows maps it
        done+=uint64_t(got);
    }
    std::lock_guard<std::mutex> guard(viewsLock);views[view]=size_t(size);
    return view;
}
extern "C" int32_t win_UnmapViewOfFile(const void* view) {
    {
        std::lock_guard<std::mutex> guard(viewsLock);
        auto found=views.find(view);
        if(found==views.end()) {
            if(unmapSectionView(view,false))return 1;
            setLastError(ErrorInvalidParameter);return 0;
        }
        views.erase(found);
    }
    azimuth::heapFree(const_cast<void*>(view));return 1;
}
extern "C" int32_t win_DeviceIoControl(void*,uint32_t code,void*,uint32_t,void*,uint32_t,uint32_t* returned,void*) {
    log("DeviceIoControl "+std::to_string(code)+" nao suportado");
    if(returned)*returned=0;
    setLastError(ErrorNotSupported);return 0;
}
// File times are the host file system's own: a request to change them is accepted.
extern "C" int32_t win_SetFileTime(void* handle,const int64_t*,const int64_t*,const int64_t*) {
    if(!findHandleOf<File>(handle)){setLastError(ErrorInvalidHandle);return 0;}
    return 1;
}

// For the module loader (Modules.cpp): the EMFILE report.
namespace azimuth::win32 {void reportOpenDescriptors(){reportDescriptors();}}
