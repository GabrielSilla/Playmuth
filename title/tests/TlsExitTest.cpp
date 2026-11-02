// Exercise the SDK libc++abi pthread fallback together with emulated TLS.
// Compile the same test with zero and one delayed emutls cleanup round.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>

extern "C" [[noreturn]] void abort_message(const char*,...) {std::abort();}
namespace {
std::atomic<int> destructors{0},errors{0};
thread_local int value=17;
thread_local int order=0;
struct Sentinel {
    int id;
    ~Sentinel() {
        if(value!=29||id!=2-order)++errors;
        ++order;++destructors;
    }
};
thread_local Sentinel first{1},second{2};
void* worker(void*) {
    if(value!=17||first.id!=1||second.id!=2)++errors;
    value=29;
    return nullptr;
}
}
int main() {
    for(int n=0;n<32;++n) {
        pthread_t thread;
        if(pthread_create(&thread,nullptr,worker,nullptr)||pthread_join(thread,nullptr))return 2;
    }
    std::printf("TLS destructors: %d/64, value/order errors: %d\n",destructors.load(),errors.load());
    return destructors==64&&errors==0?0:1;
}
