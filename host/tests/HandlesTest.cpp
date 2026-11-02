#include "../src/win32/Win32.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <unistd.h>
namespace azimuth::win32 {
int64_t fastMonotonicNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
using namespace azimuth::win32;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Value:Object {unsigned value;explicit Value(unsigned v):value(v){}};
struct Reentrant:Object {
    void* other;std::atomic<bool>& destroyed;
    Reentrant(void* o,std::atomic<bool>& d):other(o),destroyed(d){}
    ~Reentrant() override {CHECK(findHandle(other));destroyed=true;}
};
int main(){
    alarm(30);
    CHECK(!findHandle(nullptr)&&!removeHandle(nullptr)&&!findHandle(reinterpret_cast<void*>(~uintptr_t(0))));
    auto anchor=addHandle(std::make_shared<Value>(123));
    std::vector<void*> padding;
    for(unsigned n=0;n<63;++n)padding.push_back(addHandle(std::make_shared<Value>(n)));
    std::atomic<bool> destroyed{false};
    auto victim=addHandle(std::make_shared<Reentrant>(anchor,destroyed));
    CHECK((reinterpret_cast<uintptr_t>(anchor)>>2)%64==(reinterpret_cast<uintptr_t>(victim)>>2)%64);
    CHECK(removeHandle(victim)&&destroyed&&!findHandle(victim)&&!removeHandle(victim));
    for(auto h:padding)CHECK(removeHandle(h));
    std::atomic<bool> failed{false};std::vector<std::thread> workers;
    for(unsigned n=0;n<8;++n)workers.emplace_back([&,n]{
        for(unsigned i=0;i<10000;++i){
            auto h=addHandle(std::make_shared<Value>(n));auto keep=findHandleOf<Value>(h);
            if(!keep||keep->value!=n||!removeHandle(h)||findHandle(h))failed=true;
            if(keep&&keep->value!=n)failed=true;
        }
    });
    for(auto& worker:workers)worker.join();CHECK(!failed);
    // A close removes the name; shared references obtained earlier stay alive.
    auto shared=addHandle(std::make_shared<Value>(456));auto keep=findHandleOf<Value>(shared);
    std::atomic<bool> stop{false};workers.clear();
    for(unsigned n=0;n<4;++n)workers.emplace_back([&]{while(!stop){auto value=findHandleOf<Value>(shared);if(value&&value->value!=456)failed=true;}});
    CHECK(removeHandle(shared));stop=true;for(auto& worker:workers)worker.join();
    CHECK(!failed&&!findHandle(shared)&&keep&&keep->value==456);
    CHECK(removeHandle(anchor));
    std::puts(handleContentionReport().c_str());
    std::puts("OK: 80000 concurrent handle lifetimes, close/lookup race, same-bucket destructor reentry");
}
