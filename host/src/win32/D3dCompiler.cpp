// d3dcompiler_47's D3DCompile without an HLSL compiler: the engine compiles a
// few fixed shaders of its own (title 0.90: the texture detile compute shader,
// a full-screen vertex shader and three pixel shaders). They were compiled on
// Windows with d3dcompiler_47.dll (precompile.py) into CompiledShaders.inc and
// are found by the SHA-256 of what the call passes; any other source is
// reported and fails. A missed request is also written whole to the report
// folder, shaders/<key>.hlsl and .txt (entry, target, flags), for
// precompile_shaders.py --dumps to compile (title 1.47: the 1.0.2 engine builds
// most of its HLSL at run time).
#include "Win32.h"
#include "../Sha256.h"
#include "../Platform.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "CompiledShaders.inc"

extern "C" void* guestConstantBlob(unsigned index,const void* data,uint64_t size);

using azimuth::win32::log;

extern "C" int32_t win_D3DCompile(const void* source,uint64_t size,const char* name,const void* defines,void* include,
                                  const char* entry,const char* target,uint32_t flags1,uint32_t flags2,void** code,void** errors) {
    constexpr int32_t Failed=int32_t(0x80004005),InvalidArgument=int32_t(0x80070057);
    if(code)*code=nullptr;
    if(errors)*errors=nullptr;
    if(!source||!code||!entry||!target)return InvalidArgument;
    // The key: source bytes, entry, target (each NUL-ended) and both flag words.
    std::vector<uint8_t> key(static_cast<const uint8_t*>(source),static_cast<const uint8_t*>(source)+size);
    key.push_back(0);key.insert(key.end(),entry,entry+std::strlen(entry)+1);key.insert(key.end(),target,target+std::strlen(target)+1);
    uint32_t flags[2]={flags1,flags2};
    key.insert(key.end(),reinterpret_cast<uint8_t*>(flags),reinterpret_cast<uint8_t*>(flags)+8);
    auto hash=azimuth::sha256(key.data(),key.size());
    bool plain=!defines&&!include;
    for(unsigned n=0;plain&&n<sizeof compiledShaders/sizeof compiledShaders[0];++n) {
        if(std::memcmp(hash.data(),compiledShaders[n].key,32))continue;
        *code=guestConstantBlob(n,compiledShaders[n].code,compiledShaders[n].size);
        return 0;
    }
    static std::atomic<unsigned> dumps{0};
    if(plain&&dumps++<64) {
        auto folder=azimuth::platform::reportDir()+"/shaders";
        ::mkdir(folder.c_str(),0777);
        char hex[65];for(int n=0;n<32;++n)std::snprintf(hex+2*n,3,"%02x",hash[n]);
        auto base=folder+"/"+hex;
        if(auto f=std::fopen((base+".hlsl").c_str(),"wb")){std::fwrite(source,1,size_t(size),f);std::fclose(f);}
        if(auto f=std::fopen((base+".txt").c_str(),"w")){std::fprintf(f,"%s\n%s\n%u\n%u\n%s\n",entry,target,flags1,flags2,name?name:"");std::fclose(f);}
    }
    static std::atomic<unsigned> reports{0};
    if(reports++<20) {
        std::string text(static_cast<const char*>(source),size_t(std::min<uint64_t>(size,160)));
        for(auto& c:text)if(c=='\n'||c=='\r')c=' ';
        log("D3DCompile sem shader pre-compilado: "+std::string(entry)+" "+target+", "+std::to_string(size)+" bytes"+
            (name?std::string(", nome ")+name:"")+(plain?"":", com defines/include")+": "+text);
    }
    // A pixel shader with no entry gets the placeholder, which draws black, so
    // the game goes on and every missing shader is dumped in one run (title 1.48).
    if(plain&&!std::strncmp(target,"ps_",3)) {
        for(unsigned n=0;n<sizeof compiledShaders/sizeof compiledShaders[0];++n) {
            if(std::strcmp(compiledShaders[n].name,"placeholder ps_5_0"))continue;
            static std::atomic<unsigned> placeholders{0};
            if(placeholders++<20)log("D3DCompile: shader provisorio (preto) no lugar de "+std::string(entry)+" "+target+"; compile os despejos em shaders/");
            *code=guestConstantBlob(n,compiledShaders[n].code,compiledShaders[n].size);
            return 0;
        }
    }
    return Failed;
}
