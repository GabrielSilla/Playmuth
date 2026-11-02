// GetPrivateProfileStringW / WritePrivateProfileStringW over the engine's own
// INI files: AzimuthUwp/src/IniProfile.cpp (same parsing, encodings and
// return values) on POSIX file I/O and UTF-16 strings.
#include "Win32.h"
#include "../guest_abi.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
constexpr uint32_t ErrorFileTooLarge=223;
enum class Encoding {Ansi,Utf16,Utf8};
struct Document {std::vector<std::u16string> lines;Encoding encoding=Encoding::Ansi;uint32_t error=0;};
std::mutex profileMutex;
std::atomic<unsigned> serial{0};
using View=std::u16string_view;
bool equal(View a,View b) {
    return win_CompareStringOrdinal(reinterpret_cast<const uint16_t*>(a.data()),int32_t(a.size()),
                                    reinterpret_cast<const uint16_t*>(b.data()),int32_t(b.size()),1)==2;
}
std::u16string trim(View in) {
    auto first=in.find_first_not_of(u" \t\r\n");if(first==in.npos)return {};
    return std::u16string(in.substr(first,in.find_last_not_of(u" \t\r\n")-first+1));
}
bool heading(View line,std::u16string& name) {
    auto s=trim(line);if(s.size()<2||s.front()!=u'['||s.back()!=u']')return false;
    name=trim(View(s).substr(1,s.size()-2));return true;
}
bool entry(View line,std::u16string& key,std::u16string& value) {
    auto s=trim(line);if(s.empty()||s[0]==u';'||s[0]==u'#')return false;
    auto at=s.find(u'=');if(at==s.npos)return false;
    key=trim(View(s).substr(0,at));value=trim(View(s).substr(at+1));return !key.empty();
}
Document load(const std::string& path) {
    Document result;
    int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);
    if(fd<0){result.error=errno==ENOENT?ErrorFileNotFound:errorFromErrno(errno);return result;}
    struct stat info;
    if(fstat(fd,&info)){result.error=errorFromErrno(errno);close(fd);return result;}
    if(info.st_size<0||info.st_size>4*1024*1024){result.error=ErrorFileTooLarge;close(fd);return result;}
    std::vector<unsigned char> bytes(size_t(info.st_size));size_t done=0;
    while(done<bytes.size()) {
        auto n=read(fd,bytes.data()+done,bytes.size()-done);
        if(n<0){result.error=errorFromErrno(errno);close(fd);return result;}
        if(!n){result.error=ErrorHandleEof;close(fd);return result;}
        done+=size_t(n);
    }
    close(fd);
    std::u16string text;
    if(bytes.size()>=2&&bytes[0]==0xff&&bytes[1]==0xfe) {
        result.encoding=Encoding::Utf16;
        if((bytes.size()-2)%2){result.error=ErrorNoUnicodeTranslation;return result;}
        text.resize((bytes.size()-2)/2);std::memcpy(text.data(),bytes.data()+2,bytes.size()-2);
    }else if(!bytes.empty()) {
        size_t skip=0;uint32_t page=0,flags=0;   // CP_ACP
        if(bytes.size()>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf){skip=3;page=65001;flags=8;result.encoding=Encoding::Utf8;}
        if(bytes.size()>skip) {
            auto source=reinterpret_cast<const char*>(bytes.data()+skip);int32_t count=int32_t(bytes.size()-skip);
            int32_t n=win_MultiByteToWideChar(page,flags,source,count,nullptr,0);
            if(!n){result.error=win_GetLastError();return result;}
            text.resize(size_t(n));win_MultiByteToWideChar(page,flags,source,count,reinterpret_cast<uint16_t*>(text.data()),n);
        }
    }
    // An INI is text, not an arbitrary binary blob. Do not parse beyond embedded NULs.
    if(text.find(u'\0')!=text.npos){result.error=ErrorInvalidData;return result;}
    size_t at=0;
    while(at<text.size()) {
        auto end=text.find(u'\n',at);if(end==text.npos)end=text.size();
        auto line=text.substr(at,end-at);if(!line.empty()&&line.back()==u'\r')line.pop_back();
        result.lines.push_back(std::move(line));at=end+1;
    }
    return result;
}
uint32_t copyString(View value,uint16_t* output,uint32_t count) {
    if(!count||!output)return 0;
    auto n=std::min<size_t>(value.size(),count-1);std::copy_n(value.data(),n,reinterpret_cast<char16_t*>(output));output[n]=0;return uint32_t(n);
}
uint32_t copyList(const std::vector<std::u16string>& values,uint16_t* output,uint32_t count) {
    if(!count||!output)return 0;
    output[0]=0;if(count>1)output[1]=0;if(values.empty()||count<2)return 0;
    std::u16string packed;for(auto const& value:values){packed+=value;packed+=u'\0';}packed+=u'\0';
    auto out=reinterpret_cast<char16_t*>(output);
    if(packed.size()>count){std::copy_n(packed.data(),count-2,out);output[count-2]=0;output[count-1]=0;return count-2;}
    std::copy(packed.begin(),packed.end(),out);return uint32_t(packed.size()-1);
}
bool save(const std::string& file,const Document& doc,uint32_t& error) {
    std::u16string text;for(auto const& line:doc.lines){text+=line;text+=u"\r\n";}
    std::vector<unsigned char> bytes;
    if(doc.encoding==Encoding::Utf16) {
        bytes.resize(2+text.size()*2);bytes[0]=0xff;bytes[1]=0xfe;std::memcpy(bytes.data()+2,text.data(),text.size()*2);
    }else {
        uint32_t page=doc.encoding==Encoding::Utf8?65001:0;int32_t replaced=0;
        auto used=page==0?&replaced:nullptr;
        auto source=reinterpret_cast<const uint16_t*>(text.data());
        int32_t n=text.empty()?0:win_WideCharToMultiByte(page,0,source,int32_t(text.size()),nullptr,0,nullptr,used);
        if((!text.empty()&&!n)||replaced){error=ErrorNoUnicodeTranslation;return false;}
        size_t skip=doc.encoding==Encoding::Utf8?3:0;bytes.resize(skip+size_t(n));
        if(skip){bytes[0]=0xef;bytes[1]=0xbb;bytes[2]=0xbf;}
        if(n)win_WideCharToMultiByte(page,0,source,int32_t(text.size()),reinterpret_cast<char*>(bytes.data()+skip),n,nullptr,used);
    }
    // Write a new file next to the old one and rename it over: the INI is
    // either the old or the new content, never half of each.
    auto temp=file+".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
    int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0666);
    if(fd<0){error=errorFromErrno(errno);return false;}
    for(size_t at=0;at<bytes.size();) {
        auto n=write(fd,bytes.data()+at,bytes.size()-at);
        if(n<=0){error=n<0?errorFromErrno(errno):ErrorWriteFault;close(fd);unlink(temp.c_str());return false;}
        at+=size_t(n);
    }
    if(fsync(fd)){error=errorFromErrno(errno);close(fd);unlink(temp.c_str());return false;}
    close(fd);
    if(rename(temp.c_str(),file.c_str())){error=errorFromErrno(errno);unlink(temp.c_str());return false;}
    error=ErrorSuccess;return true;
}
// The host path of a profile the engine names; empty (error set) when it cannot be served.
std::string profilePath(const uint16_t* name) {
    if(!name||!*name){log("Perfil sem nome de arquivo (win.ini) ainda nao suportado");setLastError(ErrorInvalidParameter);return {};}
    return hostPath(fullPath(fromGuest(name)));
}
}

extern "C" uint32_t win_GetPrivateProfileStringW(const uint16_t* section,const uint16_t* key,const uint16_t* fallback,uint16_t* output,uint32_t count,
                                                 const uint16_t* fileName) {
    if(!count)return 0;
    if(!output){setLastError(ErrorInvalidParameter);return 0;}
    auto file=profilePath(fileName);if(file.empty()){output[0]=0;return 0;}
    std::lock_guard<std::mutex> lock(profileMutex);auto doc=load(file);uint32_t error=doc.error;
    std::vector<std::u16string> names;bool inSection=false,seen=false;std::u16string value;bool found=false;
    for(auto const& line:doc.lines) {
        std::u16string name;
        if(heading(line,name)) {
            if(!section){names.push_back(name);continue;}
            if(inSection)break;
            inSection=!seen&&equal(name,fromGuest(section));if(inSection)seen=true;
            continue;
        }
        if(!inSection)continue;
        std::u16string k,v;if(!entry(line,k,v))continue;
        if(!key)names.push_back(k);
        else if(equal(k,fromGuest(key))){value=std::move(v);found=true;break;}
    }
    uint32_t n=0;
    if(!section||!key)n=copyList(names,output,count);
    else {
        if(!found){value=fromGuest(fallback);while(!value.empty()&&value.back()==u' ')value.pop_back();}
        else if(value.size()>=2&&((value.front()==u'"'&&value.back()==u'"')||(value.front()==u'\''&&value.back()==u'\'')))value=value.substr(1,value.size()-2);
        n=copyString(value,output,count);
    }
    setLastError(error);return n;
}
extern "C" int32_t win_WritePrivateProfileStringW(const uint16_t* section,const uint16_t* key,const uint16_t* value,const uint16_t* fileName) {
    // No registry cache exists here. A cache-flush request has nothing to flush.
    if(!section&&!key&&!value){setLastError(ErrorSuccess);return 0;}
    if(!section||!*section){setLastError(ErrorInvalidParameter);return 0;}
    auto file=profilePath(fileName);if(file.empty())return 0;
    std::lock_guard<std::mutex> lock(profileMutex);auto doc=load(file);
    if(doc.error&&doc.error!=ErrorFileNotFound){setLastError(doc.error);return 0;}
    auto sectionName=fromGuest(section);
    size_t start=doc.lines.size(),end=start;std::u16string name;
    for(size_t n=0;n<doc.lines.size();++n)if(heading(doc.lines[n],name)&&equal(name,sectionName)){start=n;end=n+1;while(end<doc.lines.size()&&!heading(doc.lines[end],name))++end;break;}
    if(!key){if(start<doc.lines.size())doc.lines.erase(doc.lines.begin()+long(start),doc.lines.begin()+long(end));}
    else {
        auto keyName=fromGuest(key);bool found=false;
        for(size_t n=start+1;start<doc.lines.size()&&n<end;++n) {
            std::u16string k,v;if(entry(doc.lines[n],k,v)&&equal(k,keyName)) {
                if(value)doc.lines[n]=k+u"="+fromGuest(value);else doc.lines.erase(doc.lines.begin()+long(n));
                found=true;break;
            }
        }
        if(!found&&value) {
            if(start==doc.lines.size()){doc.lines.push_back(u"["+sectionName+u"]");end=doc.lines.size();}
            doc.lines.insert(doc.lines.begin()+long(end),keyName+u"="+fromGuest(value));
        }
    }
    uint32_t error=0;bool ok=save(file,doc,error);setLastError(error);return ok?1:0;
}
