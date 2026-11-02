#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace azimuth {
std::array<uint8_t,32> sha256(const uint8_t* data,size_t size);
// SHA-1 or SHA-256 over data given in pieces (the engine's BCrypt hashes).
class Hash {
public:
    enum class Kind {Sha1,Sha256};
    explicit Hash(Kind kind);
    void add(const uint8_t* data,size_t size);
    // The digest (20 or 32 bytes); the hash then starts over.
    std::vector<uint8_t> finish();
    size_t size() const {return kind_==Kind::Sha1?20:32;}
private:
    Kind kind_;
    uint32_t h_[8];
    uint8_t pending_[64];
    size_t used_=0;
    uint64_t total_=0;
    void reset();
    void block(const uint8_t* p);
};
}
