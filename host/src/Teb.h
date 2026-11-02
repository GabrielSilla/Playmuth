#pragma once
#include <cstddef>
#include <cstdint>

namespace azimuth {
// The Windows x64 process and thread blocks the guest reads through gs. Only
// the fields the engine and its CRT read are filled; everything else is zero.
struct ProcessBlock {
    explicit ProcessBlock(uint64_t imageBase);
    ~ProcessBlock();
    ProcessBlock(const ProcessBlock&)=delete;
    ProcessBlock& operator=(const ProcessBlock&)=delete;
    void* peb{};
};
class ThreadBlock {
public:
    // The engine's static TLS is ThreadLocalStoragePointer[0] (see AzimuthUwp/src/EngineTls.h).
    static constexpr size_t TlsCapacity=4096,TlsSlots=64;
    ThreadBlock(const ProcessBlock& process,void* stackBase,void* stackLimit,uint32_t threadId);
    ~ThreadBlock();
    ThreadBlock(const ThreadBlock&)=delete;
    ThreadBlock& operator=(const ThreadBlock&)=delete;
    // Points the calling thread's gs at this TEB; 0 or the errno.
    int enter() const;
    void* staticTls() const {return tls_;}
    // ThreadLocalStoragePointer[index] of a DLL's static TLS (index 1 and up;
    // 0 is the engine's block above).
    void setTlsSlot(size_t index,void* block){if(index<TlsSlots)slots_[index]=block;}
    void* teb() const {return teb_;}
    uint32_t id() const {return id_;}
    // Where setTlsSlot writes, for checking the page is still there (title 1.46).
    void* const* slots() const {return slots_;}
private:
    uint8_t* teb_{};
    void** slots_{};
    void* tls_{};
    uint32_t id_{};
};
}
