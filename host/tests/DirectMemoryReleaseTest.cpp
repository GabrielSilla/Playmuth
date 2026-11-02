#include "../src/win32/DirectMemoryRelease.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace azimuth::win32;
constexpr uint64_t Unit=16384,Base=0x1000000000ull;
struct Mapping {unsigned kind;int64_t physical;}; // reserved/direct/flexible
struct FakeKernel {
    std::map<uint64_t,Mapping> mapped;
    std::map<int64_t,uint64_t> physical;
    unsigned unmaps=0,releases=0,reserves=0;
    bool rejectRanges=false,crossedAllocation=false;
    uint64_t failUnmap=~uint64_t(0),failReserve=~uint64_t(0);
    int64_t failRelease=std::numeric_limits<int64_t>::max();
    void reserveInitial(uint64_t at,unsigned blocks){for(unsigned i=0;i<blocks;++i)mapped[at+i*Unit]={1,0};}
    void add(std::map<uint64_t,DirectBlock>& table,uint64_t at,unsigned blocks,int64_t phys,uint64_t id){
        for(unsigned i=0;i<blocks;++i){table[at+i*Unit]={phys+int64_t(i*Unit),id};mapped[at+i*Unit]={2,phys+int64_t(i*Unit)};physical[phys+int64_t(i*Unit)]=id;}
    }
    void flex(std::set<uint64_t>& table,uint64_t at,unsigned blocks){for(unsigned i=0;i<blocks;++i){table.insert(at+i*Unit);mapped[at+i*Unit]={3,0};}}
    bool unmap(uint64_t at,uint64_t size,bool flexible){
        ++unmaps;if((rejectRanges&&size>Unit)||(failUnmap>=at&&failUnmap<at+size))return false;
        for(auto b=at;b<at+size;b+=Unit){CHECK(mapped.count(b));if(flexible)CHECK(mapped.at(b).kind==3);}
        for(auto b=at;b<at+size;b+=Unit)mapped.erase(b);
        return true;
    }
    bool release(int64_t at,uint64_t size){
        ++releases;
        uint64_t id=physical.count(at)?physical.at(at):0;CHECK(id);
        for(auto b=at;b<at+int64_t(size);b+=Unit){CHECK(physical.count(b));if(physical.at(b)!=id)crossedAllocation=true;}
        CHECK(!crossedAllocation);
        if((rejectRanges&&size>Unit)||(failRelease>=at&&failRelease<at+int64_t(size)))return false;
        for(auto& m:mapped)if(m.second.kind==2)CHECK(m.second.physical<at||m.second.physical>=at+int64_t(size));
        for(auto b=at;b<at+int64_t(size);b+=Unit)physical.erase(b);
        return true;
    }
    bool reserve(uint64_t at,uint64_t size){
        ++reserves;if((rejectRanges&&size>Unit)||(failReserve>=at&&failReserve<at+size))return false;
        for(auto b=at;b<at+size;b+=Unit)CHECK(!mapped.count(b));
        for(auto b=at;b<at+size;b+=Unit)mapped[b]={1,0};
        return true;
    }
};
int main(){
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;
        k.add(b,Base,256,0,1);k.reserveInitial(Base+256*Unit,128);
        CHECK(releaseDirectBacking(b,f,Base,Base+384*Unit,Unit,false,k));
        CHECK(k.unmaps==1&&k.releases==1&&!k.reserves&&b.empty()&&k.physical.empty()&&k.mapped.empty());
        std::puts("4 MiB + reserved gap: 1 unmap, 1 physical release, 0 re-reservations");
    }
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;k.add(b,Base,256,0,1);
        CHECK(releaseDirectBacking(b,f,Base+64*Unit,Base+192*Unit,Unit,true,k));
        CHECK(k.unmaps==1&&k.releases==1&&k.reserves==1&&b.size()==128);
        CHECK(k.mapped.at(Base).kind==2&&k.mapped.at(Base+64*Unit).kind==1&&k.mapped.at(Base+192*Unit).kind==2);
        // Recommit with adjacent physical addresses but a different allocation.
        k.add(b,Base+64*Unit,128,64*Unit,2);
        CHECK(releaseDirectBacking(b,f,Base,Base+256*Unit,Unit,false,k));
        CHECK(k.releases==4&&k.unmaps==2&&k.reserves==1&&k.physical.empty());
    }
    for(bool preserve:{false,true}){
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;
        k.add(b,Base,2,0,1);k.flex(f,Base+2*Unit,3);k.reserveInitial(Base+5*Unit,1);k.add(b,Base+6*Unit,2,2*Unit,2);
        CHECK(releaseDirectBacking(b,f,Base,Base+8*Unit,Unit,preserve,k));
        CHECK(k.releases==2&&b.empty()&&f.empty()&&k.physical.empty());
        CHECK(k.unmaps==(preserve?3u:1u)&&k.reserves==(preserve?3u:0u));
        CHECK(k.mapped.size()==(preserve?8u:0u));
        if(preserve)for(auto& m:k.mapped)CHECK(m.second.kind==1);
    }
    for(bool preserve:{false,true}){
        FakeKernel k;k.rejectRanges=true;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;k.add(b,Base,8,0,1);
        CHECK(releaseDirectBacking(b,f,Base,Base+8*Unit,Unit,preserve,k));
        CHECK(k.unmaps==9&&k.releases==9&&k.reserves==(preserve?9u:0u));
        CHECK(b.empty()&&k.physical.empty());
    }
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;k.add(b,Base,4,0,1);
        CHECK(releaseDirectBacking(b,f,Base+Unit/2,Base+3*Unit+Unit/2,Unit,true,k));
        CHECK(b.size()==2&&b.count(Base)&&b.count(Base+3*Unit)&&k.physical.size()==2);
    }
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;
        // Same allocation tag is insufficient when physical pages have a gap.
        k.add(b,Base,1,0,1);k.add(b,Base+Unit,1,3*Unit,1);
        CHECK(releaseDirectBacking(b,f,Base,Base+2*Unit,Unit,false,k));
        CHECK(k.releases==2&&k.physical.empty());
    }
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;
        k.flex(f,Base,8);k.rejectRanges=true;
        CHECK(releaseDirectBacking(b,f,Base,Base+8*Unit,Unit,true,k));
        CHECK(k.unmaps==9&&!k.releases&&k.reserves==9&&f.empty());
    }
    // Individual errors must fail instead of losing ownership or pretending
    // that a partially completed release is safe to continue executing.
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;k.add(b,Base,1,0,1);k.failUnmap=Base;
        CHECK(!releaseDirectBacking(b,f,Base,Base+Unit,Unit,false,k));CHECK(b.size()==1&&k.physical.size()==1&&!k.releases);
    }
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;k.add(b,Base,4,0,1);k.failRelease=2*Unit;
        CHECK(!releaseDirectBacking(b,f,Base,Base+4*Unit,Unit,true,k));
        CHECK(b.size()==2&&b.count(Base+2*Unit)&&b.count(Base+3*Unit)&&k.physical.size()==2&&!k.reserves);
    }
    {
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;k.add(b,Base,4,0,1);k.failReserve=Base+Unit;
        CHECK(!releaseDirectBacking(b,f,Base,Base+4*Unit,Unit,true,k));CHECK(b.empty()&&k.physical.empty());
    }
    // Fragmented layouts exercise holes, small allocations and flexible runs.
    for(unsigned seed=1;seed<=100;++seed){
        FakeKernel k;std::map<uint64_t,DirectBlock> b;std::set<uint64_t> f;uint64_t id=1;int64_t phys=0;
        for(unsigned i=0;i<128;++i){
            unsigned kind=(i*37+seed*13)%7;
            if(kind<2)k.reserveInitial(Base+i*Unit,1);
            else if(kind==2)k.flex(f,Base+i*Unit,1);
            else {k.add(b,Base+i*Unit,1,phys,id++);phys+=Unit;}
        }
        CHECK(releaseDirectBacking(b,f,Base,Base+128*Unit,Unit,seed%2,k));
        CHECK(b.empty()&&f.empty()&&k.physical.empty());
    }
    std::puts("OK: direct release batching, physical allocation boundaries, decommit/recommit, mixed backing, fallback and errors");
}
