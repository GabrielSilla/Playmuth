#pragma once
#include <atomic>
#include <cstdint>

namespace azimuth::gpu {
// Producers only update atomics. One independent sampler reports actual
// elapsed windows, including zero-frame windows while Present is stalled.
class FrameWindow {
    std::atomic<uint64_t> frames{0},lastFrame{0},maximum{0};
    uint64_t previousTime=0,previousFrames=0; // sampler thread only
public:
    struct Sample {
        bool valid=false;
        uint64_t frames=0,elapsedNs=0,maxIntervalNs=0,noFrameNs=0,endNs=0,lastFrameNs=0;
        double fps() const{return elapsedNs?double(frames)*1e9/double(elapsedNs):0;}
    };
    void record(uint64_t now,uint64_t interval){
        auto peak=maximum.load(std::memory_order_relaxed);
        while(peak<interval&&!maximum.compare_exchange_weak(peak,interval,std::memory_order_relaxed)){}
        auto last=lastFrame.load(std::memory_order_relaxed);
        while(last<now&&!lastFrame.compare_exchange_weak(last,now,std::memory_order_relaxed)){}
        frames.fetch_add(1,std::memory_order_relaxed);
    }
    Sample sample(uint64_t now){
        if(previousTime&&now<=previousTime)return {};
        auto total=frames.load(std::memory_order_relaxed),last=lastFrame.load(std::memory_order_relaxed);
        auto peak=maximum.exchange(0,std::memory_order_relaxed);
        Sample result;
        if(previousTime&&last)result={true,total-previousFrames,now-previousTime,peak,now>last?now-last:0,now,last};
        previousTime=now;previousFrames=total;
        return result;
    }
};
}
