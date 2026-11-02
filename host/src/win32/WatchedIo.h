#pragma once
#include <cstddef>
#include <cstdint>
namespace azimuth::win32 {
struct WatchedIoSlot;
// Heap buffers reused by the calling thread. A lease owns a distinct slot,
// including when a guest VEH reenters file I/O during the destination copy.
class WatchedIoBuffer {
    WatchedIoSlot* slot=nullptr;
    char fallback[8192];
public:
    WatchedIoBuffer() noexcept;
    ~WatchedIoBuffer();
    WatchedIoBuffer(const WatchedIoBuffer&)=delete;
    WatchedIoBuffer& operator=(const WatchedIoBuffer&)=delete;
    char* data() noexcept;
    size_t capacity() const noexcept;
};
void* trackedMemcpy(void* to,const void* from,size_t size);
void* trackedMemmove(void* to,const void* from,size_t size);
void* trackedMemset(void* to,int value,size_t size);
void copyToWatched(void* destination,const void* source,size_t size);
void recordWatchedIo(uint64_t bytes,uint64_t readNs,uint64_t copyNs,bool stdio);
}
