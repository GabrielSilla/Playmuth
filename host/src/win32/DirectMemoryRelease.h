#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>

namespace azimuth::win32 {
struct DirectBlock {
    int64_t physical;
    uint64_t allocation; // identity, not just adjacent physical addresses
};

// Used by the native backend and by a fake kernel in the unit test. A range
// rejected by the backend is retried block by block. An individual failure
// is returned to the caller; never erase ownership of physical memory which
// has not been successfully released.
template<class Ops>
bool unmapDirectRange(Ops& ops,uint64_t base,uint64_t size,uint64_t unit,bool flexible){
    if(ops.unmap(base,size,flexible))return true;
    if(size==unit)return false;
    for(auto at=base;at<base+size;at+=unit)if(!ops.unmap(at,unit,flexible))return false;
    return true;
}
template<class Ops>
bool reserveDirectRange(Ops& ops,uint64_t base,uint64_t size,uint64_t unit){
    if(ops.reserve(base,size))return true;
    if(size==unit)return false;
    for(auto at=base;at<base+size;at+=unit)if(!ops.reserve(at,unit))return false;
    return true;
}
template<class Ops>
bool releaseDirectBacking(std::map<uint64_t,DirectBlock>& backing,std::set<uint64_t>& flexible,
                          uint64_t from,uint64_t to,uint64_t unit,bool preserve,Ops& ops){
    auto first=(from+unit-1)/unit*unit,last=to/unit*unit;
    if(first>=last)return true;
    // MEM_RELEASE removes the entire native reservation once, including its
    // reserved gaps. It must not recreate reservations which are then removed.
    if(!preserve&&!unmapDirectRange(ops,first,last-first,unit,false))return false;
    auto at=first;
    while(at<last){
        auto direct=backing.lower_bound(at);auto flex=flexible.lower_bound(at);
        uint64_t db=direct==backing.end()?last:direct->first,fb=flex==flexible.end()?last:*flex;
        auto begin=std::min(db,fb);if(begin>=last)break;
        if(fb<db){
            auto end=flex;auto limit=begin;
            while(end!=flexible.end()&&*end==limit&&limit<last){++end;limit+=unit;}
            if(preserve&&!unmapDirectRange(ops,begin,limit-begin,unit,true))return false;
            flexible.erase(flex,end);
            if(preserve&&!reserveDirectRange(ops,begin,limit-begin,unit))return false;
            at=limit;continue;
        }
        auto end=direct;auto limit=begin;auto origin=direct->second;
        while(end!=backing.end()&&end->first==limit&&limit<last&&
              end->second.allocation==origin.allocation&&
              end->second.physical==origin.physical+int64_t(limit-begin)){++end;limit+=unit;}
        if(preserve&&!unmapDirectRange(ops,begin,limit-begin,unit,false))return false;
        if(ops.release(origin.physical,limit-begin))backing.erase(direct,end);
        else {
            if(limit-begin==unit)return false;
            while(direct!=end){
                if(!ops.release(direct->second.physical,unit))return false;
                direct=backing.erase(direct);
            }
        }
        if(preserve&&!reserveDirectRange(ops,begin,limit-begin,unit))return false;
        at=limit;
    }
    return true;
}
}
