#pragma once
#include <cstdint>
#include <string>
#include <cstddef>
#include <sys/types.h>
namespace azimuth::win32 {
// Metadata is permanent, address keyed, and accessible without the region
// mutex from the fault handler. All operations use physical protection blocks.
bool watchPrepare(uint64_t base,uint64_t size);
constexpr unsigned WatchProtectionBatch=32;
struct WatchProtection {int protection;unsigned writable,committed;};
// Contiguous native blocks, prepared under Memory.cpp's lifetime mutex.
bool watchProtectRange(uint64_t base,const WatchProtection* protections,unsigned count);
bool watchProtect(uint64_t block,uint64_t blockSize,int protection,unsigned writable,unsigned committed);
void watchForget(uint64_t base,uint64_t size);
bool watchFault(uint64_t address);
uint32_t watchQuery(uint64_t base,uint64_t size,void** addresses,uint64_t* count,bool reset);
bool watchReset(uint64_t base,uint64_t size);
bool watchedBuffer(const void* buffer,size_t size);
ssize_t watchedPread(int fd,void* buffer,size_t size,off_t offset);
ssize_t watchedRead(int fd,void* buffer,size_t size);
}
