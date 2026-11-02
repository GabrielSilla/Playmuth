#pragma once
#include "Teb.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Windows DLLs the host loads from files, besides the engine: vkd3d-proton's
// d3d12.dll and d3d12core.dll (phase 3). Each is mapped by Image like the
// engine, its imports bound to the host's own functions (or to diagnostic
// stubs), and initialized as Windows does: TLS index and blocks, TLS
// callbacks, then DllMain. A module's HMODULE is its base address.
namespace azimuth {
// Import stubs: `mov ecx,slot; mov rax,handler; jmp rax`. The engine's
// imports use slots 0 and up, loaded DLLs ModuleImportSlots and up.
constexpr size_t ImportStubSize=32;
constexpr uint32_t ModuleImportSlots=0x100000;
void* buildImportStubs(uint32_t firstSlot,size_t count);

// Loads `name` (a Windows name, with or without path or .dll) on the calling
// engine thread, or finds it loaded. Only the DLLs the host knows to load
// from files are searched for. Returns the module's base, or 0 with the
// reason logged.
uint64_t loadModule(const std::string& name);
// The loaded module containing `address` (a handle is its base): its base, or 0.
uint64_t moduleContaining(uint64_t address);
// A loaded module by name, or 0.
uint64_t findModule(const std::string& name);
// The bases of the initialized modules, in load order (K32EnumProcessModules).
std::vector<uint64_t> moduleBases();
// "C:\windows\system32\<name>.dll", the path the module reports as its own.
std::string moduleWindowsPath(uint64_t base);
// An export by name, or by ordinal when `name` is below 0x10000; forwarders
// followed. Null when absent.
void* moduleExport(uint64_t base,const char* name);
// For logs: "d3d12core+0x1234", or empty when no module contains the address.
// Safe in a fault handler (gives up instead of waiting for the loader lock).
std::string describeModuleAddress(uint64_t address);
// "d3d12core.dll -> msvcrt.dll!qsort" for a stub slot of a loaded module.
std::string moduleImportName(uint32_t slot);

// Engine threads (GuestThreads): a starting thread gets a TLS block for each
// loaded module with static TLS and, when `notify`, DLL_THREAD_ATTACH; a
// thread on its way out gets DLL_THREAD_DETACH (when `notify`) and its blocks
// are freed. Modules loaded later give every registered thread its block.
void attachThreadToModules(ThreadBlock& block,bool notify);
void detachThreadFromModules(ThreadBlock& block,bool notify);
// Drops a thread's blocks without notifying: every way out of an engine
// thread calls it before its ThreadBlock goes (title 1.46).
void forgetThreadBlock(ThreadBlock& block);
}
