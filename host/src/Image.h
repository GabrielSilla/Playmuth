#pragma once
#include "PeImage.h"
#include <functional>

namespace azimuth {
// The engine mapped into this process: relocated, imports bound, pages given
// the section permissions (never writable and executable at once). Features
// without a correct implementation are refused rather than guessed at.
class Image {
public:
    using Resolver=std::function<void*(const Import&,unsigned slot)>;
    // `sharedBlocks`: where the kernel protects only whole blocks larger than
    // a page (platform::protectionGranularity), a block holding sections of
    // different protections gets all of them together, including writable and
    // executable at once; the blocks are listed in sharedBlocks() and logged.
    // Without it such a block is refused. `allowWritableExecutable` also lets
    // such a block be writable and executable at once (the DLLs, whose layout
    // puts .text next to .data); the engine's blocks never are, and one that
    // would be is refused. A single 4 KiB page that is both
    // writable and executable is always refused.
    Image(const PeImage& image,const Resolver& resolve,uint32_t staticTlsIndex,bool sharedBlocks=false,bool allowWritableExecutable=false);
    ~Image();
    Image(const Image&)=delete;
    Image& operator=(const Image&)=delete;
    uint64_t base() const {return reinterpret_cast<uint64_t>(memory_);}
    uint32_t size() const {return size_;}
    void* entryAddress() const {return static_cast<uint8_t*>(memory_)+entry_;}
    bool contains(uint64_t address) const {return address>=base()&&address-base()<size_;}
    // Copies the static TLS template into a thread's block (zero-filled tail).
    void initializeStaticTls(void* block,size_t capacity) const;
    // Bytes of that block: 0 when the image has no static TLS.
    size_t staticTlsSize() const {return size_t(tlsBytes_)+tlsZero_;}
    bool hasStaticTls() const {return tlsBytes_||tlsZero_||tlsStart_;}
    std::vector<void*> tlsCallbacks() const;
    // The x64 unwind table (.pdata), kept for the exception dispatcher.
    Directory exceptions() const {return exceptions_;}
    // RVAs of the blocks given a union of protections, and how many of them are writable and executable.
    const std::vector<uint32_t>& mixedBlocks() const {return mixedBlocks_;}
    unsigned writableExecutableBlocks() const {return wxBlocks_;}
private:
    void* memory_{};
    uint32_t size_{},entry_{};
    uint32_t tlsStart_{},tlsBytes_{},tlsZero_{},tlsCallbacksRva_{};
    Directory exceptions_{};
    std::vector<uint32_t> mixedBlocks_;
    unsigned wxBlocks_=0;
    size_t reserved_=0;
    std::vector<Section> sections_;
};
}
