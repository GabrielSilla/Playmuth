#pragma once
#include <cstdint>
#include <string>
namespace azimuth::win32 {
// Called with the OS signal context. Returns true only when a guest handler
// explicitly requests EXCEPTION_CONTINUE_EXECUTION (-1).
std::string vectoredExceptionReport();
bool dispatchAccessViolation(uint64_t address,uint64_t access,void* nativeContext);
}
