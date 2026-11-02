#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// The few OS services the host needs, for the PS5 payload and for the Linux
// build that checks the same code on a PC.
namespace azimuth::platform {
enum : unsigned {Read=1,Write=2,Execute=4};
// Committed read-write memory, at `preferred` when the system grants it.
void* reserve(uint64_t preferred,size_t size);
void protect(void* at,size_t size,unsigned access);
void release(void* at,size_t size) noexcept;
// The smallest range the kernel protects separately: 16 KiB on the PS5 (title
// 0.20: the last protection set on any 4 KiB page of a 16 KiB block wins for
// all of it). On Linux 4 KiB, or 16 KiB when AZIMUTH_BLOCK16K is set, to check
// the code that copes with it.
size_t protectionGranularity() noexcept;
// Whether a file can be opened for reading (0, or the errno). Tried with fopen:
// access(2) answered "not found" in the console title (title 0.24) for a file
// that fopen and stat read, in /data/azimuth-ps5.
int readable(const std::string& path) noexcept;
// Base of the calling thread's gs segment: where the guest reads its TEB.
// Both return 0 or the errno.
int setGsBase(void* base) noexcept;
int getGsBase(void** base) noexcept;
// Where reports go: /data/azimuth-ps5 on the console, the working directory on Linux.
std::string reportDir();
// The console's on-screen notification; nothing on Linux.
void notify(const std::string& text) noexcept;
const char* name() noexcept;
// Makes the calling thread share the CPU fairly: round-robin at its own
// priority, allowed on every core the process may use. A console title's
// threads are first-in first-out (title 0.40/0.41: a thread that spun
// starved the launcher, the host loop and every thread created after it).
// `raise` lifts the thread above the others by RaisedBy (on the console a
// smaller number is a higher priority): the threads the screen depends on,
// so that work on every core (title 0.50: 15 threads compiling pipelines)
// does not freeze the picture. Returns what was found and done, for the log;
// empty on Linux.
constexpr int RaisedBy=100;
std::string shareCpu(bool raise=false);
// The title's flexible memory (what malloc and mmap draw from), in bytes:
// configured and still available. False on Linux.
bool flexibleMemory(uint64_t& configured,uint64_t& available) noexcept;
}
