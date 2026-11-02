#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace azimuth::gpu {
// Bounded, allocation-free recording. Histogram percentiles are rounded up
// to milliseconds; the final bucket contains intervals above one second.
class FrameIntervals {
    std::atomic<uint64_t> buckets[1002]{};
    std::atomic<uint64_t> maximum{0},over50{0},over100{0};
public:
    void record(uint64_t ns){
        auto bin=ns/1000000+(ns%1000000!=0);if(bin>1001)bin=1001;
        buckets[bin].fetch_add(1,std::memory_order_relaxed);
        if(ns>50000000)over50.fetch_add(1,std::memory_order_relaxed);
        if(ns>100000000)over100.fetch_add(1,std::memory_order_relaxed);
        auto old=maximum.load(std::memory_order_relaxed);
        while(old<ns&&!maximum.compare_exchange_weak(old,ns,std::memory_order_relaxed)){}
    }
    std::string report(){
        uint64_t values[1002],count=0;
        for(unsigned i=0;i<1002;++i){values[i]=buckets[i].exchange(0);count+=values[i];}
        auto max=maximum.exchange(0),slow=over50.exchange(0),verySlow=over100.exchange(0);
        if(!count)return "Intervalos entre Present: sem amostras";
        auto percentile=[&](unsigned p){uint64_t sum=0,target=(count*p+99)/100;for(unsigned i=0;i<1002;++i){sum+=values[i];if(sum>=target)return i;}return 1001u;};
        auto label=[](unsigned value){return value==1001?std::string(">1000"):std::to_string(value);};
        char text[200];
        std::snprintf(text,sizeof text,"Intervalos entre Present: %llu amostras, max %.2f ms, p95 %s ms, p99 %s ms, >50 ms %llu, >100 ms %llu (~10 s)",
                      static_cast<unsigned long long>(count),double(max)/1e6,label(percentile(95)).c_str(),label(percentile(99)).c_str(),
                      static_cast<unsigned long long>(slow),static_cast<unsigned long long>(verySlow));
        return text;
    }
};
}
