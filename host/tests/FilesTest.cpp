// File handle check (title 1.06): more files open than the descriptor cache
// holds, each with its own position, read again after the cache closed its
// descriptor; written, extended, truncated and mapped through the same path.
#include "../src/Teb.h"
#include "../src/win32/Win32.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <vector>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT
namespace azimuth::win32 {
void log(const std::string& text){std::printf("log: %s\n",text.c_str());}
void stopGuest(const std::string& reason){std::printf("STOP: %s\n",reason.c_str());std::exit(2);}
}

namespace {
int failures=0;
void check(bool ok,const std::string& what){std::printf("%s %s\n",ok?"ok  ":"FALHOU",what.c_str());if(!ok)++failures;}
std::u16string guest(int i){std::string n="T:\f"+std::to_string(i)+".bin";return std::u16string(n.begin(),n.end());}
const uint16_t* w(const std::u16string& s){return reinterpret_cast<const uint16_t*>(s.c_str());}
}

int main() {
    azimuth::ProcessBlock processBlock(0);
    azimuth::ThreadBlock thread(processBlock,nullptr,nullptr,256);
    if(thread.enter()){std::fprintf(stderr,"gs nao configurado\n");return 1;}
    // Like the console: about 256 descriptors in all.
    rlimit limit{200,200};setrlimit(RLIMIT_NOFILE,&limit);
    system("rm -rf /tmp/azimuth-files-test && mkdir -p /tmp/azimuth-files-test");
    azimuth::win32::Process process;process.mounts.push_back({u"T:","/tmp/azimuth-files-test"});azimuth::win32::configure(process);
    constexpr int Count=300;
    std::vector<void*> handles;
    bool created=true;
    for(int i=0;i<Count;++i) {
        auto h=win_CreateFileW(w(guest(i)),0xC0000000,1,nullptr,2,0,nullptr);
        if(h==reinterpret_cast<void*>(-1)){created=false;break;}
        uint32_t done=0;uint32_t v[4]={uint32_t(i),uint32_t(i*7),uint32_t(i*13),0xabcd};
        created&=win_WriteFile(h,v,sizeof v,&done,nullptr)&&done==sizeof v;
        handles.push_back(h);
    }
    check(created&&handles.size()==Count,std::to_string(Count)+" arquivos criados e abertos com limite de 200 descritores");
    bool positions=true;
    for(int i=0;i<Count;++i) {
        int64_t at=-1;positions&=win_SetFilePointerEx(handles[i],4,&at,0)&&at==4;
    }
    for(int i=Count-1;i>=0;--i) {
        uint32_t v=0,done=0;positions&=win_ReadFile(handles[i],&v,4,&done,nullptr)&&done==4&&v==uint32_t(i*7);
        int64_t at=-1;positions&=win_SetFilePointerEx(handles[i],0,&at,1)&&at==8;
    }
    check(positions,"cada handle mantém a própria posição depois de reaberto");
    int64_t size=0;
    check(win_SetFilePointerEx(handles[5],-4,nullptr,2)&&win_SetEndOfFile(handles[5])&&win_GetFileSizeEx(handles[5],&size)&&size==12,"SetEndOfFile na posição do handle");
    auto again=win_CreateFileW(w(guest(9)),0x80000000,3,nullptr,3,0,nullptr);
    uint32_t v[4]={},done=0;
    check(again!=reinterpret_cast<void*>(-1)&&win_ReadFile(again,v,16,&done,nullptr)&&done==16&&v[3]==0xabcd,"arquivo reaberto sem truncar (CREATE_ALWAYS não se repete)");
    auto mapping=win_CreateFileMappingW(handles[200],nullptr,2,0,0,nullptr);
    auto view=mapping?static_cast<uint32_t*>(win_MapViewOfFile(mapping,4,0,0,0)):nullptr;
    check(view&&view[0]==200&&view[2]==200*13,"mapeamento lê pelo cache");
    // An I/O completion port (title 1.16): a read on a bound handle answers
    // ERROR_IO_PENDING and its completion arrives on the port with the key.
    {
        auto port=win_CreateIoCompletionPort(reinterpret_cast<void*>(-1),nullptr,0,0);
        auto file=win_CreateFileW(w(guest(7)),0x80000000,1,nullptr,3,0x40000000,nullptr);   // FILE_FLAG_OVERLAPPED
        bool bound=port&&win_CreateIoCompletionPort(file,port,0x77,0)==port;
        uint8_t overlapped[32]={};uint32_t offset=4;std::memcpy(overlapped+16,&offset,4);
        uint32_t value=0,done=0;
        bool pending=win_ReadFile(file,&value,4,&done,overlapped)==0&&win_GetLastError()==997;
        uint32_t bytes=0;uint64_t key=0;void* which=nullptr;
        bool packet=win_GetQueuedCompletionStatus(port,&bytes,&key,&which,1000)&&bytes==4&&key==0x77&&which==overlapped&&value==7*7;
        bool empty=!win_GetQueuedCompletionStatus(port,&bytes,&key,&which,0);
        // FILE_SKIP_COMPLETION_PORT_ON_SUCCESS: done at once, no packet.
        win_SetFileCompletionNotificationModes(file,1);
        bool skipped=win_ReadFile(file,&value,4,&done,overlapped)==1&&!win_GetQueuedCompletionStatus(port,&bytes,&key,&which,0);
        check(bound&&pending&&packet&&empty&&skipped,"porta de conclusao: leitura pendente, pacote com chave e tamanho, modo sem pacote");
        win_CloseHandle(file);win_CloseHandle(port);
    }
    for(auto h:handles)win_CloseHandle(h);
    win_CloseHandle(again);
    std::printf("%s\n",failures?"FALHOU":"tudo certo");
    return failures?1:0;
}
