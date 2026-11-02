#pragma once
#include "Image.h"
#include "Teb.h"
#include <functional>
#include <string>
#include <vector>

// Engine threads: a host thread becomes an engine thread for the duration of
// runAsGuest (TEB through gs, the static TLS template, the TLS callbacks), and
// the run's first stop ends every engine thread back in its host frame.
namespace azimuth {
struct GuestSetup {Image* image=nullptr;ProcessBlock* process=nullptr;std::vector<void*> tlsCallbacks;};
void setupGuest(const GuestSetup& setup);
uint32_t newThreadId();

enum class GuestEnd {Returned,Exited,Stopped};
struct GuestOutcome {GuestEnd end=GuestEnd::Returned;uint32_t code=0;};
// Runs body as an engine thread on the calling host thread. The main thread
// gets DLL_PROCESS_ATTACH; others DLL_THREAD_ATTACH, then on their way out
// DLL_THREAD_DETACH and their FLS callbacks. gs is restored before returning.
GuestOutcome runAsGuest(uint32_t threadId,bool mainThread,size_t stackSize,const std::function<uint32_t()>& body);
// A thread the engine created, before its host thread starts (and, if that
// fails, withdrawn): the host waits for announced threads as for running
// ones. One that starts after the run stopped does not enter the engine.
void announceGuestThread();
void withdrawGuestThread();
// ExitThread: ends the calling engine thread with this code.
[[noreturn]] void exitGuestThread(uint32_t code);

// The run's first stop: an import without implementation, an exit, a call the host cannot honour.
struct StopInfo {
    bool stopped=false;std::string reason;
    bool missingImport=false;uint32_t slot=0;uint64_t returnAddress=0;
    std::vector<uint64_t> trace;uint32_t threadId=0;
};
// Records the stop (if first) and ends the calling engine thread.
[[noreturn]] void stopRun(StopInfo info);
bool runStopping();
StopInfo stopInfo();
// Every host entry point the engine calls checks this: once the run is
// stopping, the calling engine thread ends instead of continuing.
void leaveIfStopping();
// The host waits for the first stop, then for the engine threads to end.
bool waitForStop(int timeoutMs);
unsigned liveGuestThreads();
bool waitForGuestThreads(int timeoutMs);
uint32_t currentGuestThreadId();
// The running engine threads (by id) and the TEB of one, null when it is not
// running: Thread32First/Next and NtQueryInformationThread (Azimuth 1.0.2
// writes a module's static TLS template into every thread's block).
std::vector<uint32_t> guestThreadIds();
void* guestThreadTeb(uint32_t id);
// The highest stack address a report may read on the calling engine thread (a
// host frame above all of the engine's); 0 on a host thread.
uintptr_t currentStackTop();
// Fibers (win32/Fibers.cpp): the top of the stack the calling engine thread
// now runs on, so reports and samples read only up to it; 0 returns to the
// thread's own stack.
void setFiberStackTop(uintptr_t top);
// The calling engine thread's last host calls (oldest first), for what led to a stop.
std::string recentHostCalls(unsigned count=32);
// The same for every engine thread that has called the host: "thread N: calls" lines.
std::string recentHostCallsAllThreads(unsigned count=24);
// The engine threads that called the host most since the last call (title
// 1.16, the game ran in slow motion): "thread N: calls/s (last call)", busiest first.
std::string busiestThreads(double seconds,unsigned count=8);
// Time inside the host since the last call (title 1.20): the functions with the
// most time (ms per second, calls per second, ns per call) and the threads with
// the largest share of their time inside the host. The first call starts the count.
std::string hostTimeReport(unsigned count=12);
// Where each engine thread is now: a few samples of its instruction pointer
// (a signal interrupts it) and the engine return addresses on its stack.
std::string sampleGuestThreads();
// A fault (SIGSEGV, SIGBUS, SIGILL, SIGFPE) on an engine thread becomes the
// run's stop, with the faulting instruction, the address and a rough
// backtrace; on any other thread it is written to `reportFd` and the process ends.
void installFaultHandlers(int reportFd);
}
