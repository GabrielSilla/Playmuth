#include "Modules.h"
#include "Image.h"
#include "Platform.h"
#include "Sha256.h"
#include "guest_abi.h"
#include "win32/Win32.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace azimuth {
namespace {
using win32::log;
constexpr uint32_t DllProcessAttach=1,DllThreadAttach=2,DllThreadDetach=3;
// The DLLs the host loads from files; every other library is the host's own.
// azimuth-tls is the engine's own: a DLL with static TLS and a TLS callback,
// which the engine loads from its data\bin when the game starts to check that
// the loader gives modules static TLS as Windows does.
// msvcr110 and msvcp110 (Visual C++ 2012's runtime, also in data\bin) are
// what the engine calls its desktop dependencies, loaded as the game starts.
const char* const Loadable[]={"d3d12","d3d12core","azimuth-tls","msvcr110","msvcp110",
                              "azimuth-tls-1","azimuth-tls-2","vcruntime140","vcruntime140_1","msvcp140","concrt140",
                              "sehtest",   // the exception dispatch check (tests/seh), only where it was copied
                              "avutil-60","avcodec-62"};   // FFmpeg, which the engine loads for SHAPE's audio (title 1.03)

std::vector<std::string> searchDirectories() {
    std::vector<std::string> list;
#if defined(__linux__)
    // AZIMUTH_DLL_DIR: directories separated by ':' (the Linux checks).
    if(const char* value=std::getenv("AZIMUTH_DLL_DIR")) {
        std::string all=value;size_t at=0;
        while(at<=all.size()){auto end=all.find(':',at);if(end==std::string::npos)end=all.size();if(end>at)list.push_back(all.substr(at,end-at));at=end+1;}
    }
    list.push_back("dll");
#else
    // vkd3d-proton ships in the title's own folder (dll/, title 1.27), then
    // the project's folder in /data; never another program's install.
    list.push_back("/app0/dll");
    list.push_back("/system_ex/app/FAKE78002/dll");
    list.push_back("/data/homebrew/FAKE78002/dll");
    list.push_back("/data/azimuth-ps5/dll");
#endif
    // Then the engine's own DLL folder, data\bin next to azimuth.exe.
    for(auto const& m:win32::process().mounts)if(m.guest==u"E:\\PC\\FH2")list.push_back(m.host+"/data/bin");
    return list;
}

struct Module {
    std::string name,path;
    std::unique_ptr<PeImage> parsed;
    std::unique_ptr<Image> image;
    uint32_t firstSlot=0,tlsIndex=0;
    std::vector<void*> tlsCallbacks;
    uint32_t exportStart=0,exportEnd=0,ordinalBase=0;
    std::vector<uint32_t> functions;
    std::unordered_map<std::string,uint32_t> names;   // name -> index into functions
    bool initialized=false;
};
// The loader lock: held while a module loads and while DllMain and the TLS
// callbacks run, as on Windows (a DllMain may load another module).
std::recursive_mutex loaderLock;
std::vector<std::unique_ptr<Module>> modules;
std::unordered_map<ThreadBlock*,std::vector<std::unique_ptr<uint8_t[]>>> threadBlocks;
uint32_t nextSlot=ModuleImportSlots;
uint32_t nextTlsIndex=1;

std::string hex(uint64_t value){char text[24];std::snprintf(text,sizeof text,"0x%llx",(unsigned long long)value);return text;}
std::string baseName(std::string name) {
    for(auto& c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    auto slash=name.find_last_of("\\/");if(slash!=std::string::npos)name=name.substr(slash+1);
    if(name.size()>4&&name.compare(name.size()-4,4,".dll")==0)name.resize(name.size()-4);
    return name;
}
// The host library name an import refers to, as listed in exports.inc.
std::string hostLibrary(std::string name) {
    name=baseName(name);
    if(name.rfind("api-ms-win-core-",0)==0)name="kernel32";
    if(name.rfind("api-ms-win-crt-",0)==0)name="msvcrt";   // the Universal CRT (FFmpeg's DLLs)
    for(auto& c:name)if(c=='-')c='_';
    return name;
}
const void* hostExport(const std::string& library,const char* symbol) {
    for(auto e=guestExports;e->name;++e)
        if(hostLibrary(e->library)==library&&!std::strcmp(e->name,symbol))return e->address;
    return nullptr;
}
bool loadable(const std::string& name) {
    for(auto l:Loadable)if(name==l)return true;
    return false;
}
Module* byName(const std::string& name) {
    for(auto& m:modules)if(m->name==name)return m.get();
    return nullptr;
}
Module* containing(uint64_t address) {
    for(auto& m:modules)if(m->image&&m->image->contains(address))return m.get();
    return nullptr;
}
uint32_t read32(const uint8_t* at){uint32_t value;std::memcpy(&value,at,4);return value;}
void parseExports(Module& m) {
    auto d=m.parsed->directories[0];
    if(!d.size)return;
    auto size=m.image->size();
    if(d.rva>size||d.size>size-d.rva||d.size<40)throw std::runtime_error("Diretorio de exports fora da imagem");
    auto base=reinterpret_cast<const uint8_t*>(m.image->base());
    auto dir=base+d.rva;
    m.exportStart=d.rva;m.exportEnd=d.rva+d.size;
    m.ordinalBase=read32(dir+16);
    uint32_t count=read32(dir+20),named=read32(dir+24),functions=read32(dir+28),names=read32(dir+32),ordinals=read32(dir+36);
    auto inside=[&](uint32_t rva,uint64_t length){return rva<=size&&length<=size-rva;};
    if(count>65536||named>count||!inside(functions,uint64_t(count)*4)||!inside(names,uint64_t(named)*4)||!inside(ordinals,uint64_t(named)*2))
        throw std::runtime_error("Tabela de exports invalida");
    for(uint32_t n=0;n<count;++n)m.functions.push_back(read32(base+functions+n*4));
    for(uint32_t n=0;n<named;++n) {
        uint32_t text=read32(base+names+n*4);uint16_t index;std::memcpy(&index,base+ordinals+n*2,2);
        if(!inside(text,1)||index>=count)throw std::runtime_error("Nome de export invalido");
        m.names.emplace(reinterpret_cast<const char*>(base+text),index);
    }
}
void* exportOf(Module& m,const char* name,int depth);
// A forwarder "library.symbol" (or "library.#ordinal").
void* forwarded(const std::string& target,int depth) {
    auto dot=target.find('.');
    if(dot==std::string::npos||depth>4)return nullptr;
    auto library=target.substr(0,dot),symbol=target.substr(dot+1);
    if(auto address=hostExport(hostLibrary(library),symbol.c_str()))return const_cast<void*>(address);
    auto name=baseName(library);
    if(!loadable(name))return nullptr;
    if(!byName(name)&&!loadModule(name))return nullptr;
    auto other=byName(name);
    if(symbol.size()>1&&symbol[0]=='#')
        return exportOf(*other,reinterpret_cast<const char*>(uintptr_t(std::strtoul(symbol.c_str()+1,nullptr,10))),depth+1);
    return exportOf(*other,symbol.c_str(),depth+1);
}
void* exportOf(Module& m,const char* name,int depth) {
    uint32_t index;
    if(reinterpret_cast<uintptr_t>(name)<0x10000) {
        auto ordinal=uint32_t(reinterpret_cast<uintptr_t>(name));
        if(ordinal<m.ordinalBase)return nullptr;
        index=ordinal-m.ordinalBase;
    }else {
        auto found=m.names.find(name);
        if(found==m.names.end())return nullptr;
        index=found->second;
    }
    if(index>=m.functions.size()||!m.functions[index])return nullptr;
    auto rva=m.functions[index];
    auto base=m.image->base();
    if(rva>=m.exportStart&&rva<m.exportEnd)return forwarded(reinterpret_cast<const char*>(base+rva),depth);
    return reinterpret_cast<void*>(base+rva);
}
// open/read, not fopen: fopen takes its FILE from the title's small malloc,
// which the game has filled by the time the engine loads FFmpeg (title 1.03).
Bytes readFile(const std::string& path) {
    int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);
    if(fd<0){int error=errno;log("Abrir "+path+" falhou (errno "+std::to_string(error)+")");if(error==EMFILE)win32::reportOpenDescriptors();return {};}
    struct stat st;
    if(fstat(fd,&st)||st.st_size<0){close(fd);return {};}
    Bytes bytes(size_t(st.st_size));
    size_t got=0;
    while(got<bytes.size()) {
        auto n=read(fd,bytes.data()+got,bytes.size()-got);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)break;
        got+=size_t(n);
    }
    close(fd);
    if(got!=bytes.size()){log("Leitura de "+path+" incompleta: "+std::to_string(got)+" de "+std::to_string(bytes.size())+" bytes");return {};}
    return bytes;
}
std::string sha256Hex(const Bytes& bytes) {
    auto digest=sha256(bytes.data(),bytes.size());
    std::string text;char pair[3];
    for(auto b:digest){std::snprintf(pair,sizeof pair,"%02x",b);text+=pair;}
    return text;
}
// A new TLS block for `module` on `block`'s thread, with the template copied.
void giveTlsBlock(ThreadBlock& block,Module& m) {
    auto size=m.image->staticTlsSize();
    std::unique_ptr<uint8_t[]> memory(new uint8_t[size?size:16]());
    if(size)m.image->initializeStaticTls(memory.get(),size);
    block.setTlsSlot(m.tlsIndex,memory.get());
    threadBlocks[&block].push_back(std::move(memory));
}
// Which pages of a mapped module the kernel lets the host read and write,
// found without faulting: write(2) from a page fails with EFAULT when it is
// not readable, and read(2) into a page when it is not writable (the byte is
// put back). Logged per section, to compare with the protection requested.
void probePages(Module& m) {
    int zero=::open("/dev/zero",O_RDONLY),null=::open("/dev/null",O_WRONLY);
    if(zero<0||null<0){if(zero>=0)::close(zero);if(null>=0)::close(null);return;}
    for(auto const& s:m.parsed->sections) {
        auto extent=std::max(s.virtualSize,s.fileSize);
        std::string pages;
        for(uint64_t at=s.rva&~uint64_t(4095);at<uint64_t(s.rva)+extent;at+=4096) {
            auto page=reinterpret_cast<uint8_t*>(m.image->base()+at);
            bool readable=::write(null,page,1)==1;
            bool writable=false;
            if(readable) {
                uint8_t saved=*page;
                writable=::read(zero,page,1)==1;
                if(writable)*page=saved;
            }
            pages+=readable?(writable?"RW ":"R- "):"-- ";
        }
        log(m.name+".dll "+s.name+" rva "+hex(s.rva)+" ("+std::to_string(extent)+" bytes), caracteristicas "+hex(s.characteristics)+
            ", paginas legiveis/gravaveis pelo kernel: "+pages);
    }
    ::close(zero);::close(null);
}
void notifyModule(Module& m,uint32_t reason) {
    for(auto callback:m.tlsCallbacks)guestCallTlsCallback(callback,m.image->base(),reason);
    if(m.parsed->entryPoint)guestCallDllMain(m.image->entryAddress(),m.image->base(),reason,nullptr);
}
}

void* buildImportStubs(uint32_t firstSlot,size_t count) {
    size_t size=(count*ImportStubSize+4095)/4096*4096;
    if(!size)return nullptr;
    auto stubs=static_cast<uint8_t*>(platform::reserve(0,size));
    auto handler=reinterpret_cast<uint64_t>(guestMissingImportHandler());
    for(uint32_t n=0;n<count;++n) {
        uint32_t slot=firstSlot+n;
        auto p=stubs+n*ImportStubSize;std::memset(p,0xcc,ImportStubSize);
        p[0]=0xb9;std::memcpy(p+1,&slot,4);p[5]=0x48;p[6]=0xb8;std::memcpy(p+7,&handler,8);p[15]=0xff;p[16]=0xe0;
    }
    platform::protect(stubs,size,platform::Read|platform::Execute);
    return stubs;
}

// The loader lock covers the module list and mapping, never the module's own
// code: a DLL's TLS callbacks and DllMain run without it, because a diagnostic
// stop inside them unwinds the thread with longjmp and would leave it held.
uint64_t loadModule(const std::string& wanted) {
    auto name=baseName(wanted);
    Module* loaded=nullptr;
    {
        std::lock_guard<std::recursive_mutex> guard(loaderLock);
        if(auto m=byName(name))return m->initialized?m->image->base():0;
        if(!loadable(name))return 0;
        std::string path;struct stat st;
        for(auto const& directory:searchDirectories()) {
            auto candidate=directory+"/"+name+".dll";
            if(stat(candidate.c_str(),&st)==0){path=candidate;break;}
        }
        if(path.empty()){log("Modulo "+name+".dll nao encontrado nas pastas de DLL do host");return 0;}
        auto module=std::make_unique<Module>();
        auto& m=*module;
        m.name=name;m.path=path;
        try {
            struct stat info;
            if(stat(path.c_str(),&info)==0)log("Lendo "+path+" ("+std::to_string(uint64_t(info.st_size))+" bytes)");
            auto bytes=readFile(path);
            if(bytes.empty())throw std::runtime_error("arquivo vazio ou ilegivel");
            log("Carregando "+name+".dll: "+path+" ("+std::to_string(bytes.size())+" bytes, SHA-256 "+sha256Hex(bytes)+")");
            m.parsed=std::make_unique<PeImage>(std::move(bytes));
            m.firstSlot=nextSlot;nextSlot+=uint32_t(m.parsed->imports.size());
            auto stubs=static_cast<uint8_t*>(buildImportStubs(m.firstSlot,m.parsed->imports.size()));
            if(m.parsed->directories[9].size)m.tlsIndex=nextTlsIndex++;
            if(m.tlsIndex>=ThreadBlock::TlsSlots)throw std::runtime_error("Indices de TLS esgotados");
            // A DLL the host loads that this one imports from comes first
            // (avcodec-62 imports from avutil-60, title 1.03).
            for(auto const& item:m.parsed->imports) {
                auto dependency=baseName(item.library);
                if(dependency!=name&&loadable(dependency)&&!byName(dependency))loadModule(dependency);
            }
            unsigned bound=0;std::string unbound;
            // Imports from the host's libraries, or from a DLL already loaded
            // (vkd3d-proton's DLLs load each other with LoadLibrary instead).
            m.image=std::make_unique<Image>(*m.parsed,[&](const Import& item,unsigned slot)->void* {
                auto library=hostLibrary(item.library);
                if(!item.byOrdinal) {
                    if(auto address=hostExport(library,item.symbol.c_str())){++bound;return const_cast<void*>(address);}
                    if(auto other=byName(baseName(item.library)))
                        if(auto address=exportOf(*other,item.symbol.c_str(),0)){++bound;return address;}
                }
                if(unbound.size()<4000)unbound+=" "+item.library+"!"+(item.byOrdinal?"#"+std::to_string(item.ordinal):item.symbol);
                return stubs+size_t(slot)*ImportStubSize;
            },m.tlsIndex,true,true);
            parseExports(m);
            if(!m.image->mixedBlocks().empty()) {
                std::string list;
                for(auto rva:m.image->mixedBlocks())list+=" "+hex(rva);
                log(name+".dll: blocos de "+std::to_string(platform::protectionGranularity())+" bytes com secoes de protecoes diferentes recebem a uniao delas:"+list+
                    "; gravaveis e executaveis ao mesmo tempo: "+std::to_string(m.image->writableExecutableBlocks()));
            }
            probePages(m);
            if(m.tlsIndex)m.tlsCallbacks=m.image->tlsCallbacks();
            log(name+".dll mapeado em "+hex(m.image->base())+(m.image->base()==m.parsed->imageBase?" (base preferida)":" (relocado)")+
                ": "+std::to_string(bound)+" de "+std::to_string(m.parsed->imports.size())+" imports no host, "+
                std::to_string(m.names.size())+" exports"+(m.tlsIndex?", TLS indice "+std::to_string(m.tlsIndex):""));
            if(!unbound.empty())log(name+".dll: imports sem implementacao (paradas de diagnostico se chamados):"+unbound);
        }catch(std::exception const& e) {
            log("Modulo "+name+".dll nao carregado: "+e.what());
            return 0;
        }
        modules.push_back(std::move(module));
        // Every running engine thread gets its TLS block, as Windows does
        // for a DLL loaded at run time. A block whose TLS array is no longer
        // mapped is skipped and named (title 1.46: the 1.0.2 engine's
        // d3d12core load faulted writing one).
        if(m.tlsIndex) {
            int null=::open("/dev/null",O_WRONLY);
            for(auto& t:threadBlocks) {
                if(null>=0&&::write(null,t.first->slots()+m.tlsIndex,sizeof(void*))!=ssize_t(sizeof(void*))) {
                    log(name+".dll: TLS da thread "+std::to_string(t.first->id())+" nao recebido, a tabela de TLS dela em "+
                        hex(reinterpret_cast<uint64_t>(t.first->slots()))+" nao esta mapeada (errno "+std::to_string(errno)+")");
                    continue;
                }
                giveTlsBlock(*t.first,m);
            }
            if(null>=0)::close(null);
        }
        loaded=&m;
    }
    // TLS callbacks and DllMain, on the loading thread.
    auto& m=*loaded;
    for(auto callback:m.tlsCallbacks)guestCallTlsCallback(callback,m.image->base(),DllProcessAttach);
    if(m.parsed->entryPoint&&!guestCallDllMain(m.image->entryAddress(),m.image->base(),DllProcessAttach,nullptr)) {
        log(name+".dll: DllMain(DLL_PROCESS_ATTACH) retornou FALSE");
        return 0;
    }
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    m.initialized=true;
    log(name+".dll inicializado (callbacks TLS e DllMain)");
    return m.image->base();
}
uint64_t moduleContaining(uint64_t address) {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    auto m=containing(address);
    return m?m->image->base():0;
}
uint64_t findModule(const std::string& name) {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    auto m=byName(baseName(name));
    return m&&m->initialized?m->image->base():0;
}
std::vector<uint64_t> moduleBases() {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    std::vector<uint64_t> bases;
    for(auto& m:modules)if(m->initialized)bases.push_back(m->image->base());
    return bases;
}
std::string moduleWindowsPath(uint64_t base) {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    auto m=containing(base);
    if(!m)return std::string();
    // A DLL from a folder the engine sees (data\bin on E:) is named by its real
    // path: the engine hashes its dependencies by that name (title 0.59).
    for(auto const& mount:win32::process().mounts) {
        auto const& host=mount.host;
        if(m->path.size()>host.size()&&m->path.compare(0,host.size(),host)==0&&m->path[host.size()]=='/') {
            auto rest=m->path.substr(host.size());
            for(auto& c:rest)if(c=='/')c='\\';
            return win32::utf8(reinterpret_cast<const uint16_t*>(mount.guest.data()),mount.guest.size())+rest;
        }
    }
    return "C:\\windows\\system32\\"+m->name+".dll";
}
void* moduleExport(uint64_t base,const char* name) {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    auto m=containing(base);
    return m?exportOf(*m,name,0):nullptr;
}
std::string describeModuleAddress(uint64_t address) {
    std::unique_lock<std::recursive_mutex> guard(loaderLock,std::try_to_lock);
    if(!guard.owns_lock())return {};
    auto m=containing(address);
    return m?m->name+"+"+hex(address-m->image->base()):std::string();
}
std::string moduleImportName(uint32_t slot) {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    for(auto& m:modules) {
        if(slot<m->firstSlot||slot-m->firstSlot>=m->parsed->imports.size())continue;
        auto const& item=m->parsed->imports[slot-m->firstSlot];
        return m->name+".dll -> "+item.library+"!"+(item.byOrdinal?"#"+std::to_string(item.ordinal):item.symbol);
    }
    return "#"+std::to_string(slot);
}
// The notifications run without the loader lock, like loadModule's.
void attachThreadToModules(ThreadBlock& block,bool notify) {
    std::vector<Module*> initialized;
    {
        std::lock_guard<std::recursive_mutex> guard(loaderLock);
        threadBlocks[&block];
        for(auto& m:modules)if(m->tlsIndex)giveTlsBlock(block,*m);
        for(auto& m:modules)if(m->initialized)initialized.push_back(m.get());
    }
    if(notify)for(auto m:initialized)notifyModule(*m,DllThreadAttach);
}
void detachThreadFromModules(ThreadBlock& block,bool notify) {
    std::vector<Module*> initialized;
    {
        std::lock_guard<std::recursive_mutex> guard(loaderLock);
        for(auto& m:modules)if(m->initialized)initialized.push_back(m.get());
    }
    if(notify)for(auto m=initialized.rbegin();m!=initialized.rend();++m)notifyModule(**m,DllThreadDetach);
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    threadBlocks.erase(&block);
}
void forgetThreadBlock(ThreadBlock& block) {
    std::lock_guard<std::recursive_mutex> guard(loaderLock);
    threadBlocks.erase(&block);
}
}
