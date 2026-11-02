#include "../src/gpu/FrameWindow.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    azimuth::gpu::FrameWindow window;
    uint64_t t=1000000000;
    CHECK(!window.sample(t).valid);CHECK(!window.sample(t+100000000).valid);t+=100000000;
    for(unsigned n=1;n<=6;++n)window.record(t+n*16000000,16000000);
    auto s=window.sample(t+100000000);CHECK(s.valid&&s.frames==6&&s.fps()==60&&s.maxIntervalNs==16000000);t+=100000000;
    for(unsigned n=1;n<=3;++n)window.record(t+n*30000000,30000000);
    s=window.sample(t+100000000);CHECK(s.frames==3&&s.fps()==30&&s.maxIntervalNs==30000000);t+=100000000;
    s=window.sample(t+100000000);CHECK(s.valid&&!s.frames&&s.fps()==0&&s.noFrameNs==110000000&&!s.maxIntervalNs);t+=100000000;
    // A stall remains visible even if no new Present arrives to finish it.
    s=window.sample(t+100000000);CHECK(s.valid&&!s.frames&&s.noFrameNs==210000000);t+=100000000;
    window.record(t+40000000,250000000);
    s=window.sample(t+100000000);CHECK(s.frames==1&&s.fps()==10&&s.maxIntervalNs==250000000);t+=100000000;
    // A descheduled sampler divides by its real 500 ms window, not 100 ms.
    for(unsigned n=1;n<=3;++n)window.record(t+n*100000000,100000000);
    s=window.sample(t+500000000);CHECK(s.frames==3&&s.elapsedNs==500000000&&std::abs(s.fps()-6)<1e-9);t+=500000000;
    CHECK(!window.sample(t).valid);CHECK(!window.sample(t-1).valid);
    window.record(t+10000000,10000000);
    s=window.sample(t+100000000);CHECK(s.frames==1&&s.fps()==10);
    std::puts("OK: 100 ms FPS at 60/30/10/0, stalled Present, frame gaps and real delayed-sampler duration");
}
