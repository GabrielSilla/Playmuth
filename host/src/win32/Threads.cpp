// Engine threads and the waits between them: threads, events, waitable
// timers, condition variables over SRW locks, one-time initialization,
// WaitOnAddress, sleeping, and what the system reports about CPUs and memory.
// Every wait wakes at least every 100 ms, so that once the run is stopping
// the waiting engine thread ends instead of waiting forever.
#include "Win32.h"
#include <deque>
#include "../GuestThreads.h"
#include "../guest_abi.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <unistd.h>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;
// steady_clock's time points (CLOCK_MONOTONIC), read from the time-stamp
// counter (title 1.26: a system call of ~950 ns on the console, two or three
// per wait, and millions of waits a second with a zero timeout).
struct Clock {
    using duration=std::chrono::steady_clock::duration;using time_point=std::chrono::steady_clock::time_point;
    static time_point now(){return time_point(std::chrono::nanoseconds(fastMonotonicNs()));}
};

namespace {
constexpr uint32_t Infinite=0xffffffff,WaitObject0=0,WaitTimeout=258,WaitFailed=0xffffffff,StillActive=259,ErrorTimeout=1460;
constexpr auto Slice=std::chrono::milliseconds(100);
std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
Clock::time_point deadlineAfter(uint32_t milliseconds) {
    return milliseconds==Infinite?Clock::time_point::max():Clock::now()+std::chrono::milliseconds(milliseconds);
}

// Waitable objects share one lock: a wait for several objects is a predicate
// over all of them. Each waiting thread sleeps on a condition of its own and
// lists the objects it waits for, and a signal wakes only the threads waiting
// for that object (title 1.19: one condition for the whole process woke every
// waiting thread, about a hundred, on each of tens of thousands of SetEvent
// per second; the game's job workers starved, a job the loading fiber waited
// for took several frames, and the game's state script ran ahead of the
// loading into a crash). One condition per thread, not per object: on the
// console each is a kernel object from a small pool.
std::mutex waitLock;
struct Waiter {std::condition_variable wake;std::vector<const Object*> objects;bool any=false;};
std::vector<Waiter*> waiters;   // under waitLock
thread_local Waiter* ownWaiter=nullptr;
// Registers the calling thread as waiting for these objects until the scope ends. Under waitLock.
struct Waiting {
    Waiter& waiter;
    Waiting(std::vector<const Object*> objects,bool any):waiter(ownWaiter?*ownWaiter:*(ownWaiter=new Waiter)) {
        waiter.objects=std::move(objects);waiter.any=any;waiters.push_back(&waiter);
    }
    ~Waiting(){waiters.erase(std::find(waiters.begin(),waiters.end(),&waiter));}
};
struct Waitable:Object {
    std::string origin;   // who created it (thread and code addresses), for the long-wait report
    virtual bool signaled(Clock::time_point){return false;}   // under waitLock
    virtual void consume(){}                                // an auto-reset object is taken
    virtual Clock::time_point due(){return Clock::time_point::max();}
    virtual std::string describe(){return "objeto";}       // under waitLock
};
std::string originHere(){return "thread "+std::to_string(azimuth::currentGuestThreadId())+","+codeCallers();}
struct Event:Waitable {
    bool manual=false,set=false;
    bool signaled(Clock::time_point) override{return set;}
    void consume() override{if(!manual)set=false;}
    std::string describe() override{return std::string("evento ")+(manual?"manual":"automatico")+(set?" sinalizado":" nao sinalizado");}
};
struct Start;
struct Thread:Waitable {
    uint32_t id=0,code=StillActive;bool finished=false;
    Start* pending=nullptr;   // CREATE_SUSPENDED: started by ResumeThread
    uint32_t suspends=0;      // SuspendThread's count (under threadsLock); the thread is not stopped
    std::u16string description;
    bool signaled(Clock::time_point) override{return finished;}
    std::string describe() override{return "thread "+std::to_string(id)+(pending?" (suspensa, nunca iniciada)":finished?" (encerrada)":" (rodando)");}
};
struct Timer:Waitable {
    bool manual=false,set=false;Clock::time_point at=Clock::time_point::max();std::chrono::milliseconds period{0};
    bool signaled(Clock::time_point now) override {
        if(at!=Clock::time_point::max()&&now>=at){set=true;at=period.count()?at+period:Clock::time_point::max();}
        return set;
    }
    void consume() override{if(!manual)set=false;}
    Clock::time_point due() override{return at;}
    std::string describe() override{return "timer";}
};

std::mutex threadsLock;
std::map<uint32_t,std::weak_ptr<Thread>> threadsById;
thread_local std::shared_ptr<Thread> self;
// The calling engine thread's object (created on first use for the main
// thread, or taken from OpenThread's for it).
std::shared_ptr<Thread> currentThread() {
    if(!self) {
        auto id=azimuth::currentGuestThreadId();
        std::lock_guard<std::mutex> lock(threadsLock);
        auto found=threadsById.find(id);
        if(found!=threadsById.end())self=found->second.lock();
        if(!self){self=std::make_shared<Thread>();self->id=id;threadsById[id]=self;}
    }
    return self;
}
std::shared_ptr<Object> resolveHandle(void* handle) {
    if(reinterpret_cast<intptr_t>(handle)==-2)return currentThread();
    return findHandle(handle);
}
template<class Objects>
uint32_t waitFor(const Objects& objects,bool all,uint32_t milliseconds) {
    auto deadline=deadlineAfter(milliseconds);
    // A wait still going after LongWait is reported once, with what it waits on
    // (title 0.86: the game's main thread waited forever with no other sign).
    constexpr auto LongWait=std::chrono::seconds(10);
    auto reportAt=Clock::now()+LongWait;bool reported=false;
    std::unique_lock<std::mutex> lock(waitLock);
    for(;;) {
        auto now=Clock::now();
        if(!reported&&now>=reportAt&&deadline>now) {
            reported=true;
            static std::atomic<unsigned> reports{0};
            if(reports++<60) {
                std::string text="Espera longa na thread "+std::to_string(azimuth::currentGuestThreadId())+(all?" (todos)":"")+":";
                for(auto const& o:objects)text+=" ["+o->describe()+" em "+hex(reinterpret_cast<uint64_t>(o.get()))+", criado por "+(o->origin.empty()?"?":o->origin)+"]";
                text+="; chamado de"+codeCallers();
                lock.unlock();log(text);lock.lock();
            }
        }
        if(all) {
            bool ready=true;for(auto const& o:objects)if(!o->signaled(now))ready=false;
            if(ready){for(auto const& o:objects)o->consume();return WaitObject0;}
        }else {
            for(size_t n=0;n<objects.size();++n)if(objects[n]->signaled(now)){objects[n]->consume();return WaitObject0+uint32_t(n);}
        }
        if(azimuth::runStopping()){lock.unlock();azimuth::leaveIfStopping();lock.lock();}
        if(now>=deadline)return WaitTimeout;
        auto wake=std::min(deadline,now+Slice);
        if(!reported)wake=std::min(wake,reportAt);
        for(auto const& o:objects)wake=std::min(wake,o->due());
        // Registered only for the sleep: a wait that is satisfied at once
        // (WaitForSingleObject with no timeout, millions per second) costs nothing more.
        std::vector<const Object*> watched;
        watched.reserve(objects.size());
        for(auto const& o:objects)watched.push_back(o.get());
        Waiting waiting(std::move(watched),false);
        waiting.waiter.wake.wait_until(lock,wake);
    }
}
// Wakes the threads waiting for this object (every waiting thread when null).
void notifyWaitersLocked(const Object* changed=nullptr) {
    for(auto waiter:waiters)
        if(!changed||waiter->any||std::find(waiter->objects.begin(),waiter->objects.end(),changed)!=waiter->objects.end())
            waiter->wake.notify_one();
}
void notifyWaiters(const Object* changed=nullptr) {
    std::lock_guard<std::mutex> lock(waitLock);notifyWaitersLocked(changed);
}
// Sleeps in slices, leaving early if the run stops.
void sleepFor(uint32_t milliseconds) {
    if(!milliseconds){sched_yield();return;}
    auto deadline=deadlineAfter(milliseconds);
    while(Clock::now()<deadline) {
        azimuth::leaveIfStopping();
        auto step=std::min(deadline-Clock::now(),Clock::duration(Slice));
        if(step.count()>0)usleep(useconds_t(std::chrono::duration_cast<std::chrono::microseconds>(step).count()));
    }
}

struct Start {std::shared_ptr<Thread> thread;void* start;void* parameter;size_t stack;bool pool;void* context;};
void* hostThreadMain(void* argument) {
    std::unique_ptr<Start> start(static_cast<Start*>(argument));
    self=start->thread;
    log("Thread do motor "+std::to_string(self->id)+" iniciada");
    azimuth::GuestOutcome outcome;
    try {
        outcome=azimuth::runAsGuest(self->id,false,start->stack,[&]()->uint32_t {
            if(start->pool){static char instance;guestCallSimpleCallback(start->start,&instance,start->context);return 0;}
            return guestCallThreadStart(start->start,start->parameter);
        });
    }catch(std::exception const& e){log(std::string("Thread do motor nao iniciada: ")+e.what());}
    {
        std::lock_guard<std::mutex> lock(waitLock);
        self->finished=true;self->code=outcome.code;notifyWaitersLocked(self.get());
    }
    if(!azimuth::runStopping())
        log("Thread do motor "+std::to_string(self->id)+" encerrada: codigo="+std::to_string(outcome.code)+
            (outcome.end==azimuth::GuestEnd::Exited?" (ExitThread)":""));
    return nullptr;
}
bool spawn(std::unique_ptr<Start> start) {
    pthread_attr_t attributes;pthread_attr_init(&attributes);
    // The engine's stack plus room for the host's own frames.
    pthread_attr_setstacksize(&attributes,start->stack+(256u<<10));
    pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);
    pthread_t thread;auto raw=start.release();
    azimuth::announceGuestThread();
    auto id=raw->thread->id;auto before=Clock::now();
    int error=pthread_create(&thread,&attributes,hostThreadMain,raw);
    pthread_attr_destroy(&attributes);
    log("pthread_create da thread "+std::to_string(id)+": "+std::to_string(error)+" em "+
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-before).count())+" ms");
    if(error){delete raw;azimuth::withdrawGuestThread();return false;}
    return true;
}
std::shared_ptr<Thread> newThread() {
    auto thread=std::make_shared<Thread>();thread->id=azimuth::newThreadId();thread->origin=originHere();
    std::lock_guard<std::mutex> lock(threadsLock);threadsById[thread->id]=thread;
    return thread;
}
}

namespace azimuth::win32 {
void wakeWaiters(){notifyWaiters();}
void* hostEvent(){auto event=std::make_shared<Event>();event->manual=true;return addHandle(event);}
void setHostEvent(void* handle,bool set) {
    auto event=std::dynamic_pointer_cast<Event>(findHandle(handle));if(!event)return;
    std::lock_guard<std::mutex> lock(waitLock);event->set=set;
    if(set)notifyWaitersLocked(event.get());
}
bool hostEventSet(void* handle) {
    auto event=std::dynamic_pointer_cast<Event>(findHandle(handle));
    std::lock_guard<std::mutex> lock(waitLock);return event&&event->set;
}
}

// ---- Threads -----------------------------------------------------------
extern "C" void* win_CreateThread(void*,uint64_t stack,void* start,void* parameter,uint32_t flags,uint32_t* id) {
    constexpr uint32_t CreateSuspended=0x4;
    if(!start){setLastError(ErrorInvalidParameter);return nullptr;}
    auto thread=newThread();
    size_t size=stack?size_t(stack):(1u<<20);
    if(size<(256u<<10))size=256u<<10;
    auto base=process().imageBase;auto address=reinterpret_cast<uint64_t>(start);
    log("CreateThread do motor: thread "+std::to_string(thread->id)+" rotina="+
        (address>=base&&address-base<process().imageSize?"engine+"+hex(address-base):hex(address))+" pilha="+std::to_string(stack));
    auto handle=addHandle(thread);
    // A suspended thread (msvcr110's _beginthreadex creates them so) starts at ResumeThread.
    if(flags&CreateSuspended) {
        thread->pending=new Start{thread,start,parameter,size,false,nullptr};
        if(id)*id=thread->id;
        return handle;
    }
    if(!spawn(std::unique_ptr<Start>(new Start{thread,start,parameter,size,false,nullptr}))) {
        removeHandle(handle);setLastError(ErrorNotEnoughMemory);return nullptr;
    }
    if(id)*id=thread->id;
    return handle;
}
extern "C" void win_ExitThread(uint32_t code){azimuth::exitGuestThread(code);}
// The previous suspend count: 1 for a thread created suspended, now started.
extern "C" uint32_t win_ResumeThread(void* handle) {
    auto thread=std::dynamic_pointer_cast<Thread>(findHandle(handle));
    if(!thread){setLastError(ErrorInvalidHandle);return ~0u;}
    Start* start=nullptr;
    {
        std::lock_guard<std::mutex> lock(threadsLock);std::swap(start,thread->pending);
        if(!start)return thread->suspends?thread->suspends--:0;
    }
    if(!spawn(std::unique_ptr<Start>(start))){setLastError(ErrorNotEnoughMemory);return ~0u;}
    return 1;
}
// Azimuth 1.0.2 suspends each other thread around writing a module's static
// TLS template into that thread's block (engine+0x672600), and reads its
// CONTEXT only to be sure the suspension took. The block written is one the
// thread does not use yet, so the thread keeps running: the count is kept and
// GetThreadContext answers without registers.
extern "C" uint32_t win_SuspendThread(void* handle) {
    auto thread=std::dynamic_pointer_cast<Thread>(resolveHandle(handle));
    if(!thread){setLastError(ErrorInvalidHandle);return ~0u;}
    static std::atomic<bool> said{false};
    if(!said.exchange(true))log("SuspendThread: thread "+std::to_string(thread->id)+" segue rodando (so a contagem e mantida)");
    std::lock_guard<std::mutex> lock(threadsLock);return thread->suspends++;
}
extern "C" int32_t win_GetThreadContext(void* handle,void* context) {
    if(!std::dynamic_pointer_cast<Thread>(resolveHandle(handle))){setLastError(ErrorInvalidHandle);return 0;}
    if(!context){setLastError(ErrorInvalidParameter);return 0;}
    return 1;
}
// CreateToolhelp32Snapshot with TH32CS_SNAPTHREAD: the running engine threads.
namespace {
struct ThreadSnapshot:Object {std::vector<uint32_t> ids;size_t next=0;};
// THREADENTRY32: dwSize, cntUsage, th32ThreadID, th32OwnerProcessID, tpBasePri, tpDeltaPri, dwFlags.
int32_t snapshotEntry(void* handle,void* entry,bool first) {
    auto snapshot=std::dynamic_pointer_cast<ThreadSnapshot>(findHandle(handle));
    if(!snapshot){setLastError(ErrorInvalidHandle);return 0;}
    uint32_t size=0;if(entry)std::memcpy(&size,entry,4);
    if(size<28){setLastError(ErrorInvalidParameter);return 0;}
    if(first)snapshot->next=0;
    if(snapshot->next>=snapshot->ids.size()){setLastError(ErrorNoMoreFiles);return 0;}
    uint32_t fields[7]={28,0,snapshot->ids[snapshot->next++],win_GetCurrentProcessId(),8,0,0};
    std::memcpy(entry,fields,sizeof fields);
    return 1;
}
}
extern "C" void* win_CreateToolhelp32Snapshot(uint32_t flags,uint32_t) {
    constexpr uint32_t SnapThread=0x4;
    if(!(flags&SnapThread)) {
        log("CreateToolhelp32Snapshot: flags "+hex(flags)+" sem TH32CS_SNAPTHREAD ainda sem implementacao");
        setLastError(ErrorNotSupported);return reinterpret_cast<void*>(intptr_t(-1));
    }
    auto snapshot=std::make_shared<ThreadSnapshot>();snapshot->ids=azimuth::guestThreadIds();
    return addHandle(snapshot);
}
extern "C" int32_t win_Thread32First(void* snapshot,void* entry){return snapshotEntry(snapshot,entry,true);}
extern "C" int32_t win_Thread32Next(void* snapshot,void* entry){return snapshotEntry(snapshot,entry,false);}
// ThreadBasicInformation (THREAD_BASIC_INFORMATION: ExitStatus, TebBaseAddress,
// ClientId, AffinityMask, Priority, BasePriority), which 1.0.2 asks for the TEB.
extern "C" int32_t win_NtQueryInformationThread(void* handle,uint32_t kind,void* information,uint32_t size,uint32_t* returned) {
    constexpr int32_t InvalidHandle=int32_t(0xc0000008),InvalidInfoClass=int32_t(0xc0000003),LengthMismatch=int32_t(0xc0000004);
    auto thread=std::dynamic_pointer_cast<Thread>(resolveHandle(handle));
    if(!thread)return InvalidHandle;
    if(kind!=0){log("NtQueryInformationThread: classe "+std::to_string(kind)+" ainda sem implementacao");return InvalidInfoClass;}
    if(size<48||!information)return LengthMismatch;
    uint8_t out[48]{};
    uint32_t status;{std::lock_guard<std::mutex> lock(waitLock);status=thread->finished?thread->code:StillActive;}
    void* teb=azimuth::guestThreadTeb(thread->id);
    uint64_t process=win_GetCurrentProcessId(),id=thread->id,affinity=~uint64_t(0);int32_t priority=8;
    std::memcpy(out,&status,4);std::memcpy(out+8,&teb,8);std::memcpy(out+16,&process,8);std::memcpy(out+24,&id,8);
    std::memcpy(out+32,&affinity,8);std::memcpy(out+40,&priority,4);std::memcpy(out+44,&priority,4);
    std::memcpy(information,out,48);
    if(returned)*returned=48;
    return 0;
}
// Priorities and affinities are the host's (platform::shareCpu): accepted, not applied.
extern "C" int32_t win_SetThreadPriority(void*,int32_t){return 1;}
extern "C" int32_t win_GetThreadPriority(void*){return 0;}   // THREAD_PRIORITY_NORMAL
extern "C" int32_t win_SetThreadStackGuarantee(uint32_t* size){if(size)*size=0;return 1;}
// FILETIMEs: creation, exit, kernel and user time. Only the calling thread's
// CPU time is known; it counts as user time.
extern "C" int32_t win_GetThreadTimes(void* handle,int64_t* creation,int64_t* exit,int64_t* kernel,int64_t* user) {
    if(!creation||!exit||!kernel||!user){setLastError(ErrorInvalidParameter);return 0;}
    *creation=*exit=*kernel=*user=0;
    if(handle==reinterpret_cast<void*>(intptr_t(-2))) {   // GetCurrentThread()
        timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);
        *user=(int64_t(t.tv_sec)*1000000000+t.tv_nsec)/100;
    }
    return 1;
}
extern "C" void* win_CreateSemaphoreExW(void* attributes,int32_t initial,int32_t maximum,const uint16_t* name,uint32_t,uint32_t) {
    if(name&&*name)log("CreateSemaphoreExW com nome ignora o nome: "+utf8(name,length(name)));
    return win_CreateSemaphoreA(attributes,initial,maximum,nullptr);
}
// Signals an event or a semaphore, then waits for the other object.
extern "C" uint32_t win_SignalObjectAndWait(void* toSignal,void* toWait,uint32_t milliseconds,int32_t alertable) {
    if(std::dynamic_pointer_cast<Event>(findHandle(toSignal)))win_SetEvent(toSignal);
    else if(!win_ReleaseSemaphore(toSignal,1,nullptr)){setLastError(ErrorInvalidHandle);return WaitFailed;}
    return win_WaitForSingleObjectEx(toWait,milliseconds,alertable);
}
extern "C" void win_FreeLibraryAndExitThread(void*,uint32_t code){azimuth::exitGuestThread(code);}
extern "C" int32_t win_GetExitCodeThread(void* handle,uint32_t* code) {
    auto thread=std::dynamic_pointer_cast<Thread>(resolveHandle(handle));
    if(!thread||!code){setLastError(ErrorInvalidHandle);return 0;}
    std::lock_guard<std::mutex> lock(waitLock);*code=thread->finished?thread->code:StillActive;return 1;
}
extern "C" uint32_t win_GetThreadId(void* handle) {
    auto thread=std::dynamic_pointer_cast<Thread>(resolveHandle(handle));
    if(!thread){setLastError(ErrorInvalidHandle);return 0;}
    return thread->id;
}
extern "C" uint32_t win_GetProcessIdOfThread(void* handle) {
    if(!std::dynamic_pointer_cast<Thread>(resolveHandle(handle))){setLastError(ErrorInvalidHandle);return 0;}
    return win_GetCurrentProcessId();
}
extern "C" uint32_t win_GetProcessId(void* process) {
    if(reinterpret_cast<intptr_t>(process)!=-1){setLastError(ErrorInvalidHandle);return 0;}
    return win_GetCurrentProcessId();
}
extern "C" void* win_OpenThread(uint32_t,int32_t,uint32_t id) {
    std::shared_ptr<Thread> thread;
    {
        std::lock_guard<std::mutex> lock(threadsLock);auto found=threadsById.find(id);if(found!=threadsById.end())thread=found->second.lock();
        // A running engine thread with no object yet (the main thread before
        // it asks for its own): its object is made here.
        if(!thread&&azimuth::guestThreadTeb(id)){thread=std::make_shared<Thread>();thread->id=id;threadsById[id]=thread;}
    }
    if(!thread){setLastError(ErrorInvalidParameter);return nullptr;}
    return addHandle(thread);
}
extern "C" int32_t win_DuplicateHandle(void* sourceProcess,void* source,void* targetProcess,void** target,uint32_t,int32_t,uint32_t options) {
    constexpr uint32_t CloseSource=1;
    if(reinterpret_cast<intptr_t>(sourceProcess)!=-1||reinterpret_cast<intptr_t>(targetProcess)!=-1||!target){setLastError(ErrorInvalidParameter);return 0;}
    if(reinterpret_cast<intptr_t>(source)==-1){*target=source;return 1;}   // the current process
    auto object=resolveHandle(source);
    if(!object){setLastError(ErrorInvalidHandle);return 0;}
    *target=addHandle(object);
    if(options&CloseSource)removeHandle(source);
    return 1;
}
extern "C" int32_t win_SetThreadDescription(void* handle,const uint16_t* description) {
    auto thread=std::dynamic_pointer_cast<Thread>(resolveHandle(handle));
    if(!thread)return int32_t(0x80070006);   // HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)
    thread->description=fromGuest(description);
    log("Thread do motor "+std::to_string(thread->id)+": "+utf8(description,length(description)));
    return 0;
}
extern "C" int32_t win_GetThreadDescription(void* handle,uint16_t** description) {
    auto thread=std::dynamic_pointer_cast<Thread>(resolveHandle(handle));
    if(!thread||!description)return int32_t(0x80070006);
    auto bytes=(thread->description.size()+1)*2;
    auto copy=static_cast<uint16_t*>(win_HeapAlloc(win_GetProcessHeap(),0,bytes));   // freed with LocalFree
    if(!copy)return int32_t(0x8007000e);   // E_OUTOFMEMORY
    std::memcpy(copy,thread->description.c_str(),bytes);*description=copy;return 0;
}
void reportSpin(const char* call);
extern "C" void win_Sleep(uint32_t milliseconds){if(!milliseconds)reportSpin("Sleep(0)");sleepFor(milliseconds);}
extern "C" uint32_t win_SleepEx(uint32_t milliseconds,int32_t){sleepFor(milliseconds);return 0;}
// NtDelayExecution: a negative interval is relative, in 100 ns units (vkd3d-proton's
// swap chain paces frames with it, title 0.93); an absolute one is not used here.
extern "C" int32_t win_NtDelayExecution(uint8_t,const int64_t* interval) {
    if(!interval)return int32_t(0xc000000d);   // STATUS_INVALID_PARAMETER
    if(*interval>=0){sched_yield();return 0;}
    uint64_t micros=uint64_t(-*interval)/10;
    if(micros>=100000)sleepFor(uint32_t(micros/1000));
    else if(micros)usleep(useconds_t(micros));
    else sched_yield();
    return 0;
}
// A thread that keeps yielding is spinning on something: reported (with its
// code addresses) at 100 000 yields and at every tenfold after, up to 40
// reports (title 0.87: the game's init thread spun forever this way).
void reportSpin(const char* call) {
    thread_local uint64_t yields=0,next=100000;
    if(++yields<next)return;
    next*=10;
    static std::atomic<unsigned> reports{0};
    if(reports++<40)log(std::string("Giro na thread ")+std::to_string(azimuth::currentGuestThreadId())+": "+std::to_string(yields)+" chamadas de "+call+"; chamado de"+codeCallers());
}
extern "C" int32_t win_SwitchToThread(void){reportSpin("SwitchToThread");sched_yield();return 1;}

// ---- Waits, events, timers ---------------------------------------------
extern "C" uint32_t win_WaitForMultipleObjects(uint32_t count,void* const* handles,int32_t all,uint32_t milliseconds) {
    if(!count||count>64||!handles){setLastError(ErrorInvalidParameter);return WaitFailed;}
    std::vector<std::shared_ptr<Waitable>> objects;
    objects.reserve(count);
    for(uint32_t n=0;n<count;++n) {
        auto object=std::dynamic_pointer_cast<Waitable>(resolveHandle(handles[n]));
        if(!object){setLastError(ErrorInvalidHandle);return WaitFailed;}
        objects.push_back(object);
    }
    return waitFor(objects,all!=0,milliseconds);
}
extern "C" uint32_t win_WaitForSingleObject(void* handle,uint32_t milliseconds){
    auto object=std::dynamic_pointer_cast<Waitable>(resolveHandle(handle));
    if(!object){setLastError(ErrorInvalidHandle);return WaitFailed;}
    // Polls and already-signaled waits never need a heap allocation.
    return waitFor(std::array<std::shared_ptr<Waitable>,1>{std::move(object)},false,milliseconds);
}
extern "C" uint32_t win_WaitForSingleObjectEx(void* handle,uint32_t milliseconds,int32_t){return win_WaitForSingleObject(handle,milliseconds);}
extern "C" void* win_CreateEventW(void*,int32_t manualReset,int32_t initialState,const uint16_t* name) {
    if(name&&*name)log("CreateEventW com nome ignora o nome: "+utf8(name,length(name)));
    auto event=std::make_shared<Event>();event->manual=manualReset;event->set=initialState;event->origin=originHere();
    setLastError(ErrorSuccess);return addHandle(event);
}
extern "C" int32_t win_SetEvent(void* handle) {
    auto event=std::dynamic_pointer_cast<Event>(findHandle(handle));
    if(!event){setLastError(ErrorInvalidHandle);return 0;}
    std::lock_guard<std::mutex> lock(waitLock);event->set=true;
    notifyWaitersLocked(event.get());return 1;
}
extern "C" void* win_CreateWaitableTimerExW(void*,const uint16_t*,uint32_t flags,uint32_t) {
    constexpr uint32_t ManualReset=0x1;
    auto timer=std::make_shared<Timer>();timer->manual=flags&ManualReset;
    setLastError(ErrorSuccess);return addHandle(timer);
}
extern "C" int32_t win_SetWaitableTimerEx(void* handle,const int64_t* due,int32_t period,void* completion,void*,void*,uint32_t) {
    auto timer=std::dynamic_pointer_cast<Timer>(findHandle(handle));
    if(!timer||!due){setLastError(timer?ErrorInvalidParameter:ErrorInvalidHandle);return 0;}
    if(completion){log("SetWaitableTimer com rotina de conclusao ainda sem implementacao");setLastError(ErrorNotSupported);return 0;}
    // Negative: relative, in 100 ns units. Positive: an absolute FILETIME.
    Clock::time_point at;
    if(*due<0)at=Clock::now()+std::chrono::microseconds(-*due/10);
    else {
        int64_t now;win_GetSystemTimeAsFileTime(&now);
        at=Clock::now()+std::chrono::microseconds(*due>now?(*due-now)/10:0);
    }
    std::lock_guard<std::mutex> lock(waitLock);timer->set=false;timer->at=at;timer->period=std::chrono::milliseconds(period>0?period:0);
    notifyWaitersLocked(timer.get());return 1;
}
extern "C" int32_t win_SetWaitableTimer(void* handle,const int64_t* due,int32_t period,void* completion,void* argument,int32_t) {
    return win_SetWaitableTimerEx(handle,due,period,completion,argument,nullptr,0);
}
extern "C" int32_t win_CancelWaitableTimer(void* handle) {
    auto timer=std::dynamic_pointer_cast<Timer>(findHandle(handle));
    if(!timer){setLastError(ErrorInvalidHandle);return 0;}
    std::lock_guard<std::mutex> lock(waitLock);timer->at=Clock::time_point::max();return 1;
}

// ---- Condition variables, one-time initialization, WaitOnAddress -------
namespace {
// CONDITION_VARIABLE is one pointer-sized word, zero when initialized. Since
// title 1.14 the word is a generation count, changed under one of 64 shared
// mutex and condition pairs chosen by its address, with no kernel object per
// variable (a mutex and condition each, never freed, drew on libkernel's
// internal pool). A wake wakes every sleeper of the pair, which Windows allows.
// A wake with no sleeper on the pair changes the word only (title 1.26: an
// engine thread woke a variable nobody slept on thousands of times a second,
// each paying the pair's lock and a notify, ~6.5 us).
struct Condition {std::mutex lock;std::condition_variable changed;std::atomic<uint32_t> sleepers{0};};
Condition conditions[64];
Condition* condition(void* variable){return &conditions[(reinterpret_cast<uintptr_t>(variable)>>3)%64];}
uint64_t& generationOf(void* variable){return *static_cast<uint64_t*>(variable);}
std::mutex onceLock;std::condition_variable onceChanged;
// WaitOnAddress sleeps on one of 64 pairs chosen by the address, and a wake
// wakes that pair only (title 1.19: one pair for the process woke every
// waiter on each of 100 000 WakeByAddressSingle per second).
struct AddressBucket {std::mutex lock;std::condition_variable changed;std::atomic<uint32_t> sleepers{0};};
AddressBucket addressBuckets[64];
AddressBucket& bucketOf(const void* address){return addressBuckets[(reinterpret_cast<uintptr_t>(address)>>3)%64];}
}
// The sleeper counted before it reads the word, and a waker changes the word
// before it reads the count: either the waker sees the sleeper, or the
// sleeper sees the new word.
struct Sleeping {
    std::atomic<uint32_t>& count;
    explicit Sleeping(std::atomic<uint32_t>& c):count(c){count.fetch_add(1,std::memory_order_seq_cst);}
    ~Sleeping(){count.fetch_sub(1,std::memory_order_seq_cst);}
};
uint64_t loadGeneration(void* variable){return __atomic_load_n(&generationOf(variable),__ATOMIC_SEQ_CST);}
extern "C" int32_t win_SleepConditionVariableSRW(void* variable,void* srw,uint32_t milliseconds,uint32_t flags) {
    constexpr uint32_t Shared=0x1;
    auto c=condition(variable);auto deadline=deadlineAfter(milliseconds);bool woken=false;
    {
        std::unique_lock<std::mutex> lock(c->lock);Sleeping sleeping(c->sleepers);auto generation=loadGeneration(variable);
        if(flags&Shared)win_ReleaseSRWLockShared(srw);else win_ReleaseSRWLockExclusive(srw);
        for(;;) {
            if(loadGeneration(variable)!=generation){woken=true;break;}
            if(azimuth::runStopping()||Clock::now()>=deadline)break;
            c->changed.wait_until(lock,std::min(deadline,Clock::now()+Slice));
        }
    }
    azimuth::leaveIfStopping();
    if(flags&Shared)win_AcquireSRWLockShared(srw);else win_AcquireSRWLockExclusive(srw);
    if(!woken){setLastError(ErrorTimeout);return 0;}
    return 1;
}
extern "C" void win_WakeConditionVariable(void* variable) {
    auto c=condition(variable);
    __atomic_add_fetch(&generationOf(variable),1,__ATOMIC_SEQ_CST);
    if(!c->sleepers.load(std::memory_order_seq_cst))return;
    {std::lock_guard<std::mutex> lock(c->lock);}   // a sleeper between its check and its wait gets there first
    c->changed.notify_all();
}
extern "C" void win_WakeAllConditionVariable(void* variable){win_WakeConditionVariable(variable);}
// INIT_ONCE: one pointer. 0 = not started, 1 = in progress, otherwise the
// context with bit 1 set (the low two bits are Windows' state bits).
extern "C" int32_t win_InitOnceBeginInitialize(void* once,uint32_t flags,int32_t* pending,void** context) {
    constexpr uint32_t CheckOnly=0x1,Async=0x2;
    if(flags&Async){log("InitOnce assincrono ainda sem implementacao");setLastError(ErrorNotSupported);return 0;}
    auto slot=static_cast<uintptr_t*>(once);
    std::unique_lock<std::mutex> lock(onceLock);
    for(;;) {
        if(*slot>=2){if(pending)*pending=0;if(context)*context=reinterpret_cast<void*>(*slot&~uintptr_t(3));return 1;}
        if(flags&CheckOnly){setLastError(ErrorInvalidParameter);return 0;}   // not yet complete
        if(*slot==0){*slot=1;if(pending)*pending=1;return 1;}
        if(azimuth::runStopping()){lock.unlock();azimuth::leaveIfStopping();lock.lock();}
        onceChanged.wait_for(lock,Slice);
    }
}
extern "C" int32_t win_InitOnceComplete(void* once,uint32_t flags,void* context) {
    constexpr uint32_t InitFailed=0x4;
    auto slot=static_cast<uintptr_t*>(once);
    {
        std::lock_guard<std::mutex> lock(onceLock);
        if(*slot!=1){setLastError(ErrorInvalidParameter);return 0;}
        *slot=(flags&InitFailed)?0:(reinterpret_cast<uintptr_t>(context)&~uintptr_t(3))|2;
    }
    onceChanged.notify_all();return 1;
}
extern "C" int32_t win_WaitOnAddress(void* address,void* compare,uint64_t size,uint32_t milliseconds) {
    if(!address||!compare||(size!=1&&size!=2&&size!=4&&size!=8)){setLastError(ErrorInvalidParameter);return 0;}
    auto deadline=deadlineAfter(milliseconds);
    auto& bucket=bucketOf(address);
    std::unique_lock<std::mutex> lock(bucket.lock);
    Sleeping sleeping(bucket.sleepers);
    while(std::memcmp(address,compare,size)==0) {
        if(azimuth::runStopping()){lock.unlock();azimuth::leaveIfStopping();lock.lock();}
        if(Clock::now()>=deadline){setLastError(ErrorTimeout);return 0;}
        bucket.changed.wait_until(lock,std::min(deadline,Clock::now()+Slice));
    }
    return 1;
}
extern "C" void win_WakeByAddressSingle(void* address){
    auto& bucket=bucketOf(address);
    // Waiter registers before checking memory; no notification is needed if
    // no waiter exists. Keep the bucket lock to bridge predicate-check/wait.
    if(!bucket.sleepers.load(std::memory_order_seq_cst))return;
    std::lock_guard<std::mutex> lock(bucket.lock);bucket.changed.notify_all();
}
extern "C" void win_WakeByAddressAll(void* address){win_WakeByAddressSingle(address);}

// ---- Thread pool, system information -----------------------------------
// Each simple callback runs on an engine thread of its own.
extern "C" int32_t win_TrySubmitThreadpoolCallback(void* callback,void* context,void* environment) {
    if(!callback){setLastError(ErrorInvalidParameter);return 0;}
    if(environment)log("TrySubmitThreadpoolCallback com ambiente: ambiente ignorado");
    auto thread=newThread();
    if(!spawn(std::unique_ptr<Start>(new Start{thread,callback,nullptr,1u<<20,true,context}))){setLastError(ErrorNotEnoughMemory);return 0;}
    return 1;
}
extern "C" void win_FlushProcessWriteBuffers(void){std::atomic_thread_fence(std::memory_order_seq_cst);}
namespace {
uint32_t processors(){long n=sysconf(_SC_NPROCESSORS_ONLN);return n>0&&n<=64?uint32_t(n):8;}
}
// SYSTEM_INFO of an x64 Windows on the PS5's Zen 2 (family 0x17).
extern "C" void win_GetNativeSystemInfo(void* info) {
    auto p=static_cast<uint8_t*>(info);std::memset(p,0,48);
    uint16_t architecture=9;std::memcpy(p,&architecture,2);
    uint32_t page=4096;std::memcpy(p+4,&page,4);
    uint64_t minimum=0x10000,maximum=0x7ffffffeffff,mask=(uint64_t(1)<<processors())-1;
    std::memcpy(p+8,&minimum,8);std::memcpy(p+16,&maximum,8);std::memcpy(p+24,&mask,8);
    uint32_t count=processors(),type=8664,granularity=65536;
    std::memcpy(p+32,&count,4);std::memcpy(p+36,&type,4);std::memcpy(p+40,&granularity,4);
    uint16_t level=0x17,revision=0x7100;std::memcpy(p+44,&level,2);std::memcpy(p+46,&revision,2);
}
extern "C" void win_GetSystemInfo(void* info){win_GetNativeSystemInfo(info);}
// SYSTEM_LOGICAL_PROCESSOR_INFORMATION (32 bytes each): one core per
// processor, one package and one NUMA node holding them all.
extern "C" int32_t win_GetLogicalProcessorInformation(void* buffer,uint32_t* length) {
    constexpr uint32_t RelationProcessorCore=0,RelationNumaNode=1,RelationProcessorPackage=3,Entry=32;
    if(!length){setLastError(ErrorInvalidParameter);return 0;}
    uint32_t count=processors(),need=(count+2)*Entry;
    if(!buffer||*length<need){*length=need;setLastError(ErrorInsufficientBuffer);return 0;}
    auto p=static_cast<uint8_t*>(buffer);std::memset(p,0,need);
    uint64_t all=(uint64_t(1)<<count)-1;
    for(uint32_t n=0;n<count;++n){uint64_t mask=uint64_t(1)<<n;std::memcpy(p+n*Entry,&mask,8);std::memcpy(p+n*Entry+8,&RelationProcessorCore,4);}
    std::memcpy(p+count*Entry,&all,8);std::memcpy(p+count*Entry+8,&RelationProcessorPackage,4);
    std::memcpy(p+(count+1)*Entry,&all,8);std::memcpy(p+(count+1)*Entry+8,&RelationNumaNode,4);
    *length=need;return 1;
}
extern "C" int32_t win_GetNumaHighestNodeNumber(uint32_t* node){if(!node){setLastError(ErrorInvalidParameter);return 0;}*node=0;return 1;}
extern "C" uint32_t win_GetCurrentProcessorNumber(void){return 0;}
extern "C" uint64_t win_SetThreadAffinityMask(void*,uint64_t mask){return mask?(uint64_t(1)<<processors())-1:0;}
extern "C" int32_t win_SetProcessAffinityMask(void*,uint64_t){return 1;}
extern "C" int32_t win_GetProcessAffinityMask(void* process,uint64_t* processMask,uint64_t* systemMask) {
    if(reinterpret_cast<intptr_t>(process)!=-1||!processMask||!systemMask){setLastError(ErrorInvalidParameter);return 0;}
    *processMask=*systemMask=(uint64_t(1)<<processors())-1;return 1;
}
// MEMORYSTATUSEX: physical memory as the host reports it, a 128 TiB address space.
extern "C" int32_t win_GlobalMemoryStatusEx(void* status) {
    auto p=static_cast<uint8_t*>(status);uint32_t size;std::memcpy(&size,p,4);
    if(size!=64){setLastError(ErrorInvalidParameter);return 0;}
    long pages=sysconf(_SC_PHYS_PAGES),pageSize=sysconf(_SC_PAGESIZE);
    uint64_t total=pages>0&&pageSize>0?uint64_t(pages)*uint64_t(pageSize):(uint64_t(16)<<30);
    uint64_t available=total/2;
    uint32_t load=50;std::memcpy(p+4,&load,4);
    uint64_t values[7]={total,available,total,available,uint64_t(0x7ffffffe0000),uint64_t(0x7ffffffe0000)-(uint64_t(4)<<30),0};
    std::memcpy(p+8,values,sizeof values);return 1;
}

// ---- For the DLLs the host loads (vkd3d-proton) -------------------------
namespace {
struct Semaphore:Waitable {
    int32_t count=0,maximum=1;
    bool signaled(Clock::time_point) override{return count>0;}
    void consume() override{--count;}
    std::string describe() override{return "semaforo "+std::to_string(count)+"/"+std::to_string(maximum);}
};
}
namespace {
// A mutex: free, or owned by one thread, which may take it again (recursion).
struct Mutex:Waitable {
    uint32_t owner=0,depth=0;
    bool signaled(Clock::time_point) override{return !owner||owner==win_GetCurrentThreadId();}
    void consume() override{owner=win_GetCurrentThreadId();++depth;}
    std::string describe() override{return "mutex com dono "+std::to_string(owner);}
};
void* newMutex(int32_t initialOwner) {
    auto mutex=std::make_shared<Mutex>();mutex->origin=originHere();
    if(initialOwner){mutex->owner=win_GetCurrentThreadId();mutex->depth=1;}
    setLastError(ErrorSuccess);return addHandle(mutex);
}
}
extern "C" void* win_CreateMutexW(void*,int32_t initialOwner,const uint16_t* name) {
    if(name&&*name)log("CreateMutexW com nome ignora o nome: "+utf8(name,length(name)));
    return newMutex(initialOwner);
}
extern "C" void* win_CreateMutexA(void*,int32_t initialOwner,const char* name) {
    if(name&&*name)log(std::string("CreateMutexA com nome ignora o nome: ")+name);
    return newMutex(initialOwner);
}
extern "C" int32_t win_ReleaseMutex(void* handle) {
    constexpr uint32_t ErrorNotOwner=288;
    auto mutex=std::dynamic_pointer_cast<Mutex>(findHandle(handle));
    if(!mutex){setLastError(ErrorInvalidHandle);return 0;}
    {
        std::lock_guard<std::mutex> lock(waitLock);
        if(mutex->owner!=win_GetCurrentThreadId()){setLastError(ErrorNotOwner);return 0;}
        if(--mutex->depth==0){mutex->owner=0;notifyWaitersLocked(mutex.get());}
    }
    return 1;
}
// CREATE_EVENT_MANUAL_RESET (1) and CREATE_EVENT_INITIAL_SET (2).
extern "C" void* win_CreateEventExW(void* attributes,const uint16_t* name,uint32_t flags,uint32_t) {
    return win_CreateEventW(attributes,flags&1,(flags&2)?1:0,name);
}
extern "C" int32_t win_ResetEvent(void* handle) {
    auto event=std::dynamic_pointer_cast<Event>(findHandle(handle));
    if(!event){setLastError(ErrorInvalidHandle);return 0;}
    std::lock_guard<std::mutex> lock(waitLock);event->set=false;return 1;
}
extern "C" uint32_t win_WaitForMultipleObjectsEx(uint32_t count,void* const* handles,int32_t all,uint32_t milliseconds,int32_t) {
    return win_WaitForMultipleObjects(count,handles,all,milliseconds);
}
extern "C" void* win_CreateEventA(void* attributes,int32_t manualReset,int32_t initialState,const char* name) {
    if(name&&*name)log(std::string("CreateEventA com nome ignora o nome: ")+name);
    return win_CreateEventW(attributes,manualReset,initialState,nullptr);
}
extern "C" void* win_CreateSemaphoreA(void*,int32_t initial,int32_t maximum,const char* name) {
    if(maximum<=0||initial<0||initial>maximum){setLastError(ErrorInvalidParameter);return nullptr;}
    if(name&&*name)log(std::string("CreateSemaphoreA com nome ignora o nome: ")+name);
    auto semaphore=std::make_shared<Semaphore>();semaphore->count=initial;semaphore->maximum=maximum;semaphore->origin=originHere();
    setLastError(ErrorSuccess);return addHandle(semaphore);
}
extern "C" int32_t win_ReleaseSemaphore(void* handle,int32_t release,int32_t* previous) {
    constexpr uint32_t ErrorTooManyPosts=298;
    auto semaphore=std::dynamic_pointer_cast<Semaphore>(findHandle(handle));
    if(!semaphore){setLastError(ErrorInvalidHandle);return 0;}
    {
        std::lock_guard<std::mutex> lock(waitLock);
        if(release<=0||semaphore->count>semaphore->maximum-release){setLastError(release<=0?ErrorInvalidParameter:ErrorTooManyPosts);return 0;}
        if(previous)*previous=semaphore->count;
        semaphore->count+=release;notifyWaitersLocked(semaphore.get());
    }
    return 1;
}
extern "C" void win_InitializeConditionVariable(void* variable){*static_cast<void**>(variable)=nullptr;}
// As SleepConditionVariableSRW, over a critical section held once.
extern "C" int32_t win_SleepConditionVariableCS(void* variable,void* section,uint32_t milliseconds) {
    auto c=condition(variable);auto deadline=deadlineAfter(milliseconds);bool woken=false;
    {
        std::unique_lock<std::mutex> lock(c->lock);Sleeping sleeping(c->sleepers);auto generation=loadGeneration(variable);
        win_LeaveCriticalSection(section);
        for(;;) {
            if(loadGeneration(variable)!=generation){woken=true;break;}
            if(azimuth::runStopping()||Clock::now()>=deadline)break;
            c->changed.wait_until(lock,std::min(deadline,Clock::now()+Slice));
        }
    }
    azimuth::leaveIfStopping();
    win_EnterCriticalSection(section);
    if(!woken){setLastError(ErrorTimeout);return 0;}
    return 1;
}
// The callback is Windows code (PINIT_ONCE_FN), run once by the first caller.
extern "C" int32_t win_InitOnceExecuteOnce(void* once,void* function,void* parameter,void** context) {
    constexpr uint32_t InitFailed=0x4;
    int32_t pending=0;void* existing=nullptr;
    if(!win_InitOnceBeginInitialize(once,0,&pending,&existing))return 0;
    if(!pending){if(context)*context=existing;return 1;}
    void* result=nullptr;
    int32_t ok=guestCallInitOnce(function,once,parameter,&result);
    win_InitOnceComplete(once,ok?0:InitFailed,result);
    if(ok&&context)*context=result;
    return ok;
}

// I/O completion ports are not offered: no port was ever created, so nothing
// ever completes. A wait for a completion simply waits out its time (in
// slices that end once the run is stopping).
// I/O completion ports as queues of packets (thread pools use them so; no
// file I/O completes through them in the host).
namespace {
struct CompletionPort:Object {
    struct Packet {uint32_t bytes;uint64_t key;void* overlapped;};
    std::deque<Packet> packets;
};
}
// With a file handle the port (existing, or a new one) receives that file's
// completions with the key (title 1.16; before, the binding was dropped).
extern "C" void* win_CreateIoCompletionPort(void* file,void* existing,uint64_t key,uint32_t) {
    void* port=existing;
    if(port&&!std::dynamic_pointer_cast<CompletionPort>(findHandle(port))){setLastError(ErrorInvalidHandle);return nullptr;}
    if(!port)port=addHandle(std::make_shared<CompletionPort>());
    if(reinterpret_cast<uintptr_t>(file)!=InvalidHandle) {
        if(!bindCompletionPort(file,port,key)){setLastError(ErrorInvalidHandle);return nullptr;}
        static std::atomic<unsigned> bound{0};
        if(bound++<8)log("CreateIoCompletionPort: arquivo "+hex(reinterpret_cast<uint64_t>(file))+" ligado a porta "+hex(reinterpret_cast<uint64_t>(port))+", chave "+hex(key));
    }
    setLastError(ErrorSuccess);return port;
}
extern "C" int32_t win_PostQueuedCompletionStatus(void* handle,uint32_t bytes,uint64_t key,void* overlapped) {
    auto port=std::dynamic_pointer_cast<CompletionPort>(findHandle(handle));
    if(!port){setLastError(ErrorInvalidHandle);return 0;}
    std::lock_guard<std::mutex> lock(waitLock);port->packets.push_back({bytes,key,overlapped});
    notifyWaitersLocked(port.get());return 1;
}
extern "C" int32_t win_GetQueuedCompletionStatus(void* handle,uint32_t* bytes,uint64_t* key,void** overlapped,uint32_t milliseconds) {
    if(bytes)*bytes=0;
    if(key)*key=0;
    if(overlapped)*overlapped=nullptr;
    auto port=std::dynamic_pointer_cast<CompletionPort>(findHandle(handle));
    if(!port){sleepFor(milliseconds);setLastError(WaitTimeout);return 0;}
    auto deadline=deadlineAfter(milliseconds);
    std::unique_lock<std::mutex> lock(waitLock);
    for(;;) {
        if(!port->packets.empty()) {
            auto packet=port->packets.front();port->packets.pop_front();
            if(bytes)*bytes=packet.bytes;
            if(key)*key=packet.key;
            if(overlapped)*overlapped=packet.overlapped;
            return 1;
        }
        if(azimuth::runStopping()){lock.unlock();azimuth::leaveIfStopping();lock.lock();}
        if(Clock::now()>=deadline){setLastError(WaitTimeout);return 0;}
        auto step=std::min(deadline,Clock::now()+Slice);
        Waiting waiting({static_cast<const Object*>(port.get())},false);
        waiting.waiter.wake.wait_until(lock,step);
    }
}

// ---- ConcRT's scheduler (title 1.10) ------------------------------------
// msvcr110's resource manager asks GetProcAddress for the processor-group
// functions and throws scheduler_resource_allocation_error without them.
// GROUP_AFFINITY: Mask, Group, Reserved[3]. One group holds every processor.
extern "C" int32_t win_GetThreadGroupAffinity(void*,void* affinity) {
    if(!affinity){setLastError(ErrorInvalidParameter);return 0;}
    auto p=static_cast<uint8_t*>(affinity);std::memset(p,0,16);
    uint64_t all=(uint64_t(1)<<processors())-1;std::memcpy(p,&all,8);
    return 1;
}
extern "C" int32_t win_SetThreadGroupAffinity(void* thread,const void* affinity,void* previous) {
    if(!affinity){setLastError(ErrorInvalidParameter);return 0;}
    if(previous)win_GetThreadGroupAffinity(thread,previous);
    return 1;
}
// PROCESSOR_NUMBER: Group, Number, Reserved.
extern "C" void win_GetCurrentProcessorNumberEx(void* number){if(number)std::memset(number,0,4);}
// SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX records, as GetLogicalProcessorInformation
// describes the machine: a core per processor, one package, one NUMA node, one group.
extern "C" int32_t win_GetLogicalProcessorInformationEx(uint32_t relation,void* buffer,uint32_t* length) {
    constexpr uint32_t Core=0,Numa=1,Package=3,Group=4,All=0xffff;
    if(!length){setLastError(ErrorInvalidParameter);return 0;}
    uint32_t count=processors();uint64_t all=(uint64_t(1)<<count)-1;
    std::vector<uint8_t> out;
    auto record=[&](uint32_t kind,uint32_t size)->uint8_t* {
        size_t at=out.size();out.resize(at+size,0);
        std::memcpy(&out[at],&kind,4);std::memcpy(&out[at+4],&size,4);
        return &out[at+8];
    };
    // PROCESSOR_RELATIONSHIP: Flags, EfficiencyClass, Reserved[20], GroupCount, GroupMask[1] at 24.
    auto processorRecord=[&](uint32_t kind,uint64_t mask){auto p=record(kind,48);uint16_t groups=1;std::memcpy(p+22,&groups,2);std::memcpy(p+24,&mask,8);};
    if(relation==Core||relation==All)for(uint32_t n=0;n<count;++n)processorRecord(Core,uint64_t(1)<<n);
    if(relation==Package||relation==All)processorRecord(Package,all);
    // NUMA_NODE_RELATIONSHIP: NodeNumber, Reserved[20], GroupMask at 24.
    if(relation==Numa||relation==All){auto p=record(Numa,48);std::memcpy(p+24,&all,8);}
    // GROUP_RELATIONSHIP: MaximumGroupCount, ActiveGroupCount, Reserved[20], GroupInfo[1] at 24
    // (MaximumProcessorCount, ActiveProcessorCount, Reserved[38], ActiveProcessorMask at 40).
    if(relation==Group||relation==All) {
        auto p=record(Group,80);uint16_t groups=1;std::memcpy(p,&groups,2);std::memcpy(p+2,&groups,2);
        p[24]=uint8_t(count);p[25]=uint8_t(count);std::memcpy(p+64,&all,8);
    }
    if(out.empty()){setLastError(ErrorInvalidParameter);return 0;}
    if(!buffer||*length<out.size()){*length=uint32_t(out.size());setLastError(ErrorInsufficientBuffer);return 0;}
    std::memcpy(buffer,out.data(),out.size());*length=uint32_t(out.size());
    return 1;
}

// Thread pool waits and timers, and the older RegisterWaitForSingleObject and
// timer queue forms of both. Each object has an engine thread of its own that
// waits for it to be armed, then for its handle or its due time, and calls the
// callback there.
namespace {
struct PoolObject {
    enum Kind {Wait,Timer,LegacyWait,LegacyTimer,Work,Io} kind;
    void* callback;void* context;
    bool once=false;               // WT_EXECUTEONLYONCE
    std::mutex lock;std::condition_variable changed;
    bool closed=false,armed=false;
    uint64_t generation=0;         // bumped by each Set, so a stale wait is dropped
    std::shared_ptr<Waitable> target;
    Clock::time_point due=Clock::time_point::max();
    uint32_t period=0;             // ms
    unsigned running=0;uint32_t threadId=0;uint64_t pendingWork=0;
    void* completion=nullptr;      // UnregisterWaitEx/DeleteTimerQueueTimer event, set when the thread ends
    std::shared_ptr<CompletionPort> port;   // TP_IO: the file's completions
    unsigned ioRunning=0;          // TP_IO: a packet taken, its callback not yet returned (under waitLock)
};
thread_local PoolObject* activePool=nullptr;
std::mutex poolLock;
std::map<PoolObject*,std::shared_ptr<PoolObject>> poolObjects;
std::shared_ptr<PoolObject> poolObject(void* handle) {
    std::lock_guard<std::mutex> guard(poolLock);
    auto found=poolObjects.find(static_cast<PoolObject*>(handle));
    return found==poolObjects.end()?nullptr:found->second;
}
PoolObject* newPoolObject(PoolObject::Kind kind,void* callback,void* context,std::shared_ptr<CompletionPort> port=nullptr) {
    auto object=std::make_shared<PoolObject>();
    object->kind=kind;object->callback=callback;object->context=context;object->port=std::move(port);
    {std::lock_guard<std::mutex> guard(poolLock);poolObjects[object.get()]=object;}
    for(unsigned n=0;n<(kind==PoolObject::Work?4u:1u);++n){
        uint32_t id=0;
        auto thread=win_CreateThread(nullptr,256u<<10,const_cast<void*>(guestPoolThreadStart),object.get(),0,&id);
        if(!thread){
            {std::lock_guard<std::mutex> guard(object->lock);object->closed=true;object->changed.notify_all();}
            std::lock_guard<std::mutex> guard(poolLock);poolObjects.erase(object.get());
            setLastError(ErrorNotEnoughMemory);return nullptr;
        }
        object->threadId=id;win_CloseHandle(thread);
    }
    return object.get();
}
// A FILETIME timeout: null forever, negative relative (100 ns), else absolute.
Clock::time_point dueOf(const int64_t* time) {
    if(!time)return Clock::time_point::max();
    int64_t value=*time;
    if(value<=0)return Clock::now()+std::chrono::microseconds(-value/10);
    int64_t now;win_GetSystemTimeAsFileTime(&now);
    return Clock::now()+std::chrono::microseconds(value>now?(value-now)/10:0);
}
void arm(PoolObject& o,std::shared_ptr<Waitable> target,Clock::time_point due,uint32_t period) {
    std::lock_guard<std::mutex> guard(o.lock);
    o.target=std::move(target);o.due=due;o.period=period;++o.generation;
    o.armed=o.target||due!=Clock::time_point::max();
    o.changed.notify_all();
}
// Waits for the callbacks in flight, unless called from the object's own thread.
void drain(PoolObject& o,bool cancel) {
    std::unique_lock<std::mutex> guard(o.lock);
    if(cancel){o.armed=false;o.pendingWork=0;++o.generation;}
    if(activePool==&o)return;
    o.changed.wait(guard,[&]{return o.running==0&&(o.kind!=PoolObject::Work||o.pendingWork==0);});
}
void close(void* handle,void* completion,bool wait) {
    auto o=poolObject(handle);
    if(!o)return;
    {std::lock_guard<std::mutex> guard(poolLock);poolObjects.erase(o.get());}
    {std::lock_guard<std::mutex> guard(o->lock);o->closed=true;o->armed=false;++o->generation;o->completion=completion;o->changed.notify_all();}
    if(wait)drain(*o,false);
}
// A TP_IO's thread: each packet on its port (a transfer that answered
// ERROR_IO_PENDING) becomes a PTP_WIN32_IO_CALLBACK(instance, context,
// overlapped, IoResult, bytes, io). Only successful transfers post packets.
void ioLoop(PoolObject& o) {
    static char instance;
    auto port=o.port;
    std::unique_lock<std::mutex> lock(waitLock);
    for(;;) {
        if(port->packets.empty()) {
            lock.unlock();
            {std::lock_guard<std::mutex> guard(o.lock);if(o.closed)return;}
            azimuth::leaveIfStopping();
            lock.lock();
            if(!port->packets.empty())continue;
            Waiting waiting({static_cast<const Object*>(port.get())},false);
            waiting.waiter.wake.wait_until(lock,Clock::now()+Slice);
            continue;
        }
        auto packet=port->packets.front();port->packets.pop_front();++o.ioRunning;
        lock.unlock();
        guestCall8(o.callback,uint64_t(&instance),uint64_t(o.context),uint64_t(packet.overlapped),0,packet.bytes,reinterpret_cast<uint64_t>(&o),0,0);
        lock.lock();
        --o.ioRunning;
    }
}
}

extern "C" uint32_t pool_ThreadMain(void* parameter) {
    std::shared_ptr<PoolObject> o;
    {std::lock_guard<std::mutex> guard(poolLock);auto f=poolObjects.find(static_cast<PoolObject*>(parameter));if(f!=poolObjects.end())o=f->second;}
    if(!o)return 0;
    if(o->kind==PoolObject::Io){activePool=o.get();ioLoop(*o);activePool=nullptr;return 0;}
    static char instance;activePool=o.get();
    std::unique_lock<std::mutex> guard(o->lock);
    for(;;) {
        o->changed.wait(guard,[&]{return o->closed||o->armed||o->pendingWork;});
        if(o->closed&&(o->kind!=PoolObject::Work||!o->pendingWork))break;
        if(o->kind==PoolObject::Work){
            --o->pendingWork;++o->running;guard.unlock();
            guestCall8(o->callback,uint64_t(&instance),uint64_t(o->context),reinterpret_cast<uint64_t>(o.get()),0,0,0,0,0);
            guard.lock();--o->running;o->changed.notify_all();continue;
        }
        uint64_t generation=o->generation;
        bool fired=false;uint32_t result=WaitTimeout;
        if(o->target) {
            // The handle, in slices, so a new Set or a close is noticed.
            auto target=o->target;auto due=o->due;
            guard.unlock();
            for(;;) {
                auto now=Clock::now();
                if(now>=due)break;
                auto left=std::chrono::duration_cast<std::chrono::milliseconds>(due-now).count();
                uint32_t slice=uint32_t(std::min<int64_t>(left,100));
                if(waitFor(std::array<std::shared_ptr<Waitable>,1>{target},false,slice)==WaitObject0){result=WaitObject0;break;}
                std::lock_guard<std::mutex> check(o->lock);
                if(o->closed||o->generation!=generation)break;
            }
            guard.lock();
            if(o->closed)break;
            if(o->generation!=generation)continue;
            fired=true;
            if(o->kind==PoolObject::Wait||o->once)o->armed=false;   // a thread pool wait fires once per Set
        }else {
            if(Clock::now()<o->due){o->changed.wait_until(guard,o->due);continue;}
            fired=true;result=WaitTimeout;
            if(o->period)o->due+=std::chrono::milliseconds(o->period);
            else o->armed=false;
        }
        if(!fired)continue;
        ++o->running;
        guard.unlock();
        auto self=reinterpret_cast<uint64_t>(o.get());
        switch(o->kind) {
        case PoolObject::Wait:guestCall8(o->callback,uint64_t(&instance),uint64_t(o->context),self,result,0,0,0,0);break;
        case PoolObject::Timer:guestCall8(o->callback,uint64_t(&instance),uint64_t(o->context),self,0,0,0,0,0);break;
        default:guestCall8(o->callback,uint64_t(o->context),result==WaitTimeout?1:0,0,0,0,0,0,0);break;   // WAITORTIMERCALLBACK
        }
        guard.lock();
        --o->running;o->changed.notify_all();
    }
    void* completion=o->completion;activePool=nullptr;
    guard.unlock();
    if(completion&&reinterpret_cast<intptr_t>(completion)!=-1)win_SetEvent(completion);
    return 0;
}

extern "C" void* win_CreateThreadpoolWait(void* callback,void* context,void*) {
    if(!callback){setLastError(ErrorInvalidParameter);return nullptr;}
    return newPoolObject(PoolObject::Wait,callback,context);
}
extern "C" void win_SetThreadpoolWait(void* wait,void* handle,const int64_t* timeout) {
    auto o=poolObject(wait);
    if(!o)return;
    if(!handle){arm(*o,nullptr,Clock::time_point::max(),0);return;}
    auto target=std::dynamic_pointer_cast<Waitable>(resolveHandle(handle));
    if(!target){log("SetThreadpoolWait: handle "+hex(reinterpret_cast<uint64_t>(handle))+" nao e esperavel");return;}
    arm(*o,target,dueOf(timeout),0);
}
extern "C" void win_WaitForThreadpoolWaitCallbacks(void* wait,int32_t cancel){if(auto o=poolObject(wait))drain(*o,cancel!=0);}
extern "C" void win_CloseThreadpoolWait(void* wait){close(wait,nullptr,false);}
extern "C" void* win_CreateThreadpoolTimer(void* callback,void* context,void*) {
    if(!callback){setLastError(ErrorInvalidParameter);return nullptr;}
    return newPoolObject(PoolObject::Timer,callback,context);
}
extern "C" void win_SetThreadpoolTimer(void* timer,const int64_t* due,uint32_t period,uint32_t) {
    auto o=poolObject(timer);
    if(!o)return;
    arm(*o,nullptr,due?dueOf(due):Clock::time_point::max(),due?period:0);
}
extern "C" int32_t win_IsThreadpoolTimerSet(void* timer) {
    auto o=poolObject(timer);
    if(!o)return 0;
    std::lock_guard<std::mutex> guard(o->lock);return o->armed;
}
extern "C" void win_WaitForThreadpoolTimerCallbacks(void* timer,int32_t cancel){if(auto o=poolObject(timer))drain(*o,cancel!=0);}
extern "C" void win_CloseThreadpoolTimer(void* timer){close(timer,nullptr,false);}
extern "C" int32_t win_RegisterWaitForSingleObject(void** out,void* handle,void* callback,void* context,uint32_t milliseconds,uint32_t flags) {
    constexpr uint32_t ExecuteOnlyOnce=0x8;
    auto target=std::dynamic_pointer_cast<Waitable>(resolveHandle(handle));
    if(!out||!callback||!target){setLastError(target?ErrorInvalidParameter:ErrorInvalidHandle);return 0;}
    auto o=newPoolObject(PoolObject::LegacyWait,callback,context);
    if(!o)return 0;
    o->once=(flags&ExecuteOnlyOnce)!=0;
    arm(*o,target,deadlineAfter(milliseconds),0);
    *out=o;return 1;
}
extern "C" int32_t win_UnregisterWaitEx(void* wait,void* completion) {
    if(!poolObject(wait)){setLastError(ErrorInvalidHandle);return 0;}
    close(wait,completion,reinterpret_cast<intptr_t>(completion)==-1);
    return 1;
}
extern "C" int32_t win_UnregisterWait(void* wait){return win_UnregisterWaitEx(wait,nullptr);}
// The timers of every queue run alike, so a queue is only a token.
extern "C" void* win_CreateTimerQueue(void){static char queue;return &queue;}
extern "C" int32_t win_DeleteTimerQueueEx(void*,void* completion) {
    if(completion&&reinterpret_cast<intptr_t>(completion)!=-1)win_SetEvent(completion);
    return 1;
}
extern "C" int32_t win_CreateTimerQueueTimer(void** out,void*,void* callback,void* context,uint32_t due,uint32_t period,uint32_t flags) {
    constexpr uint32_t ExecuteOnlyOnce=0x8;
    if(!out||!callback){setLastError(ErrorInvalidParameter);return 0;}
    auto o=newPoolObject(PoolObject::LegacyTimer,callback,context);
    if(!o)return 0;
    o->once=(flags&ExecuteOnlyOnce)!=0;
    arm(*o,nullptr,Clock::now()+std::chrono::milliseconds(due),o->once?0:period);
    *out=o;return 1;
}
extern "C" int32_t win_ChangeTimerQueueTimer(void*,void* timer,uint32_t due,uint32_t period) {
    auto o=poolObject(timer);
    if(!o){setLastError(ErrorInvalidHandle);return 0;}
    arm(*o,nullptr,Clock::now()+std::chrono::milliseconds(due),period);
    return 1;
}
extern "C" int32_t win_DeleteTimerQueueTimer(void*,void* timer,void* completion) {
    if(!poolObject(timer)){setLastError(ErrorInvalidHandle);return 0;}
    close(timer,completion,reinterpret_cast<intptr_t>(completion)==-1);
    return 1;
}

// ConcRT creates its threads with CreateRemoteThreadEx and an attribute list
// that holds their group affinity; one group holds every processor here.
extern "C" void* win_CreateRemoteThreadEx(void* process,void* security,uint64_t stack,void* start,void* parameter,uint32_t flags,void*,uint32_t* id) {
    if(reinterpret_cast<intptr_t>(process)!=-1){setLastError(ErrorInvalidParameter);return nullptr;}
    return win_CreateThread(security,stack,start,parameter,flags,id);
}
extern "C" int32_t win_InitializeProcThreadAttributeList(void* list,uint32_t count,uint32_t,uint64_t* size) {
    if(!size){setLastError(ErrorInvalidParameter);return 0;}
    uint64_t need=48+uint64_t(count)*24;
    if(!list||*size<need){*size=need;setLastError(ErrorInsufficientBuffer);return 0;}
    std::memset(list,0,size_t(need));
    return 1;
}
extern "C" int32_t win_UpdateProcThreadAttribute(void* list,uint32_t,uint64_t,void*,uint64_t,void*,uint64_t*) {
    if(!list){setLastError(ErrorInvalidParameter);return 0;}
    return 1;
}
extern "C" void win_DeleteProcThreadAttributeList(void*){}

// User-mode scheduling is not available, as on a Windows without it.
namespace {
int32_t noUms(){setLastError(50);return 0;}   // ERROR_NOT_SUPPORTED
}
extern "C" int32_t win_CreateUmsCompletionList(void**){return noUms();}
extern "C" int32_t win_CreateUmsThreadContext(void**){return noUms();}
extern "C" int32_t win_DeleteUmsCompletionList(void*){return noUms();}
extern "C" int32_t win_DeleteUmsThreadContext(void*){return noUms();}
extern "C" int32_t win_DequeueUmsCompletionListItems(void*,uint32_t,void**){return noUms();}
extern "C" int32_t win_EnterUmsSchedulingMode(void*){return noUms();}
extern "C" int32_t win_ExecuteUmsThread(void*){return noUms();}
extern "C" void* win_GetCurrentUmsThread(void){return nullptr;}
extern "C" void* win_GetNextUmsListItem(void*){return nullptr;}
extern "C" int32_t win_GetUmsCompletionListEvent(void*,void**){return noUms();}
extern "C" int32_t win_QueryUmsThreadInformation(void*,uint32_t,void*,uint32_t,uint32_t*){return noUms();}
extern "C" int32_t win_SetUmsThreadInformation(void*,uint32_t,void*,uint32_t){return noUms();}
extern "C" int32_t win_UmsThreadYield(void*){return noUms();}
// ETW: registration succeeds, no session ever enables tracing.
extern "C" uint32_t win_RegisterTraceGuidsW(void*,void*,const void*,uint32_t,void*,const uint16_t*,const uint16_t*,uint64_t* handle){if(handle)*handle=1;return 0;}
extern "C" uint32_t win_UnregisterTraceGuids(uint64_t){return 0;}
extern "C" uint32_t win_TraceEvent(uint64_t,void*){return 0;}
extern "C" uint64_t win_GetTraceLoggerHandle(void*){setLastError(ErrorInvalidParameter);return 0;}
extern "C" uint8_t win_GetTraceEnableLevel(uint64_t){return 0;}
extern "C" uint32_t win_GetTraceEnableFlags(uint64_t){return 0;}

// A callback's instance: modules are never unloaded (FreeLibrary counts
// nothing), and every callback has a thread of its own.
extern "C" void win_FreeLibraryWhenCallbackReturns(void*,void*){static std::atomic<bool> said{false};if(!said.exchange(true))log("FreeLibraryWhenCallbackReturns: primeira chamada (modulos nunca sao descarregados)");}
extern "C" int32_t win_CallbackMayRunLong(void*){return 1;}
extern "C" void win_DisassociateCurrentThreadFromCallback(void*){}

extern "C" void* win_CreateThreadpoolWork(void* callback,void* context,void*){
    if(!callback){setLastError(ErrorInvalidParameter);return nullptr;}
    return newPoolObject(PoolObject::Work,callback,context);
}
extern "C" void win_SubmitThreadpoolWork(void* work){
    if(auto o=poolObject(work)){std::lock_guard<std::mutex> guard(o->lock);if(!o->closed){++o->pendingWork;o->changed.notify_one();}}
}
extern "C" void win_WaitForThreadpoolWorkCallbacks(void* work,int32_t cancel){if(auto o=poolObject(work))drain(*o,cancel!=0);}
extern "C" void win_CloseThreadpoolWork(void* work){close(work,nullptr,false);}

// Thread pool I/O (Azimuth 1.0.2 gives it to the game's kernelx): the file is
// bound to a port of the object's own, as CreateIoCompletionPort binds one.
// Transfers complete before ReadFile/WriteFile return, so StartThreadpoolIo
// and CancelThreadpoolIo have nothing to count.
extern "C" void* win_CreateThreadpoolIo(void* file,void* callback,void* context,void*) {
    if(!callback){setLastError(ErrorInvalidParameter);return nullptr;}
    auto port=std::make_shared<CompletionPort>();
    auto handle=addHandle(port);
    if(!bindCompletionPort(file,handle,0)){removeHandle(handle);setLastError(ErrorInvalidHandle);return nullptr;}
    static std::atomic<unsigned> created{0};
    if(created++<8)log("CreateThreadpoolIo: arquivo "+hex(reinterpret_cast<uint64_t>(file))+" ligado a porta "+hex(reinterpret_cast<uint64_t>(handle)));
    return newPoolObject(PoolObject::Io,callback,context,port);
}
extern "C" void win_StartThreadpoolIo(void*){}
extern "C" void win_CancelThreadpoolIo(void*){}
// The packets queued are delivered first (or dropped, with cancel), then the callback in flight ends.
extern "C" void win_WaitForThreadpoolIoCallbacks(void* io,int32_t cancel) {
    auto o=poolObject(io);
    if(!o)return;
    for(;;) {
        {
            std::lock_guard<std::mutex> lock(waitLock);
            if(cancel)o->port->packets.clear();
            if(o->port->packets.empty()&&(o->ioRunning==0||activePool==o.get()))return;
        }
        sleepFor(1);
    }
}
// The port stays: the file may still be open and bound to it.
extern "C" void win_CloseThreadpoolIo(void* io) {
    if(auto o=poolObject(io)){std::lock_guard<std::mutex> lock(waitLock);o->port->packets.clear();}
    close(io,nullptr,false);
}
