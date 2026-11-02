#pragma once
#include <stdint.h>

// The boundary with the engine's Windows x64 calling convention, in
// guest_abi.c (see there for why it is a separate C translation unit).
#ifdef __cplusplus
extern "C" {
#endif
// Target of every diagnostic import stub: `mov ecx,slot; mov rax,handler; jmp rax`.
const void* guestMissingImportHandler(void);
// Calls into the engine with the Windows x64 convention.
void guestCallEntry(const void* entry);
void guestCallTlsCallback(const void* callback,uint64_t imageBase,uint32_t reason);
// LOCALE_ENUMPROCW: BOOL CALLBACK(LPWSTR name).
int32_t guestCallLocaleEnum(const void* callback,uint16_t* name);
// LPTHREAD_START_ROUTINE: DWORD WINAPI(LPVOID).
uint32_t guestCallThreadStart(const void* start,void* parameter);
// PFLS_CALLBACK_FUNCTION: VOID NTAPI(PVOID).
void guestCallFlsCallback(const void* callback,void* value);
// PTP_SIMPLE_CALLBACK: VOID CALLBACK(PTP_CALLBACK_INSTANCE, PVOID).
void guestCallSimpleCallback(const void* callback,void* instance,void* context);
// The start routine of the thread pool's wait and timer threads (Threads.cpp).
extern const void* const guestPoolThreadStart;
// WNDPROC: LRESULT CALLBACK(HWND, UINT, WPARAM, LPARAM).
int64_t guestCallWindowProc(const void* proc,void* window,uint32_t message,uint64_t w,int64_t l);
// TIMERPROC: VOID CALLBACK(HWND, UINT, UINT_PTR, DWORD).
void guestCallTimerProc(const void* proc,void* window,uint32_t message,uint64_t id,uint32_t time);
// DllMain: BOOL WINAPI(HINSTANCE, DWORD reason, LPVOID).
int32_t guestCallDllMain(const void* entry,uint64_t instance,uint32_t reason,void* reserved);
// _PVFV (the CRT's initializer tables): void(void).
void guestCallVoid(const void* function);
// qsort's comparator: int(const void*, const void*).
int32_t guestCallCompare(const void* compare,const void* a,const void* b);
// PINIT_ONCE_FN: BOOL CALLBACK(PINIT_ONCE, PVOID, PVOID*).
int32_t guestCallInitOnce(const void* function,void* once,void* parameter,void** context);
// Any Windows function or COM method of up to 8 integer or pointer
// arguments (extra ones are ignored by the callee), returning in rax.
uint64_t guestCall8(const void* function,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h);
// Called first by every Windows entry point: ends the calling engine thread
// once the run is stopping.
void hostGate(void);
// The same, naming the function called: the thread's last calls are kept for the stop report.
void hostGateNamed(const char* name);
// Its return: the function named last by hostGateNamed has returned to the engine.
void hostLeave(int* unused);
// System V side, in the host: reports the import and ends the run.
__attribute__((noreturn)) void hostMissingImport(uint32_t slot,uint64_t returnAddress,const uint64_t* stack);

// The Windows functions the host implements (win32/exports.inc), with the
// address of their Windows x64 entry point. Ends with a null entry.
typedef struct {const char* library;const char* name;const void* address;} GuestExport;
extern const GuestExport guestExports[];
#ifdef __cplusplus
}
#endif
