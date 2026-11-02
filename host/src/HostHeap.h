#pragma once
#include <cstddef>

// The host's allocator: every C++ allocation (operator new), the engine's
// process heap and the msvcrt of the loaded DLLs go through it. The console
// title's malloc refused blocks of a few MiB (titles 0.21-0.26: bad_alloc
// reading a 5.9 MB DLL, E_OUTOFMEMORY from vkd3d-proton, and an abort in
// CreateCompatibleBitmap for the launcher's 8 MB bitmap), and later ran out
// altogether (title 0.47). No block comes from it: blocks of MappedFrom bytes
// or more are mapped on their own (direct memory on the console), smaller
// ones come from the host's arenas (HostHeap.cpp).
// Every block has a 32-byte header in front of it, so it can be freed, sized
// and moved without a table.
namespace azimuth {
constexpr size_t MappedFrom=1u<<20;
// `alignment` a power of two up to 4096 (at least 16 is used). Null when out of memory.
void* heapAlloc(size_t size,size_t alignment,bool zero) noexcept;
void heapFree(void* block) noexcept;
// The bytes a block can hold (its requested size or more).
size_t heapUsable(const void* block) noexcept;
// Blocks that malloc refused and mmap served, for the log.
size_t heapFallbacks() noexcept;
// Bytes the allocator holds in its own mmap blocks now.
size_t heapMappedBytes() noexcept;
// The largest free range of the console's direct memory (0 on Linux).
size_t directMemoryFree() noexcept;
// Direct memory allocated by the whole process (title 1.40: the largest free
// run alone cannot tell fragmentation from exhaustion), 0 when unknown.
size_t directMemoryUsed() noexcept;
}
