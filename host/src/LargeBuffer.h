#pragma once
#include "Platform.h"
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

// A std::vector<uint8_t> whose big blocks come from mmap and not from malloc.
// Title 0.21 on the console: reading the 5.9 MB d3d12core.dll through the
// system's malloc ended in std::bad_alloc, while 118 KB blocks were fine.
// Blocks of 256 KiB and up are mapped (rounded to the page, zero-filled by
// the kernel); smaller ones use operator new as before.
namespace azimuth {
template<class T>struct LargeAllocator {
    using value_type=T;
    static constexpr size_t Threshold=256u<<10,Page=16384;
    LargeAllocator()=default;
    template<class U>LargeAllocator(const LargeAllocator<U>&){}
    T* allocate(size_t count) {
        size_t bytes=count*sizeof(T);
        if(bytes<Threshold)return static_cast<T*>(::operator new(bytes));
        try {return static_cast<T*>(platform::reserve(0,(bytes+Page-1)/Page*Page));}
        catch(...) {throw std::bad_alloc();}
    }
    void deallocate(T* block,size_t count) noexcept {
        size_t bytes=count*sizeof(T);
        if(bytes<Threshold)::operator delete(block);
        else platform::release(block,(bytes+Page-1)/Page*Page);
    }
    template<class U>bool operator==(const LargeAllocator<U>&) const {return true;}
    template<class U>bool operator!=(const LargeAllocator<U>&) const {return false;}
};
using Bytes=std::vector<uint8_t,LargeAllocator<uint8_t>>;
}
