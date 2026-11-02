#pragma once
#include <stdint.h>

// The Windows x64 side of the probe, in msabi.c (see there for why it is a
// separate translation unit).

// Address of a host function the generated code calls with the Windows x64
// convention: six arguments, four in registers and two on the stack.
const void* msabiHostSum(void);
// Calls generated code as a Windows x64 function with no arguments.
int64_t msabiCallGuest(const void* code);
// System V function the Windows x64 host function calls back.
int64_t probeCombine(int64_t a,int64_t b,int64_t c,int64_t d,int64_t e,int64_t f);
