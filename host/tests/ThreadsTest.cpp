#include "../src/Teb.h"
#include "../src/win32/Win32.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <unistd.h>
#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT
namespace azimuth::win32 {
void log(const std::string& text){std::printf("%s\n",text.c_str());}
void stopGuest(const std::string&){std::exit(2);}
}
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    alarm(40);
    azimuth::ProcessBlock process(0);
    azimuth::ThreadBlock thread(process,nullptr,nullptr,256);
    CHECK(!thread.enter());
    auto automatic=win_CreateEventW(nullptr,0,1,nullptr);
    CHECK(win_WaitForSingleObject(automatic,0)==0);
    CHECK(win_WaitForSingleObjectEx(automatic,0,0)==258);
    auto manual=win_CreateEventW(nullptr,1,1,nullptr);
    CHECK(win_WaitForSingleObject(manual,0)==0);
    CHECK(win_WaitForSingleObject(manual,0)==0);
    CHECK(win_WaitForSingleObject(nullptr,0)==0xffffffff);
    std::thread signal([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10));win_SetEvent(automatic);});
    auto result=win_WaitForSingleObject(automatic,1000);
    signal.join();CHECK(result==0);
    CHECK(win_WaitForSingleObject(automatic,0)==258);
    // Exercise both sides of the registration/wake race, including buckets
    // with no sleeper. Finite deadline exposes a lost notification.
    for(unsigned i=0;i<200;++i){
        uint64_t value=0,expected=0;
        std::thread writer([&]{
            if(i%2)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            __atomic_store_n(&value,1,__ATOMIC_SEQ_CST);
            if(i%2)win_WakeByAddressSingle(&value);else win_WakeByAddressAll(&value);
        });
        auto ready=win_WaitOnAddress(&value,&expected,8,1000);
        writer.join();CHECK(ready);
    }
    // Repeated event handoff covers registration before/after signaling.
    auto reply=win_CreateEventW(nullptr,0,0,nullptr);std::atomic<bool> failed{false};
    std::thread partner([&]{for(unsigned i=0;i<2000;++i){if(win_WaitForSingleObject(automatic,2000)!=0){failed=true;return;}if(!win_SetEvent(reply)){failed=true;return;}}});
    for(unsigned i=0;i<2000;++i){CHECK(win_SetEvent(automatic));CHECK(win_WaitForSingleObject(reply,2000)==0);}
    partner.join();CHECK(!failed);CHECK(win_CloseHandle(reply));
    // wait-all must not consume an auto event until every object is ready.
    auto second=win_CreateEventW(nullptr,0,0,nullptr);void* pair[]={automatic,second};
    CHECK(win_SetEvent(automatic));CHECK(win_WaitForMultipleObjects(2,pair,1,0)==258);
    CHECK(win_SetEvent(second));CHECK(win_WaitForMultipleObjects(2,pair,1,1000)==0);
    CHECK(win_WaitForSingleObject(automatic,0)==258&&win_WaitForSingleObject(second,0)==258);
    CHECK(win_CloseHandle(second));
    auto semaphore=win_CreateSemaphoreA(nullptr,0,1000,nullptr);CHECK(semaphore);
    std::atomic<unsigned> ready{0},taken{0};std::vector<std::thread> consumers;
    for(unsigned n=0;n<8;++n)consumers.emplace_back([&]{++ready;for(unsigned i=0;i<125;++i){if(win_WaitForSingleObject(semaphore,2000)!=0){failed=true;return;}++taken;}});
    while(ready!=8)std::this_thread::yield();
    int32_t oldCount=-1;CHECK(win_ReleaseSemaphore(semaphore,1000,&oldCount)&&oldCount==0);
    for(auto& consumer:consumers)consumer.join();
    CHECK(!failed&&taken==1000&&win_WaitForSingleObject(semaphore,0)==258);
    CHECK(!win_ReleaseSemaphore(semaphore,1001,nullptr));CHECK(win_CloseHandle(semaphore));
    // Releasing one recursion level must not make a mutex available.
    auto mutex=win_CreateMutexW(nullptr,1,nullptr);CHECK(mutex);
    CHECK(win_WaitForSingleObject(mutex,0)==0);
    auto acquired=win_CreateEventW(nullptr,0,0,nullptr);
    std::thread owner([&]{
        azimuth::ThreadBlock other(process,nullptr,nullptr,2001);
        if(other.enter()||win_WaitForSingleObject(mutex,2000)!=0){failed=true;return;}
        if(!win_SetEvent(acquired)||!win_ReleaseMutex(mutex))failed=true;
    });
    CHECK(win_WaitForSingleObject(acquired,20)==258);CHECK(win_ReleaseMutex(mutex));
    CHECK(win_WaitForSingleObject(acquired,20)==258);CHECK(win_ReleaseMutex(mutex));
    CHECK(win_WaitForSingleObject(acquired,2000)==0);owner.join();CHECK(!failed);
    CHECK(win_CloseHandle(acquired)&&win_CloseHandle(mutex));
    auto host=azimuth::win32::hostEvent();CHECK(host);
    azimuth::win32::setHostEvent(host,true);CHECK(win_WaitForSingleObject(host,0)==0);
    azimuth::win32::setHostEvent(host,false);CHECK(win_WaitForSingleObject(host,0)==258);CHECK(win_CloseHandle(host));
    auto timer=win_CreateWaitableTimerExW(nullptr,nullptr,1,0);CHECK(timer);int64_t due=-20000;
    CHECK(win_SetWaitableTimer(timer,&due,0,nullptr,nullptr,0));CHECK(win_WaitForSingleObject(timer,1000)==0);CHECK(win_CloseHandle(timer));
    auto port=win_CreateIoCompletionPort(reinterpret_cast<void*>(~uintptr_t(0)),nullptr,0,0);CHECK(port);
    CHECK(win_PostQueuedCompletionStatus(port,42,123,reinterpret_cast<void*>(456)));
    uint32_t bytes=0;uint64_t key=0;void* overlapped=nullptr;
    CHECK(win_GetQueuedCompletionStatus(port,&bytes,&key,&overlapped,1000));CHECK(bytes==42&&key==123&&overlapped==reinterpret_cast<void*>(456));CHECK(win_CloseHandle(port));
    CHECK(win_CloseHandle(automatic));CHECK(win_CloseHandle(manual));
    std::puts("OK: events, 2000 handoffs, wait-all, 1000 semaphore permits, recursive mutex, timer, completion port and 200 address races");
}
