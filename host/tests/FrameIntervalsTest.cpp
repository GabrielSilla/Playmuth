#include "../src/gpu/FrameIntervals.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %s\n",#x);return 1;}}while(0)
int main(){
    azimuth::gpu::FrameIntervals samples;
    for(unsigned i=0;i<95;++i)samples.record(33333333);
    for(unsigned i=0;i<3;++i)samples.record(80000000);
    for(unsigned i=0;i<2;++i)samples.record(200000000);
    auto text=samples.report();std::puts(text.c_str());
    CHECK(text.find("100 amostras")!=std::string::npos);
    CHECK(text.find("max 200.00 ms, p95 34 ms, p99 200 ms, >50 ms 5, >100 ms 2")!=std::string::npos);
    CHECK(samples.report().find("sem amostras")!=std::string::npos);
    samples.record(2200000000);
    CHECK(samples.report().find("p99 >1000 ms")!=std::string::npos);
    std::puts("OK: frame interval distribution and reset");
}
