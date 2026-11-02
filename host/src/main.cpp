// Azimuth_PS5 host: maps the unchanged Azimuth 0.9.7 engine and runs its
// ORIGINAL entry point on an engine thread (Windows TEB through gs, static
// TLS, TLS callbacks). Imports the host implements (win32/exports.inc) are
// bound to them; every other import is a diagnostic stop that names the
// function and the engine address that called it. The run ends at its first
// stop: the engine threads end in their host frames and the host returns
// from main, as the probe that ran on the console does.
#include "GuestThreads.h"
#include "HostHeap.h"
#include "Image.h"
#include "Modules.h"
#include "Platform.h"
#include "Sha256.h"
#include "Teb.h"
#include "gfx/Png.h"
#include "Input.h"
#include "gfx/VideoOut.h"
#include "gpu/D3d12Check.h"
#include "gpu/Dxgi.h"
#include "guest_abi.h"
#include "win32/Win32.h"
#include "win32/VectoredExceptions.h"
#include <sys/resource.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <new>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <pthread.h>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace azimuth {
namespace {
constexpr size_t MainStackSize=8u<<20;

FILE* reportFile=nullptr;
std::atomic<int> loopStage{0};
// Lines go to a queue that a thread of its own writes (title 1.16): a write
// and an fsync per line, on the engine thread that logged, slowed the game
// down once its world streaming logged its memory calls. The file is synced
// at most every 500 ms and when the run ends (flushNotes); the fault handler
// keeps writing straight to the file. The queue is made on first use and never
// destroyed: the host's allocator logs before main (static initialization) and
// the writer runs until the process ends. A line logged while the same thread
// is queueing one (an allocation inside the queue that logs) is written at once.
struct Notes {std::mutex lock,writing;std::condition_variable changed;std::vector<std::string> queue;bool writer=false;};
Notes& notes() {
    alignas(Notes) static unsigned char storage[sizeof(Notes)];
    static Notes* made=new(storage) Notes;
    return *made;
}
thread_local bool queueing=false;
void writeNotes(std::vector<std::string>& lines,bool sync) {
    std::lock_guard<std::mutex> guard(notes().writing);
    for(auto const& line:lines){std::fputs(line.c_str(),stdout);if(reportFile)std::fputs(line.c_str(),reportFile);}
    std::fflush(stdout);
    if(reportFile){std::fflush(reportFile);if(sync)fsync(fileno(reportFile));}
}
void notesThread() {
    auto& n=notes();
    auto lastSync=std::chrono::steady_clock::now();bool dirty=false;
    for(;;) {
        std::vector<std::string> batch;
        {
            std::unique_lock<std::mutex> guard(n.lock);
            n.changed.wait_for(guard,std::chrono::milliseconds(200),[&]{return !n.queue.empty();});
            batch.swap(n.queue);
        }
        dirty=dirty||!batch.empty();
        auto now=std::chrono::steady_clock::now();
        bool sync=dirty&&now-lastSync>=std::chrono::milliseconds(500);
        writeNotes(batch,sync);
        if(sync){lastSync=now;dirty=false;}
    }
}
void note(const std::string& text) {
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string line=std::to_string(ms)+" "+text+"\n";
    if(queueing){std::fputs(line.c_str(),stdout);if(reportFile)std::fputs(line.c_str(),reportFile);return;}
    auto& n=notes();
    queueing=true;
    {
        std::lock_guard<std::mutex> guard(n.lock);
        n.queue.push_back(std::move(line));
        if(!n.writer){n.writer=true;std::thread(notesThread).detach();}
    }
    queueing=false;
    n.changed.notify_one();
}
// Everything queued, on disk; then no more writes to the file.
void flushNotes(bool close) {
    auto& n=notes();
    std::vector<std::string> batch;
    {std::lock_guard<std::mutex> guard(n.lock);batch.swap(n.queue);}
    writeNotes(batch,true);
    if(close){std::lock_guard<std::mutex> guard(n.writing);if(reportFile){std::fclose(reportFile);reportFile=nullptr;}}
}
std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
template<class T>T get(const Bytes& b,size_t at) {
    if(at>b.size()||sizeof(T)>b.size()-at)throw std::runtime_error("Leitura fora da imagem");
    T value;std::memcpy(&value,b.data()+at,sizeof value);return value;
}
// Explicit profiles keep the original executable unchanged and audited.
// A version string alone is not sufficient to select a compatible image.
struct EngineProfile {const char* version;const char* hash;size_t bytes;uint32_t imageSize,entry,loadConfig;};
constexpr EngineProfile EngineProfiles[]={
    {"0.9.7","5d3ce30b440dab2604fe42ff7cd129394f0b98418e22b2a44270f548199bc6a9",3087872,3481600,0x1dc6ec,0x26d640},
    {"1.0.2","42527964f48c90a537f2414200b8598ab9a6f92919bfbe046dbecccb36e7c597",9715712,10153984,0x71675c,0x832e70}
};
const EngineProfile& validateProfile(const PeImage& p) {
    auto hash=sha256(p.bytes().data(),p.bytes().size());std::string digest;char pair[3];
    for(auto byte:hash){std::snprintf(pair,sizeof pair,"%02x",byte);digest+=pair;}
    const EngineProfile* selected=nullptr;
    for(auto& profile:EngineProfiles)if(digest==profile.hash&&p.bytes().size()==profile.bytes)selected=&profile;
    if(!selected)throw std::runtime_error("Motor Azimuth sem perfil validado; SHA-256="+digest+"; suportados: 0.9.7 e 1.0.2");
    if(p.imageBase!=0x140000000||p.imageSize!=selected->imageSize||p.entryPoint!=selected->entry||p.directories[10].rva!=selected->loadConfig||p.directories[10].size!=0x140)
        throw std::runtime_error("Layout diferente do perfil Azimuth "+std::string(selected->version));
    auto b=p.mappedBytes(p.imageBase);auto d=p.directories[10];
    size_t pe=get<uint32_t>(p.bytes(),60);
    if(get<uint16_t>(p.bytes(),pe+24+70)&0x4000)throw std::runtime_error("Motor requer CFG de loader ainda nao integrado");
    if(get<uint32_t>(b,d.rva)!=0x140||get<uint32_t>(b,d.rva+0x90)!=0x100)throw std::runtime_error("Load config diferente do perfil de integracao");
    // Only the known cookie/guard pointer fields may be nonzero in this profile.
    for(size_t n=0;n<d.size;++n) {
        bool known=(n<4)||(n>=0x58&&n<0x60)||(n>=0x70&&n<0x80)||(n>=0x90&&n<0x94)||(n>=0x100&&n<0x108)||(n>=0x118&&n<0x140);
        if(!known&&b[d.rva+n])throw std::runtime_error("Campo de load config ainda nao tratado");
    }
    for(size_t offset:{0x58,0x70,0x78,0x100,0x118,0x120,0x128,0x130,0x138}) {
        auto address=get<uint64_t>(b,d.rva+offset);
        if(address<p.imageBase||address-p.imageBase>=p.imageSize)throw std::runtime_error("Ponteiro de load config fora do perfil");
    }
    return *selected;
}
Bytes readFile(const std::string& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)return {};
    in.seekg(0,std::ios::end);
    auto size=in.tellg();
    if(size<0)return {};
    in.seekg(0);
    Bytes bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));
    if(!in)return {};
    return bytes;
}
std::string findEngine(int argc,char** argv) {
    if(argc>1)return argv[1];
    // The internal SSD first (title 1.24: the USB drive's ~38 MB/s let the
    // game's state script run ahead of its loading), then the old places.
    std::vector<std::string> candidates{"/data/PC/FH2/azimuth.exe","/data/azimuth-ps5/azimuth.exe"};
    for(int n=0;n<8;++n) {
        candidates.push_back("/mnt/usb"+std::to_string(n)+"/PC/FH2/azimuth.exe");
        candidates.push_back("/mnt/usb"+std::to_string(n)+"/azimuth-ps5/azimuth.exe");
    }
    // Each candidate is logged with why it failed: a title may not see every
    // folder a payload does.
    std::string tried;
    for(auto const& path:candidates) {
        int error=platform::readable(path);
        if(!error)return path;
        if(path.rfind("/data/",0)==0||path.find("/usb1/")!=std::string::npos||path.find("/usb0/")!=std::string::npos)
            tried+=" "+path+" (errno "+std::to_string(error)+")";
    }
    note("azimuth.exe nao legivel em nenhum candidato:"+tried);
    throw std::runtime_error("azimuth.exe nao encontrado (procurado em /data/PC/FH2, /data/azimuth-ps5 e /mnt/usb0..7/PC/FH2; veja o errno de cada um acima)");
}
// REPORT_DIR/host-stop containing "mapa" ends the run after mapping the engine.
bool mapOnly() {
    std::ifstream in(platform::reportDir()+"/host-stop");
    std::string word;in>>word;return word=="mapa";
}

struct Run {
    std::unique_ptr<PeImage> parsed;
    std::unique_ptr<Image> image;
    std::unique_ptr<ProcessBlock> process;
    // Written by the main engine thread before it sets mainDone.
    std::string error;
    bool entryReturned=false;
    // host --d3d12: the D3D12 check on the main thread instead of the engine.
    bool d3d12=false;uint32_t checkCode=1;
    std::atomic<bool> mainDone{false};
} run;
// Engine imports bound to the host's own implementations. The api-ms-win-core
// API sets are served by the KERNEL32 implementations, as on Windows.
std::string libraryName(std::string name) {
    for(auto& c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    if(name.size()>4&&name.compare(name.size()-4,4,".dll")==0)name.resize(name.size()-4);
    if(name.rfind("api-ms-win-core-",0)==0)name="kernel32";
    for(auto& c:name)if(c=='-')c='_';   // vulkan-1 is listed as vulkan_1
    return name;
}
// The engine's d3d12.lib imports these by ordinal (as AzimuthUwp found on the Xbox).
std::string ordinalName(const std::string& library,uint16_t ordinal) {
    if(library=="d3d12"&&ordinal==101)return "D3D12CreateDevice";
    if(library=="d3d12"&&ordinal==102)return "D3D12GetDebugInterface";
    return {};
}
void* implemented(const Import& item) {
    auto library=libraryName(item.library);
    auto symbol=item.byOrdinal?ordinalName(library,item.ordinal):item.symbol;
    if(symbol.empty())return nullptr;
    for(auto e=guestExports;e->name;++e)
        if(libraryName(e->library)==library&&symbol==e->name)return const_cast<void*>(e->address);
    return nullptr;
}
std::string describe(uint64_t address) {
    if(run.image&&run.image->contains(address))return "engine+"+hex(address-run.image->base());
    auto inModule=describeModuleAddress(address);
    return inModule.empty()?hex(address):inModule;
}
std::string importName(uint32_t slot) {
    if(slot>=ModuleImportSlots)return moduleImportName(slot);
    if(!run.parsed||slot>=run.parsed->imports.size())return "#"+std::to_string(slot);
    auto const& item=run.parsed->imports[slot];
    return item.library+"!"+(item.byOrdinal?"#"+std::to_string(item.ordinal):item.symbol);
}
// The launcher's last picture, as REPORT_DIR/frame.png: written next to it
// and renamed, so the file on the console is always a whole image.
// Each new picture goes to the screen at once (VideoOut) and, at most every
// two seconds, to frame.png; `force` writes the last one regardless.
struct FrameSaver {
    uint64_t serial=0;unsigned saved=0;int width=0,height=0;
    std::chrono::steady_clock::time_point lastPng{};
    std::atomic<bool> writing{false};
    // frame.png is written by a thread of its own: on the console a 6 MB write
    // can stall for a long time, and the picture must not wait for it.
    void write(win32::Frame frame,bool announce) {
        auto path=platform::reportDir()+"/frame.png",partial=path+".tmp";
        if(!gfx::savePng(partial,frame.pixels,frame.width,frame.height)||std::rename(partial.c_str(),path.c_str())!=0) {
            note("Quadro do launcher nao gravado em "+path);return;
        }
        if(announce)
            note("Quadro do launcher gravado: "+path+" ("+std::to_string(frame.width)+"x"+std::to_string(frame.height)+
                 ", janela "+hex(reinterpret_cast<uintptr_t>(frame.window))+")");
    }
    win32::Frame frame;   // reused: a 1080p copy is 8 MB of the title's memory
    void save(bool force=false) {
        if(!win32::latestFrame(frame,serial))return;
        serial=frame.serial;
        loopStage=3;video::present(frame.pixels.data(),frame.width,frame.height);loopStage=4;
        auto now=std::chrono::steady_clock::now();
        // frame.png once, and again at the end: the screen shows the rest.
        if(!force&&saved)return;
        if(force) {
            for(int wait=0;writing&&wait<500;++wait)usleep(10000);
            write(frame,false);++saved;return;
        }
        if(writing.exchange(true))return;
        lastPng=now;bool announce=!saved++||frame.width!=width||frame.height!=height;
        width=frame.width;height=frame.height;
        std::thread([this,announce](win32::Frame copy){write(std::move(copy),announce);writing=false;},frame).detach();
    }
};
// The engine's first thread: its entry point. While the engine runs, gs holds
// its TEB; the host restores gs before it logs anything here.
void* guestMain(void*) {
    try {
        auto id=newThreadId();
        if(run.d3d12) {
            note("Thread principal "+std::to_string(id)+" (TEB pelo gs, sem o motor): verificacao D3D12");
            auto outcome=runAsGuest(id,true,MainStackSize,[]()->uint32_t{return gpu::d3d12Check();});
            if(outcome.end==GuestEnd::Returned){run.entryReturned=true;run.checkCode=outcome.code;}
        }else {
            note("Etapa 4: thread principal do motor "+std::to_string(id)+"; chamando o entry point ORIGINAL ("+
                 describe(reinterpret_cast<uint64_t>(run.image->entryAddress()))+")");
            auto outcome=runAsGuest(id,true,MainStackSize,[]()->uint32_t{guestCallEntry(run.image->entryAddress());return 0;});
            if(outcome.end==GuestEnd::Returned){run.entryReturned=true;note("Entry point do motor retornou; revisar o log antes de considerar sucesso");}
        }
    }catch(std::exception const& e){run.error=e.what();}
    run.mainDone=true;
    return nullptr;
}
}
}

using namespace azimuth;

namespace azimuth::win32 {
void log(const std::string& text){note(text);}
void stopGuest(const std::string& reason){StopInfo info;info.reason=reason;stopRun(std::move(info));}
}

int main(int argc,char** argv) {
    // The previous run's report stays as host-anterior.txt (title 1.16: a new
    // start overwrote the only run that had reached the open world).
    std::rename((platform::reportDir()+"/host.txt").c_str(),(platform::reportDir()+"/host-anterior.txt").c_str());
    reportFile=std::fopen((platform::reportDir()+"/host.txt").c_str(),"w");
    run.d3d12=argc>1&&!std::strcmp(argv[1],"--d3d12");
    note(std::string("Azimuth_PS5 host 0.8 (")+platform::name()+"), fase 3: "+
         (run.d3d12?"verificacao D3D12 (vkd3d-proton carregado pelo host), sem o motor":"motor original 0.9.7"));
    installFaultHandlers(reportFile?fileno(reportFile):-1);
    // The descriptor limit, raised to the most allowed: the game keeps many
    // files open, and the loader found none left for FFmpeg (title 1.04, EMFILE).
    if(struct rlimit limit;!getrlimit(RLIMIT_NOFILE,&limit)) {
        auto before=limit.rlim_cur;
        if(limit.rlim_cur<limit.rlim_max){limit.rlim_cur=limit.rlim_max;setrlimit(RLIMIT_NOFILE,&limit);getrlimit(RLIMIT_NOFILE,&limit);}
        note("Descritores de arquivo: limite "+std::to_string(uint64_t(before))+" -> "+std::to_string(uint64_t(limit.rlim_cur))+
             " (maximo "+std::to_string(uint64_t(limit.rlim_max))+")");
    }
    std::string summary;int code=0;
    try {
      if(run.d3d12) {
        // No engine: an empty process (no image, no mounts) whose main
        // thread loads vkd3d-proton's d3d12.dll as the engine would.
        run.process=std::make_unique<ProcessBlock>(0);
        // C:\azimuth is the report folder: vkd3d-proton keeps its pipeline cache there.
        win32::configure({0,0,u"C:\\azimuth\\d3d12-check.exe","d3d12-check.exe",{{u"C:\\azimuth",platform::reportDir()}},u"C:\\azimuth"});
        setupGuest({nullptr,run.process.get(),{}});
      }else {
        auto path=findEngine(argc,argv);
        auto bytes=readFile(path);
        note("Motor: "+path+" ("+std::to_string(bytes.size())+" bytes)");
        run.parsed=std::make_unique<PeImage>(std::move(bytes));
        note("Etapa 1: validar SHA-256, layout e load config do motor");
        auto& engineProfile=validateProfile(*run.parsed);
        note("Perfil do motor: Azimuth "+std::string(engineProfile.version));
        auto stubs=static_cast<uint8_t*>(buildImportStubs(0,run.parsed->imports.size()));
        note("Etapa 2: mapear e relocar o motor (TLS estatico no slot 0)");
        unsigned bound=0;
        run.image=std::make_unique<Image>(*run.parsed,[&](const Import& item,unsigned slot) {
            if(auto address=implemented(item)){++bound;return address;}
            return static_cast<void*>(stubs+slot*ImportStubSize);
        },0,true);   // the engine's mixed 16 KiB blocks (none writable and executable) get the union
        run.process=std::make_unique<ProcessBlock>(run.image->base());
        if(!run.image->mixedBlocks().empty()) {
            std::string list;
            for(auto rva:run.image->mixedBlocks())list+=" "+hex(rva);
            note("Motor: blocos de "+std::to_string(platform::protectionGranularity())+" bytes com secoes de protecoes diferentes recebem a uniao delas:"+list);
        }
        note("Motor mapeado em "+hex(run.image->base())+(run.image->base()==run.parsed->imageBase?" (base preferida)":" (relocado)"));
        note("Etapa 3: "+std::to_string(bound)+" de "+std::to_string(run.parsed->imports.size())+
             " imports implementados pelo host; os demais sao paradas de diagnostico");
        // The engine sees its folder as E:\PC\FH2, where it was on the Xbox's
        // drive; when that folder is <drive>/PC/FH2, the drive is E:\ too.
        auto engineDir=path.substr(0,path.find_last_of('/'));
        std::vector<win32::Mount> mounts{{u"E:\\PC\\FH2",engineDir}};
        if(engineDir.size()>7&&engineDir.compare(engineDir.size()-7,7,"/PC/FH2")==0)
            mounts.push_back({u"E:",engineDir.substr(0,engineDir.size()-7)});
        for(auto const& mount:mounts)note("Unidade: "+win32::utf8(reinterpret_cast<const uint16_t*>(mount.guest.data()),mount.guest.size())+" -> "+mount.host);
        win32::configure({run.image->base(),run.image->size(),u"E:\\PC\\FH2\\azimuth.exe","azimuth.exe",mounts,u"E:\\PC\\FH2"});
        setupGuest({run.image.get(),run.process.get(),run.image->tlsCallbacks()});
      }
        if(!run.d3d12&&mapOnly()) {
            note("host-stop: execucao encerrada depois do mapeamento");
            summary="Azimuth_PS5: motor mapeado";
        }else {
            pthread_attr_t attributes;pthread_attr_init(&attributes);
            pthread_attr_setstacksize(&attributes,MainStackSize+(256u<<10));
            pthread_t thread;
            int error=pthread_create(&thread,&attributes,guestMain,nullptr);
            pthread_attr_destroy(&attributes);
            if(error)throw std::runtime_error("pthread_create falhou: "+std::to_string(error));
            // The run ends at its first stop, or when the entry point returns;
            // meanwhile the launcher's picture is saved once a second when it changes.
            input::start();
            FrameSaver frames;
            // Dedicated 100 ms FPS sampler: it still reports zero frames when
            // the render thread is stuck. Heavy watchdog dumps cannot hold it.
            std::thread([]{
                if(auto scheduling=platform::shareCpu(true);!scheduling.empty())note("Agendamento da amostragem FPS: "+scheduling);
                (void)gpu::frameWindowReport();
                auto next=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
                while(!runStopping()){
                    std::this_thread::sleep_until(next);if(runStopping())break;
                    auto line=gpu::frameWindowReport();if(!line.empty())note(line);
                    next+=std::chrono::milliseconds(100);
                    auto now=std::chrono::steady_clock::now();
                    // Never fabricate backfilled 100 ms samples after a delay.
                    if(next<=now)next=now+std::chrono::milliseconds(100);
                }
            }).detach();
            // A watchdog thread of its own (the loop below may be the thing that is stuck).
            if(auto scheduling=platform::shareCpu(true);!scheduling.empty())note("Agendamento do laco do host: "+scheduling);
            std::thread([]{
                if(auto scheduling=platform::shareCpu(true);!scheduling.empty())note("Agendamento da vigia: "+scheduling);
                // Every 100 ms the title's flexible memory, when it moved by 8 MiB or more.
                auto watchMemory=[](unsigned halfSeconds) {
                    static uint64_t lastAvailable=~uint64_t(0);
                    for(unsigned n=0;n<halfSeconds;++n) {
                        uint64_t configured=0,available=0;
                        if(platform::flexibleMemory(configured,available)) {
                            uint64_t moved=available>lastAvailable?available-lastAvailable:lastAvailable-available;
                            static size_t lastLarge=0;
                            size_t large=heapMappedBytes(),grew=large>lastLarge?large-lastLarge:lastLarge-large;
                            if(lastAvailable==~uint64_t(0)||moved>=(8u<<20)||available<(32u<<20)||grew>=(64u<<20)) {
                                lastAvailable=available;lastLarge=large;
                                note("Memoria flexivel: "+std::to_string(available>>20)+" MiB livres de "+std::to_string(configured>>20)+
                                     " MiB; blocos grandes do alocador do host: "+std::to_string(heapMappedBytes()>>20)+
                                     " MiB; maior trecho livre de memoria direta: "+std::to_string(directMemoryFree()>>20)+" MiB"+
                                     "; memoria direta em uso: "+std::to_string(directMemoryUsed()>>20)+" MiB");
                            }
                        }
                        usleep(100000);
                    }
                };
                // Every 10 s for the whole run: the loop's stage and each engine
                // thread's last calls (title 0.73: the engine went quiet after
                // splitting its graphics heap). Sampling threads with signals
                // is gone: it faulted twice on the console and rarely answered.
                for(unsigned dump=1;;++dump) {
                    watchMemory(100);
                    note("Despejo "+std::to_string(dump)+": laco principal na etapa "+std::to_string(loopStage.load()));
                    {
                        // Frames presented and the busiest threads over the last 10 s (title 1.16).
                        static uint64_t framesBefore=0,nanosBefore=0;
                        uint64_t frames=gpu::presentedFrames(),nanos=gpu::presentNanoseconds();
                        char text[160];
                        std::snprintf(text,sizeof text,"Desempenho: %.1f quadros/s, %.2f ms por Present (SyncInterval %u, flags 0x%x); ",
                                      double(frames-framesBefore)/10.0,frames>framesBefore?double(nanos-nanosBefore)/1e6/double(frames-framesBefore):0.0,
                                      gpu::presentInterval(),gpu::presentFlags());
                        framesBefore=frames;nanosBefore=nanos;
                        note(text+busiestThreads(10.0));
                        note("Tempo no host: "+hostTimeReport());
                        note(win32::writeWatchReport());
                        note(win32::vectoredExceptionReport());
                        note(gpu::frameIntervalReport());
                        note(win32::memoryProtectionReport());
                        note(win32::watchProtectionReport());
                        note(win32::watchedIoReport());
                        note(win32::watchStageReport());
                        note(win32::bulkWriteReport());
                        note(win32::repeatedAudioReport());
                        note(win32::handleContentionReport());
                        note(win32::directReleaseReport());
                        for(auto& event:win32::memorySlowEvents())note(event);
                    }
                    note("Estado do motor ("+std::to_string(dump)+"): ultimas chamadas por thread:"+recentHostCallsAllThreads(24));
                }
            }).detach();
            for(;;) {
                loopStage=1;
                if(waitForStop(100)||run.mainDone)break;
                loopStage=2;frames.save();
            }
            if(runStopping()&&!waitForGuestThreads(3000))
                note("Threads do motor ainda ativas depois da parada: "+std::to_string(liveGuestThreads()));
            frames.serial=0;frames.save(true);
            if(frames.saved)note("Quadros do launcher gravados: "+std::to_string(frames.saved)+" (o ultimo fica em frame.png)");
            if(!run.error.empty())throw std::runtime_error(run.error);
            auto stop=stopInfo();
            if(stop.missingImport) {
                auto name=importName(stop.slot);
                note("IMPORT SEM ADAPTACAO CHAMADO: "+name+" (import "+std::to_string(stop.slot)+"), chamado de "+describe(stop.returnAddress)+
                     " na thread "+std::to_string(stop.threadId));
                std::string trace;
                for(size_t n=0;n<stop.trace.size();++n)trace+=(n?" ":"")+describe(stop.trace[n]);
                if(!trace.empty())note("Enderecos do motor na pilha (backtrace aproximado): "+trace);
                note("O motor original executou ate um import do Windows ainda sem implementacao. Parada de diagnostico, nao uma falha do loader.");
                summary="Azimuth_PS5: motor parou em "+name;
            }else if(stop.stopped) {
                note("Motor parado pelo host na thread "+std::to_string(stop.threadId)+": "+stop.reason);
                if(auto fibers=win32::describeFibers();!fibers.empty())note("Fibras na parada:"+fibers);
                std::string trace;
                for(size_t n=0;n<stop.trace.size();++n)trace+=(n?" ":"")+describe(stop.trace[n]);
                if(!trace.empty())note("Enderecos do motor na pilha (backtrace aproximado): "+trace);
                summary="Azimuth_PS5: "+stop.reason;
            }else if(run.d3d12) {
                summary=run.checkCode?"Azimuth_PS5: verificacao D3D12 falhou; veja host.txt":"Azimuth_PS5: D3D12 pelo vkd3d-proton passou";
                code=int(run.checkCode);
            }else {
                summary="Azimuth_PS5: entry point retornou";
            }
        }
    }catch(std::exception const& e) {
        note(std::string("Tentativa interrompida: ")+e.what());
        summary=std::string("Azimuth_PS5: ")+e.what();code=1;
    }
    platform::notify(summary);
    note("Fim do host");
    flushNotes(true);
    // A normal return from main, as the probe that ran on the console ends.
    return code;
}
