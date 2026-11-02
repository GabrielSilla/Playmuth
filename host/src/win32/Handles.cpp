// The handle table and the errno-to-Win32 error mapping shared by the
// Windows API implementations.
#include "Win32.h"
#include <cerrno>
#include <array>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

namespace azimuth::win32 {
namespace {
// Independent handles no longer serialize on one process-wide mutex.
// Handles remain monotonically allocated and are never recycled.
struct alignas(64) Bucket {
    std::mutex lock;
    std::map<uintptr_t,std::shared_ptr<Object>> objects;
    std::atomic<uint64_t> contentions{0},waitNs{0},maxWaitNs{0};
};
std::array<Bucket,64> buckets;
std::atomic<uintptr_t> next{0x100};
Bucket& bucketFor(uintptr_t handle){return buckets[(handle>>2)%buckets.size()];}
struct LockedBucket {
    Bucket& bucket;std::unique_lock<std::mutex> guard;
    explicit LockedBucket(Bucket& b):bucket(b),guard(b.lock,std::try_to_lock){
        if(guard.owns_lock())return;
        auto begin=fastMonotonicNs();guard.lock();auto end=fastMonotonicNs();
        auto elapsed=end>begin?uint64_t(end-begin):0;
        b.contentions.fetch_add(1,std::memory_order_relaxed);b.waitNs.fetch_add(elapsed,std::memory_order_relaxed);
        auto old=b.maxWaitNs.load(std::memory_order_relaxed);
        while(old<elapsed&&!b.maxWaitNs.compare_exchange_weak(old,elapsed,std::memory_order_relaxed)){}
    }
};
}
void* addHandle(std::shared_ptr<Object> object) {
    auto handle=next.fetch_add(4,std::memory_order_relaxed);auto& b=bucketFor(handle);
    LockedBucket guard(b);b.objects.emplace(handle,std::move(object));
    return reinterpret_cast<void*>(handle);
}
std::shared_ptr<Object> findHandle(void* handle) {
    auto key=reinterpret_cast<uintptr_t>(handle);auto& b=bucketFor(key);LockedBucket guard(b);
    auto found=b.objects.find(key);
    return found==b.objects.end()?nullptr:found->second;
}
bool removeHandle(void* handle) {
    // Destructors can reenter the handle table: destroy after unlocking.
    std::shared_ptr<Object> released;
    auto key=reinterpret_cast<uintptr_t>(handle);auto& b=bucketFor(key);LockedBucket guard(b);
    auto found=b.objects.find(key);
    if(found==b.objects.end())return false;
    released=std::move(found->second);b.objects.erase(found);return true;
}
std::string handleContentionReport(){
    uint64_t count=0,total=0,peak=0;
    for(auto& b:buckets){count+=b.contentions.exchange(0);total+=b.waitNs.exchange(0);peak=std::max(peak,b.maxWaitNs.exchange(0));}
    return "Disputa de handles: "+std::to_string(count)+" esperas, "+std::to_string(double(total)/1e6)+" ms, max "+std::to_string(double(peak)/1e6)+" ms, 64 particoes (~10 s)";
}
uint32_t errorFromErrno(int error) {
    switch(error) {
        case 0:return ErrorSuccess;
        case ENOENT:return ErrorFileNotFound;
        case ENOTDIR:return ErrorPathNotFound;
        case EACCES:case EPERM:case EROFS:case EISDIR:case EBUSY:return ErrorAccessDenied;
        case EEXIST:return ErrorFileExists;
        case ENOTEMPTY:return ErrorDirNotEmpty;
        case ENOSPC:return ErrorDiskFull;
        case ENAMETOOLONG:return ErrorFilenameExcedRange;
        case EINVAL:return ErrorInvalidParameter;
        case EBADF:return ErrorInvalidHandle;
        case ENOMEM:return ErrorNotEnoughMemory;
        default:return 31;   // ERROR_GEN_FAILURE
    }
}
}
