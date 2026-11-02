// BCrypt hashes the engine uses (the game folder check): SHA-256 and SHA-1
// by the host's own implementation. Other algorithms, HMAC secrets and
// providers are a diagnostic stop. Handles live in the host's handle table.
#include "Win32.h"
#include <mutex>
#include <random>
#include "../Sha256.h"
#include <cstring>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
constexpr int32_t StatusSuccess=0,StatusInvalidHandle=int32_t(0xc0000008),StatusInvalidParameter=int32_t(0xc000000d);
struct Algorithm:Object {azimuth::Hash::Kind kind;};
struct HashObject:Object {azimuth::Hash hash;explicit HashObject(azimuth::Hash::Kind kind):hash(kind){}};
[[noreturn]] void unsupported(const std::string& what){stopGuest("bcrypt ainda sem adaptacao para este uso: "+what);}
std::string text(const uint16_t* value){return value?utf8(value,length(value)):std::string("(nulo)");}
}

extern "C" int32_t win_BCryptOpenAlgorithmProvider(void** algorithm,const uint16_t* id,const uint16_t* implementation,uint32_t flags) {
    if(!algorithm||!id)return StatusInvalidParameter;
    auto name=text(id);
    if(implementation||flags)unsupported("BCryptOpenAlgorithmProvider "+name+" com provedor "+text(implementation)+" e flags "+std::to_string(flags));
    auto object=std::make_shared<Algorithm>();
    if(name=="SHA256")object->kind=azimuth::Hash::Kind::Sha256;
    else if(name=="SHA1")object->kind=azimuth::Hash::Kind::Sha1;
    else unsupported("BCryptOpenAlgorithmProvider do algoritmo "+name);
    *algorithm=addHandle(object);
    return StatusSuccess;
}
extern "C" int32_t win_BCryptCloseAlgorithmProvider(void* algorithm,uint32_t) {
    if(!findHandleOf<Algorithm>(algorithm))return StatusInvalidHandle;
    removeHandle(algorithm);return StatusSuccess;
}
// The hash object buffer, when the engine passes one, is left unused: the
// state lives in the host object.
extern "C" int32_t win_BCryptCreateHash(void* algorithm,void** hash,uint8_t*,uint32_t,uint8_t* secret,uint32_t secretSize,uint32_t flags) {
    auto a=findHandleOf<Algorithm>(algorithm);
    if(!a)return StatusInvalidHandle;
    if(!hash)return StatusInvalidParameter;
    if(secret||secretSize||flags)unsupported("BCryptCreateHash com segredo (HMAC) ou flags "+std::to_string(flags));
    *hash=addHandle(std::make_shared<HashObject>(a->kind));
    return StatusSuccess;
}
extern "C" int32_t win_BCryptHashData(void* hash,uint8_t* input,uint32_t size,uint32_t flags) {
    auto h=findHandleOf<HashObject>(hash);
    if(!h)return StatusInvalidHandle;
    if(flags||(size&&!input))return StatusInvalidParameter;
    h->hash.add(input,size);return StatusSuccess;
}
extern "C" int32_t win_BCryptFinishHash(void* hash,uint8_t* output,uint32_t size,uint32_t flags) {
    auto h=findHandleOf<HashObject>(hash);
    if(!h)return StatusInvalidHandle;
    if(flags||!output||size!=h->hash.size())return StatusInvalidParameter;
    auto digest=h->hash.finish();std::memcpy(output,digest.data(),digest.size());
    return StatusSuccess;
}
extern "C" int32_t win_BCryptDestroyHash(void* hash) {
    if(!findHandleOf<HashObject>(hash))return StatusInvalidHandle;
    removeHandle(hash);return StatusSuccess;
}
// Random bytes from the host's generator, whatever the algorithm handle.
extern "C" int32_t win_BCryptGenRandom(void*,uint8_t* buffer,uint32_t size,uint32_t) {
    if(!buffer&&size)return int32_t(0xC000000D);   // STATUS_INVALID_PARAMETER
    static std::mutex lock;static std::mt19937_64 random{std::random_device{}()};
    std::lock_guard<std::mutex> guard(lock);
    for(uint32_t n=0;n<size;){uint64_t value=random();for(int k=0;k<8&&n<size;++k,++n)buffer[n]=uint8_t(value>>(8*k));}
    return 0;
}
