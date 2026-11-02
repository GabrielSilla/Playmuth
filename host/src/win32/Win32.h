#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Shared by the Windows API implementations (win32/*.cpp). They run on the
// engine's threads, with gs holding the thread's TEB.
namespace azimuth::win32 {
// A Windows path prefix (no trailing backslash) served by a host directory.
struct Mount {std::u16string guest;std::string host;};
struct Process {
    uint64_t imageBase=0;
    uint32_t imageSize=0;
    std::u16string modulePath;   // Windows path of azimuth.exe as the engine sees it
    std::string commandLine;     // UTF-8
    std::vector<Mount> mounts;   // most specific first
    std::u16string currentDirectory;
};
// Set once by the host before the engine runs.
void configure(const Process& process);
const Process& process();

// The calling engine thread's TEB (gs:[0x30]).
uint8_t* teb();
void setLastError(uint32_t error);
// The monotonic clock (CLOCK_MONOTONIC's nanoseconds) from the processor's
// time-stamp counter, without a system call (title 1.26).
int64_t fastMonotonicNs();
std::string writeWatchReport();
std::string memoryProtectionReport();
std::string watchProtectionReport();
std::string watchedIoReport();
std::string watchStageReport();
std::string bulkWriteReport();
std::string repeatedAudioReport();
std::string handleContentionReport();
std::string directReleaseReport();
std::vector<std::string> memorySlowEvents();

size_t length(const uint16_t* text);
std::string utf8(const uint16_t* text,size_t count);
std::u16string utf16(const std::string& text);

// Host services, implemented by the host (main.cpp).
void log(const std::string& text);
// Code addresses on the calling thread's stack (engine, game, DLLs), " 0x..." each.
std::string codeCallers();
// When the process has run out of file descriptors: what holds them, once (Files.cpp).
void reportOpenDescriptors();
// CreateIoCompletionPort with a file handle: binds the file to the port (Files.cpp).
bool bindCompletionPort(void* file,void* port,uint64_t key);
// Every fiber, with the code addresses on each suspended one's stack (Fibers.cpp).
std::string describeFibers();
// Ends the engine's run from inside a Windows call (an exit, or a call the
// host cannot honour). Call it holding no lock.
[[noreturn]] void stopGuest(const std::string& reason);

// Engine threads (Threads.cpp, Kernel32.cpp): the calling thread's FLS
// destructors on its way out, and waking every blocked wait when the run stops.
void runFlsCallbacks();
void wakeWaiters();
// A manual-reset event the host owns (a thread's message queue): a handle
// the engine's waits accept, set and read without touching the last error.
void* hostEvent();
void setHostEvent(void* event,bool set);
bool hostEventSet(void* event);

// Windows and GDI (User32.cpp, Gdi.cpp). The display is one 1920x1080
// monitor; a window's client area is its whole rectangle (no frame).
constexpr int DisplayWidth=1920,DisplayHeight=1080;
bool windowClientSize(void* window,int& width,int& height);
// A key from the controller (Input.cpp), to the active window and to GetKeyState.
void postKey(uint32_t vk,uint32_t scan,bool down);
// The last picture the engine copied to a window (BitBlt to its DC), B,G,R,X
// top-down; false when nothing newer than `after` was drawn.
struct Frame {std::vector<uint32_t> pixels;int width=0,height=0;void* window=nullptr;uint64_t serial=0;};
bool latestFrame(Frame& frame,uint64_t after);
// Pagefile-backed sections (Memory.cpp): CreateFileMapping with no file, and
// MapViewOfFile/UnmapViewOfFile on them. mapSectionView is null and
// unmapSectionView false for a handle or view that is not a section's.
void* createSection(uint64_t size,uint32_t protect);
void* mapSectionView(void* handle,uint64_t offset,uint64_t size,uint32_t access);
bool unmapSectionView(const void* view,bool preservePlaceholder);
// Who holds the GDI lock now, for reports.
std::string gdiOwner();

enum : uint32_t {
    ErrorSuccess=0,ErrorFileNotFound=2,ErrorPathNotFound=3,ErrorAccessDenied=5,ErrorInvalidHandle=6,ErrorNotEnoughMemory=8,
    ErrorNoMoreFiles=18,ErrorWriteFault=29,ErrorHandleEof=38,ErrorNotSupported=50,ErrorFileExists=80,ErrorInvalidParameter=87,
    ErrorDiskFull=112,ErrorInsufficientBuffer=122,ErrorInvalidName=123,ErrorModNotFound=126,ErrorProcNotFound=127,
    ErrorDirNotEmpty=145,ErrorAlreadyExists=183,ErrorFilenameExcedRange=206,ErrorInvalidData=13,ErrorNoUnicodeTranslation=1113
};
// The Win32 error for a POSIX errno.
uint32_t errorFromErrno(int error);

// Kernel objects behind the HANDLEs the engine holds (files, searches, and
// later events, threads...). Handles are small multiples of 4, as on Windows.
struct Object {virtual ~Object()=default;};
void* addHandle(std::shared_ptr<Object> object);
std::shared_ptr<Object> findHandle(void* handle);
template<class T>std::shared_ptr<T> findHandleOf(void* handle){return std::dynamic_pointer_cast<T>(findHandle(handle));}
bool removeHandle(void* handle);
constexpr uintptr_t InvalidHandle=~uintptr_t(0);

// Windows paths: the full form of a path the engine names (relative to the
// current directory, separators and dot segments resolved), and the host
// path that serves it. An empty host path means the path is outside every
// mount; the Win32 error is set.
std::u16string fullPath(const std::u16string& path);
std::string hostPath(const std::u16string& fullGuestPath);
std::u16string fromGuest(const uint16_t* text);

// The environment of the DLLs the host loads, shared by msvcrt's getenv and
// GetEnvironmentVariableA (Msvcrt.cpp): empty until they set a variable. The
// list is locked while `lock` holds the lock this call takes.
std::vector<std::pair<std::string,std::string>>& dllEnvironment(std::unique_lock<std::mutex>& lock);
}
