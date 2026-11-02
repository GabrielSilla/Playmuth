// Exception dispatch check (title 1.00): a Windows DLL built with clang for
// x86_64-pc-windows-msvc, so its C++ exceptions use MSVC's tables and
// msvcr110's __CxxFrameHandler3 and _CxxThrowException, and its __try/__except
// msvcr110's __C_specific_handler, as the game's do. The host's D3D12 check
// loads it and calls sehTest(), which returns one bit per case that worked.
// Built by build.sh (no CRT: no entry point, imports from msvcr110 and kernel32).
// The type descriptors point to type_info's vtable; msvcr110 matches catch
// types by name, so a local one does (the DLL's export is data, which an
// import library cannot give to a direct reference).
asm(".section .rdata\n.globl \"??_7type_info@@6B@\"\n\"??_7type_info@@6B@\":\n.quad 0\n.quad 0\n.text\n");
extern "C" __declspec(dllimport) void __stdcall RaiseException(unsigned long code,unsigned long flags,unsigned long count,const unsigned long long* arguments);

namespace {
struct Oops {int code;};
struct Other {int code;};
volatile int destroyed=0;
struct Guard {~Guard(){++destroyed;}};

__declspec(noinline) void deep(int code) {
    Guard guard;
    throw Oops{code};
}
__declspec(noinline) void middle(int code) {
    Guard guard;
    deep(code);
}
__declspec(noinline) int filter(unsigned long code){return code==0xe0001234u?1:0;}   // EXCEPTION_EXECUTE_HANDLER
// SEH and C++ try cannot share a function.
__declspec(noinline) int structured() {
    __try {RaiseException(0xe0001234u,0,0,nullptr);} __except(filter(0xe0001234u)) {return 16;}
    return 0;
}
}

extern "C" __declspec(dllexport) int sehTest() {
    int passed=0;
    // 1: throw and catch in one frame.
    try {throw 42;} catch(int value) {if(value==42)passed|=1;}
    // 2: through two frames with destructors.
    destroyed=0;
    try {middle(7);} catch(const Oops& oops) {if(oops.code==7&&destroyed==2)passed|=2;}
    // 3: rethrow from inside a catch block.
    try {
        try {deep(3);} catch(Oops&) {throw;}
    } catch(const Oops& oops) {if(oops.code==3)passed|=4;}
    // 4: catch (...) for a type no handler names.
    try {throw Other{5};} catch(const Oops&) {} catch(...) {passed|=8;}
    // 5: Windows SEH, __try/__except around RaiseException.
    passed|=structured();
    return passed;
}
