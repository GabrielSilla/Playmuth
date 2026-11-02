#include "../src/win32/RepeatedAudioLog.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    using azimuth::win32::RepeatedAudioLog;
    RepeatedAudioLog filter;
    const char* warning="[xma2 @ 00000060C11B8800] Could not update timestamps for skipped samples.";
    CHECK(filter.accept(warning));CHECK(!filter.accept(warning));
    CHECK(!filter.accept("[xma2 @ abcdef012345] Could not update timestamps for skipped samples."));
    for(auto text:{"[xma2 @ abc] decode failed", "[xma2 @ ] Could not update timestamps for skipped samples.",
                  "[xma2 @ xyz] Could not update timestamps for skipped samples.",
                  "[other @ abc] Could not update timestamps for skipped samples.",
                  "[xma2 @ abc] Could not update timestamps for skipped samples. extra", "ordinary stderr"})
        CHECK(filter.accept(text));
    auto counts=filter.take();CHECK(counts.occurrences==3&&counts.suppressed==2);
    counts=filter.take();CHECK(!counts.occurrences&&!counts.suppressed);
    CHECK(filter.accept(warning));counts=filter.take();CHECK(counts.occurrences==1&&!counts.suppressed);
    std::atomic<unsigned> passed{0},done{0};std::vector<std::thread> writers;
    for(unsigned n=0;n<8;++n)writers.emplace_back([&]{for(unsigned i=0;i<10000;++i)if(filter.accept(warning))++passed;++done;});
    uint64_t total=0,suppressed=0;
    while(done!=8){counts=filter.take();total+=counts.occurrences;suppressed+=counts.suppressed;std::this_thread::yield();}
    for(auto& writer:writers)writer.join();
    counts=filter.take();total+=counts.occurrences;suppressed+=counts.suppressed;
    CHECK(total==80000&&passed+suppressed==total);
    std::puts("OK: exact audio warning, other diagnostics preserved, concurrent report accounting");
}
