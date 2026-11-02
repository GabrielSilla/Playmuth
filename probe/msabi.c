// Windows x64 calling convention on the PS5. The SDK's x86_64-sie-ps5 target
// accepts only the C convention, so the functions the engine calls, and the
// calls into the engine's code, live in this file. It is compiled for
// x86_64-pc-freebsd, which has the same CPU and the same System V ABI for
// everything else, and it calls only this project's own functions, never a
// system library, so nothing here depends on the PS5's dynamic linking.
#include "msabi.h"

static __attribute__((ms_abi)) int64_t hostSum(int64_t a,int64_t b,int64_t c,int64_t d,int64_t e,int64_t f) {
    // Back into System V code compiled for the PS5 target.
    return probeCombine(a,b,c,d,e,f);
}
const void* msabiHostSum(void){return (const void*)&hostSum;}
int64_t msabiCallGuest(const void* code) {
    return ((int64_t (__attribute__((ms_abi))*)(void))code)();
}
