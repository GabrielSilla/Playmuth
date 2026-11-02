# APIs implementadas pelo host

Gerado por `host/tools/list_apis.py` a partir do código (não editar à mão). 625 exports em `host/src/win32/exports.inc`, mais as interfaces COM das vtables `*_abi.c` e os 763 comandos Vulkan gerados em `host/src/gpu/vulkan_commands.inc` (todos os do `vulkan_core.h` 1.4.354, cada um com um adaptador Windows x64 → System V em `gpu/vulkan_abi.c`).

Cada export é uma função `win_<nome>` em C/C++ (System V) com uma entrada `ms_<nome>` na convenção do Windows (`guest_abi.c`), que registra o nome no anel de chamadas da thread (`hostGateNamed`). Um import que não está nesta lista vira uma parada de diagnóstico com o nome da DLL e da função; `GetProcAddress` de um nome ausente devolve nulo e registra "ainda sem implementacao".

## Exports por biblioteca

### advapi32.dll (10)

| Função | Implementação |
|---|---|
| `GetTraceEnableFlags` | `win32/Threads.cpp` |
| `GetTraceEnableLevel` | `win32/Threads.cpp` |
| `GetTraceLoggerHandle` | `win32/Threads.cpp` |
| `RegCloseKey` | `win32/Advapi32.cpp` |
| `RegisterTraceGuidsW` | `win32/Threads.cpp` |
| `RegNotifyChangeKeyValue` | `win32/Advapi32.cpp` |
| `RegOpenKeyA` | `win32/Advapi32.cpp` |
| `RegQueryValueExA` | `win32/Advapi32.cpp` |
| `TraceEvent` | `win32/Threads.cpp` |
| `UnregisterTraceGuids` | `win32/Threads.cpp` |

### bcrypt.dll (7)

| Função | Implementação |
|---|---|
| `BCryptCloseAlgorithmProvider` | `win32/Bcrypt.cpp` |
| `BCryptCreateHash` | `win32/Bcrypt.cpp` |
| `BCryptDestroyHash` | `win32/Bcrypt.cpp` |
| `BCryptFinishHash` | `win32/Bcrypt.cpp` |
| `BCryptGenRandom` | `win32/Bcrypt.cpp` |
| `BCryptHashData` | `win32/Bcrypt.cpp` |
| `BCryptOpenAlgorithmProvider` | `win32/Bcrypt.cpp` |

### combase.dll (2)

| Função | Implementação |
|---|---|
| `RoInitialize` | `win32/Misc.cpp` |
| `RoUninitialize` | `win32/Misc.cpp` |

### d3d12.dll (3)

| Função | Implementação |
|---|---|
| `D3D12CreateDevice` | `win32/D3d12.cpp` |
| `D3D12GetDebugInterface` | `win32/D3d12.cpp` |
| `D3D12SerializeRootSignature` | `win32/D3d12.cpp` |

### d3dcompiler_47.dll (1)

| Função | Implementação |
|---|---|
| `D3DCompile` | `win32/D3dCompiler.cpp` |

### dxgi.dll (2)

| Função | Implementação |
|---|---|
| `CreateDXGIFactory1` | `win32/D3d12.cpp` |
| `CreateDXGIFactory2` | `win32/D3d12.cpp` |

### gdi32.dll (35)

| Função | Implementação |
|---|---|
| `BitBlt` | `win32/Gdi.cpp` |
| `CreateCompatibleBitmap` | `win32/Gdi.cpp` |
| `CreateCompatibleDC` | `win32/Gdi.cpp` |
| `CreateDIBSection` | `win32/Gdi.cpp` |
| `CreateFontW` | `win32/Gdi.cpp` |
| `D3DKMTCloseAdapter` | `win32/Advapi32.cpp` |
| `D3DKMTCreateDevice` | `win32/Advapi32.cpp` |
| `D3DKMTDestroyAllocation` | `win32/Advapi32.cpp` |
| `D3DKMTDestroyDevice` | `win32/Advapi32.cpp` |
| `D3DKMTDestroyKeyedMutex` | `win32/Advapi32.cpp` |
| `D3DKMTDestroySynchronizationObject` | `win32/Advapi32.cpp` |
| `D3DKMTEscape` | `win32/Advapi32.cpp` |
| `D3DKMTOpenAdapterFromLuid` | `win32/Advapi32.cpp` |
| `D3DKMTOpenResource2` | `win32/Advapi32.cpp` |
| `D3DKMTOpenResourceFromNtHandle` | `win32/Advapi32.cpp` |
| `D3DKMTOpenSyncObjectFromNtHandle` | `win32/Advapi32.cpp` |
| `D3DKMTQueryResourceInfo` | `win32/Advapi32.cpp` |
| `D3DKMTQueryResourceInfoFromNtHandle` | `win32/Advapi32.cpp` |
| `D3DKMTShareObjects` | `win32/Advapi32.cpp` |
| `DeleteDC` | `win32/Gdi.cpp` |
| `DeleteObject` | `win32/Gdi.cpp` |
| `ExtTextOutW` | `win32/Gdi.cpp` |
| `GdiAlphaBlend` | `win32/Gdi.cpp` |
| `GdiFlush` | `win32/Gdi.cpp` |
| `GetTextExtentPoint32W` | `win32/Gdi.cpp` |
| `IntersectClipRect` | `win32/Gdi.cpp` |
| `RestoreDC` | `win32/Gdi.cpp` |
| `SaveDC` | `win32/Gdi.cpp` |
| `SelectObject` | `win32/Gdi.cpp` |
| `SetBkColor` | `win32/Gdi.cpp` |
| `SetBkMode` | `win32/Gdi.cpp` |
| `SetBrushOrgEx` | `win32/Gdi.cpp` |
| `SetStretchBltMode` | `win32/Gdi.cpp` |
| `SetTextColor` | `win32/Gdi.cpp` |
| `StretchDIBits` | `win32/Gdi.cpp` |

### kernel32.dll (320)

| Função | Implementação |
|---|---|
| `AcquireSRWLockExclusive` | `win32/Kernel32.cpp` |
| `AcquireSRWLockShared` | `win32/Kernel32.cpp` |
| `AddVectoredExceptionHandler` | `win32/VectoredExceptions.cpp` |
| `AreFileApisANSI` | `win32/Files.cpp` |
| `CallbackMayRunLong` | `win32/Threads.cpp` |
| `CancelWaitableTimer` | `win32/Threads.cpp` |
| `ChangeTimerQueueTimer` | `win32/Threads.cpp` |
| `CloseHandle` | `win32/Files.cpp` |
| `CloseThreadpoolTimer` | `win32/Threads.cpp` |
| `CloseThreadpoolWait` | `win32/Threads.cpp` |
| `CompareFileTime` | `win32/Kernel32.cpp` |
| `CompareStringEx` | `win32/Locale.cpp` |
| `CompareStringOrdinal` | `win32/Locale.cpp` |
| `ConvertFiberToThread` | `win32/Fibers.cpp` |
| `ConvertThreadToFiber` | `win32/Fibers.cpp` |
| `ConvertThreadToFiberEx` | `win32/Fibers.cpp` |
| `CopyFileW` | `win32/Files.cpp` |
| `CreateDirectoryW` | `win32/Files.cpp` |
| `CreateEventA` | `win32/Threads.cpp` |
| `CreateEventExW` | `win32/Threads.cpp` |
| `CreateEventW` | `win32/Threads.cpp` |
| `CreateFiber` | `win32/Fibers.cpp` |
| `CreateFiberEx` | `win32/Fibers.cpp` |
| `CreateFileA` | `win32/Files.cpp` |
| `CreateFileMappingA` | `win32/Files.cpp` |
| `CreateFileMappingW` | `win32/Files.cpp` |
| `CreateFileW` | `win32/Files.cpp` |
| `CreateIoCompletionPort` | `win32/Threads.cpp` |
| `CreateMutexA` | `win32/Threads.cpp` |
| `CreateMutexW` | `win32/Threads.cpp` |
| `CreateRemoteThreadEx` | `win32/Threads.cpp` |
| `CreateSemaphoreA` | `win32/Threads.cpp` |
| `CreateSemaphoreExW` | `win32/Threads.cpp` |
| `CreateThread` | `win32/Threads.cpp` |
| `CreateThreadpoolTimer` | `win32/Threads.cpp` |
| `CreateThreadpoolWait` | `win32/Threads.cpp` |
| `CreateTimerQueue` | `win32/Threads.cpp` |
| `CreateTimerQueueTimer` | `win32/Threads.cpp` |
| `CreateUmsCompletionList` | `win32/Threads.cpp` |
| `CreateUmsThreadContext` | `win32/Threads.cpp` |
| `CreateWaitableTimerExW` | `win32/Threads.cpp` |
| `DecodePointer` | `win32/Kernel32.cpp` |
| `DeleteCriticalSection` | `win32/Kernel32.cpp` |
| `DeleteFiber` | `win32/Fibers.cpp` |
| `DeleteFileA` | `win32/Files.cpp` |
| `DeleteFileW` | `win32/Files.cpp` |
| `DeleteProcThreadAttributeList` | `win32/Threads.cpp` |
| `DeleteTimerQueueEx` | `win32/Threads.cpp` |
| `DeleteTimerQueueTimer` | `win32/Threads.cpp` |
| `DeleteUmsCompletionList` | `win32/Threads.cpp` |
| `DeleteUmsThreadContext` | `win32/Threads.cpp` |
| `DequeueUmsCompletionListItems` | `win32/Threads.cpp` |
| `DeviceIoControl` | `win32/Files.cpp` |
| `DisableThreadLibraryCalls` | `win32/Kernel32.cpp` |
| `DisassociateCurrentThreadFromCallback` | `win32/Threads.cpp` |
| `DuplicateHandle` | `win32/Threads.cpp` |
| `EncodePointer` | `win32/Kernel32.cpp` |
| `EnterCriticalSection` | `win32/Kernel32.cpp` |
| `EnterUmsSchedulingMode` | `win32/Threads.cpp` |
| `EnumSystemLocalesEx` | `win32/Locale.cpp` |
| `EnumSystemLocalesW` | `win32/Locale.cpp` |
| `ExecuteUmsThread` | `win32/Threads.cpp` |
| `ExitProcess` | `win32/Kernel32.cpp` |
| `ExitThread` | `win32/Threads.cpp` |
| `FileTimeToLocalFileTime` | `win32/Kernel32.cpp` |
| `FileTimeToSystemTime` | `win32/Kernel32.cpp` |
| `FindClose` | `win32/Files.cpp` |
| `FindFirstFileExW` | `win32/Files.cpp` |
| `FindFirstFileW` | `win32/Files.cpp` |
| `FindNextFileW` | `win32/Files.cpp` |
| `FlsAlloc` | `win32/Kernel32.cpp` |
| `FlsFree` | `win32/Kernel32.cpp` |
| `FlsGetValue` | `win32/Kernel32.cpp` |
| `FlsSetValue` | `win32/Kernel32.cpp` |
| `FlushFileBuffers` | `win32/Files.cpp` |
| `FlushInstructionCache` | `win32/Misc.cpp` |
| `FlushProcessWriteBuffers` | `win32/Threads.cpp` |
| `FormatMessageA` | `win32/Kernel32.cpp` |
| `FormatMessageW` | `win32/Misc.cpp` |
| `FreeEnvironmentStringsW` | `win32/Kernel32.cpp` |
| `FreeLibrary` | `win32/Kernel32.cpp` |
| `FreeLibraryAndExitThread` | `win32/Threads.cpp` |
| `FreeLibraryWhenCallbackReturns` | `win32/Threads.cpp` |
| `GetACP` | `win32/Kernel32.cpp` |
| `GetCommandLineA` | `win32/Kernel32.cpp` |
| `GetCommandLineW` | `win32/Kernel32.cpp` |
| `GetConsoleMode` | `win32/Misc.cpp` |
| `GetConsoleOutputCP` | `win32/Misc.cpp` |
| `GetConsoleScreenBufferInfo` | `win32/Kernel32.cpp` |
| `GetCPInfo` | `win32/Kernel32.cpp` |
| `GetCurrentDirectoryA` | `win32/Files.cpp` |
| `GetCurrentDirectoryW` | `win32/Files.cpp` |
| `GetCurrentPackageFamilyName` | `win32/Kernel32.cpp` |
| `GetCurrentPackageFullName` | `win32/Kernel32.cpp` |
| `GetCurrentPackageId` | `win32/Kernel32.cpp` |
| `GetCurrentProcess` | `win32/Kernel32.cpp` |
| `GetCurrentProcessId` | `win32/Kernel32.cpp` |
| `GetCurrentProcessorNumber` | `win32/Threads.cpp` |
| `GetCurrentProcessorNumberEx` | `win32/Threads.cpp` |
| `GetCurrentThread` | `win32/Kernel32.cpp` |
| `GetCurrentThreadId` | `win32/Kernel32.cpp` |
| `GetCurrentUmsThread` | `win32/Threads.cpp` |
| `GetDateFormatEx` | `win32/Locale.cpp` |
| `GetDiskFreeSpaceExW` | `win32/Files.cpp` |
| `GetDriveTypeW` | `win32/Files.cpp` |
| `GetEnvironmentStringsW` | `win32/Kernel32.cpp` |
| `GetEnvironmentVariableA` | `win32/Kernel32.cpp` |
| `GetEnvironmentVariableW` | `win32/Misc.cpp` |
| `GetExitCodeThread` | `win32/Threads.cpp` |
| `GetFileAttributesExW` | `win32/Files.cpp` |
| `GetFileAttributesW` | `win32/Files.cpp` |
| `GetFileInformationByHandle` | `win32/Files.cpp` |
| `GetFileInformationByHandleEx` | `win32/Files.cpp` |
| `GetFileSize` | `win32/Files.cpp` |
| `GetFileSizeEx` | `win32/Files.cpp` |
| `GetFileType` | `win32/Files.cpp` |
| `GetFinalPathNameByHandleW` | `win32/Files.cpp` |
| `GetFullPathNameA` | `win32/Files.cpp` |
| `GetFullPathNameW` | `win32/Files.cpp` |
| `GetLastError` | `win32/Kernel32.cpp` |
| `GetLocaleInfoEx` | `win32/Locale.cpp` |
| `GetLocaleInfoW` | `win32/Locale.cpp` |
| `GetLocalTime` | `win32/Kernel32.cpp` |
| `GetLogicalDrives` | `win32/Files.cpp` |
| `GetLogicalProcessorInformation` | `win32/Threads.cpp` |
| `GetLogicalProcessorInformationEx` | `win32/Threads.cpp` |
| `GetModuleFileNameA` | `win32/Kernel32.cpp` |
| `GetModuleFileNameW` | `win32/Kernel32.cpp` |
| `GetModuleHandleA` | `win32/Kernel32.cpp` |
| `GetModuleHandleExA` | `win32/Kernel32.cpp` |
| `GetModuleHandleExW` | `win32/Kernel32.cpp` |
| `GetModuleHandleW` | `win32/Kernel32.cpp` |
| `GetNativeSystemInfo` | `win32/Threads.cpp` |
| `GetNextUmsListItem` | `win32/Threads.cpp` |
| `GetNumaHighestNodeNumber` | `win32/Threads.cpp` |
| `GetOEMCP` | `win32/Kernel32.cpp` |
| `GetOverlappedResultEx` | `win32/Misc.cpp` |
| `GetPrivateProfileStringW` | `win32/Profile.cpp` |
| `GetProcAddress` | `win32/Kernel32.cpp` |
| `GetProcessAffinityMask` | `win32/Threads.cpp` |
| `GetProcessHeap` | `win32/Kernel32.cpp` |
| `GetProcessId` | `win32/Threads.cpp` |
| `GetProcessIdOfThread` | `win32/Threads.cpp` |
| `GetQueuedCompletionStatus` | `win32/Threads.cpp` |
| `GetStartupInfoW` | `win32/Kernel32.cpp` |
| `GetStdHandle` | `win32/Kernel32.cpp` |
| `GetStringTypeW` | `win32/Locale.cpp` |
| `GetSystemDirectoryA` | `win32/Kernel32.cpp` |
| `GetSystemDirectoryW` | `win32/Misc.cpp` |
| `GetSystemInfo` | `win32/Threads.cpp` |
| `GetSystemTime` | `win32/Kernel32.cpp` |
| `GetSystemTimeAsFileTime` | `win32/Kernel32.cpp` |
| `GetSystemTimePreciseAsFileTime` | `win32/Kernel32.cpp` |
| `GetTempPath2W` | `win32/Files.cpp` |
| `GetTempPathA` | `win32/Files.cpp` |
| `GetTempPathW` | `win32/Files.cpp` |
| `GetThreadDescription` | `win32/Threads.cpp` |
| `GetThreadGroupAffinity` | `win32/Threads.cpp` |
| `GetThreadId` | `win32/Threads.cpp` |
| `GetThreadPriority` | `win32/Threads.cpp` |
| `GetThreadTimes` | `win32/Threads.cpp` |
| `GetTickCount` | `win32/Kernel32.cpp` |
| `GetTickCount64` | `win32/Kernel32.cpp` |
| `GetTimeFormatEx` | `win32/Locale.cpp` |
| `GetTimeZoneInformation` | `win32/Kernel32.cpp` |
| `GetUmsCompletionListEvent` | `win32/Threads.cpp` |
| `GetUserDefaultLCID` | `win32/Locale.cpp` |
| `GetUserDefaultLocaleName` | `win32/Locale.cpp` |
| `GetVersionExW` | `win32/Kernel32.cpp` |
| `GetWriteWatch` | `win32/Misc.cpp` |
| `GlobalMemoryStatusEx` | `win32/Threads.cpp` |
| `HeapAlloc` | `win32/Kernel32.cpp` |
| `HeapCompact` | `win32/Kernel32.cpp` |
| `HeapFree` | `win32/Kernel32.cpp` |
| `HeapQueryInformation` | `win32/Kernel32.cpp` |
| `HeapReAlloc` | `win32/Kernel32.cpp` |
| `HeapSize` | `win32/Kernel32.cpp` |
| `HeapValidate` | `win32/Kernel32.cpp` |
| `HeapWalk` | `win32/Kernel32.cpp` |
| `InitializeConditionVariable` | `win32/Threads.cpp` |
| `InitializeCriticalSection` | `win32/Kernel32.cpp` |
| `InitializeCriticalSectionAndSpinCount` | `win32/Kernel32.cpp` |
| `InitializeCriticalSectionEx` | `win32/Kernel32.cpp` |
| `InitializeProcThreadAttributeList` | `win32/Threads.cpp` |
| `InitializeSListHead` | `win32/Kernel32.cpp` |
| `InitializeSRWLock` | `win32/Kernel32.cpp` |
| `InitOnceBeginInitialize` | `win32/Threads.cpp` |
| `InitOnceComplete` | `win32/Threads.cpp` |
| `InitOnceExecuteOnce` | `win32/Threads.cpp` |
| `InterlockedFlushSList` | `win32/Kernel32.cpp` |
| `InterlockedPopEntrySList` | `win32/Kernel32.cpp` |
| `InterlockedPushEntrySList` | `win32/Kernel32.cpp` |
| `IsDBCSLeadByteEx` | `win32/Kernel32.cpp` |
| `IsDebuggerPresent` | `win32/Kernel32.cpp` |
| `IsProcessorFeaturePresent` | `win32/Kernel32.cpp` |
| `IsThreadAFiber` | `win32/Fibers.cpp` |
| `IsThreadpoolTimerSet` | `win32/Threads.cpp` |
| `IsValidCodePage` | `win32/Kernel32.cpp` |
| `IsValidLocale` | `win32/Locale.cpp` |
| `IsValidLocaleName` | `win32/Locale.cpp` |
| `LCIDToLocaleName` | `win32/Locale.cpp` |
| `LCMapStringEx` | `win32/Locale.cpp` |
| `LCMapStringW` | `win32/Locale.cpp` |
| `LeaveCriticalSection` | `win32/Kernel32.cpp` |
| `LoadLibraryA` | `gpu/D3d12Check.cpp` |
| `LoadLibraryExA` | `win32/Kernel32.cpp` |
| `LoadLibraryExW` | `win32/Kernel32.cpp` |
| `LoadLibraryW` | `win32/Kernel32.cpp` |
| `LocaleNameToLCID` | `win32/Locale.cpp` |
| `LocalFileTimeToFileTime` | `win32/Kernel32.cpp` |
| `LocalFree` | `win32/Kernel32.cpp` |
| `LockFileEx` | `win32/Files.cpp` |
| `MapViewOfFile` | `win32/Files.cpp` |
| `MapViewOfFile3` | `win32/Memory.cpp` |
| `MoveFileA` | `win32/Files.cpp` |
| `MoveFileExW` | `win32/Files.cpp` |
| `MultiByteToWideChar` | `win32/Kernel32.cpp` |
| `OpenThread` | `win32/Threads.cpp` |
| `OutputDebugStringA` | `win32/Kernel32.cpp` |
| `OutputDebugStringW` | `win32/Kernel32.cpp` |
| `PostQueuedCompletionStatus` | `win32/Threads.cpp` |
| `ProcessIdToSessionId` | `win32/Kernel32.cpp` |
| `QueryDepthSList` | `win32/Kernel32.cpp` |
| `QueryPerformanceCounter` | `win32/Kernel32.cpp` |
| `QueryPerformanceFrequency` | `win32/Kernel32.cpp` |
| `QueryUmsThreadInformation` | `win32/Threads.cpp` |
| `ReadFile` | `win32/Files.cpp` |
| `RegisterWaitForSingleObject` | `win32/Threads.cpp` |
| `ReleaseMutex` | `win32/Threads.cpp` |
| `ReleaseSemaphore` | `win32/Threads.cpp` |
| `ReleaseSRWLockExclusive` | `win32/Kernel32.cpp` |
| `ReleaseSRWLockShared` | `win32/Kernel32.cpp` |
| `RemoveDirectoryW` | `win32/Files.cpp` |
| `RemoveVectoredExceptionHandler` | `win32/VectoredExceptions.cpp` |
| `ReplaceFileA` | `win32/Files.cpp` |
| `ResetEvent` | `win32/Threads.cpp` |
| `ResetWriteWatch` | `win32/Misc.cpp` |
| `ResolveLocaleName` | `win32/Locale.cpp` |
| `ResumeThread` | `win32/Threads.cpp` |
| `RtlAddFunctionTable` | `win32/Exceptions.cpp` |
| `RtlCaptureStackBackTrace` | `win32/Misc.cpp` |
| `RtlDeleteFunctionTable` | `win32/Exceptions.cpp` |
| `RtlLookupFunctionEntry` | `win32/Exceptions.cpp` |
| `RtlPcToFileHeader` | `win32/Exceptions.cpp` |
| `RtlVirtualUnwind` | `win32/Exceptions.cpp` |
| `SetConsoleCtrlHandler` | `win32/Misc.cpp` |
| `SetConsoleTextAttribute` | `win32/Kernel32.cpp` |
| `SetCurrentDirectoryA` | `win32/Files.cpp` |
| `SetCurrentDirectoryW` | `win32/Files.cpp` |
| `SetDefaultDllDirectories` | `win32/Kernel32.cpp` |
| `SetEndOfFile` | `win32/Files.cpp` |
| `SetEnvironmentVariableA` | `win32/Kernel32.cpp` |
| `SetEnvironmentVariableW` | `win32/Misc.cpp` |
| `SetErrorMode` | `win32/Kernel32.cpp` |
| `SetEvent` | `win32/Threads.cpp` |
| `SetFileAttributesW` | `win32/Files.cpp` |
| `SetFileCompletionNotificationModes` | `win32/Files.cpp` |
| `SetFileInformationByHandle` | `win32/Files.cpp` |
| `SetFilePointer` | `win32/Files.cpp` |
| `SetFilePointerEx` | `win32/Files.cpp` |
| `SetFileTime` | `win32/Files.cpp` |
| `SetLastError` | `win32/Kernel32.cpp` |
| `SetProcessAffinityMask` | `win32/Threads.cpp` |
| `SetStdHandle` | `win32/Kernel32.cpp` |
| `SetThreadAffinityMask` | `win32/Threads.cpp` |
| `SetThreadDescription` | `win32/Threads.cpp` |
| `SetThreadGroupAffinity` | `win32/Threads.cpp` |
| `SetThreadpoolTimer` | `win32/Threads.cpp` |
| `SetThreadpoolWait` | `win32/Threads.cpp` |
| `SetThreadPriority` | `win32/Threads.cpp` |
| `SetThreadStackGuarantee` | `win32/Threads.cpp` |
| `SetUmsThreadInformation` | `win32/Threads.cpp` |
| `SetUnhandledExceptionFilter` | `win32/Kernel32.cpp` |
| `SetWaitableTimer` | `win32/Threads.cpp` |
| `SetWaitableTimerEx` | `win32/Threads.cpp` |
| `SignalObjectAndWait` | `win32/Threads.cpp` |
| `Sleep` | `win32/Threads.cpp` |
| `SleepConditionVariableCS` | `win32/Threads.cpp` |
| `SleepConditionVariableSRW` | `win32/Threads.cpp` |
| `SleepEx` | `win32/Threads.cpp` |
| `SwitchToFiber` | `win32/Fibers.cpp` |
| `SwitchToThread` | `win32/Threads.cpp` |
| `SystemTimeToFileTime` | `win32/Kernel32.cpp` |
| `SystemTimeToTzSpecificLocalTime` | `win32/Kernel32.cpp` |
| `TerminateProcess` | `win32/Kernel32.cpp` |
| `TlsAlloc` | `win32/Kernel32.cpp` |
| `TlsFree` | `win32/Kernel32.cpp` |
| `TlsGetValue` | `win32/Kernel32.cpp` |
| `TlsSetValue` | `win32/Kernel32.cpp` |
| `TryAcquireSRWLockExclusive` | `win32/Kernel32.cpp` |
| `TryEnterCriticalSection` | `win32/Kernel32.cpp` |
| `TrySubmitThreadpoolCallback` | `win32/Threads.cpp` |
| `UmsThreadYield` | `win32/Threads.cpp` |
| `UnhandledExceptionFilter` | `win32/Misc.cpp` |
| `UnlockFileEx` | `win32/Files.cpp` |
| `UnmapViewOfFile` | `win32/Files.cpp` |
| `UnmapViewOfFile2` | `win32/Memory.cpp` |
| `UnregisterWait` | `win32/Threads.cpp` |
| `UnregisterWaitEx` | `win32/Threads.cpp` |
| `UpdateProcThreadAttribute` | `win32/Threads.cpp` |
| `VirtualAlloc` | `win32/Memory.cpp` |
| `VirtualAlloc2` | `win32/Memory.cpp` |
| `VirtualFree` | `win32/Memory.cpp` |
| `VirtualProtect` | `win32/Memory.cpp` |
| `VirtualQuery` | `win32/Memory.cpp` |
| `WaitForMultipleObjects` | `win32/Threads.cpp` |
| `WaitForMultipleObjectsEx` | `win32/Threads.cpp` |
| `WaitForSingleObject` | `win32/Threads.cpp` |
| `WaitForSingleObjectEx` | `win32/Threads.cpp` |
| `WaitForThreadpoolTimerCallbacks` | `win32/Threads.cpp` |
| `WaitForThreadpoolWaitCallbacks` | `win32/Threads.cpp` |
| `WaitOnAddress` | `win32/Threads.cpp` |
| `WakeAllConditionVariable` | `win32/Threads.cpp` |
| `WakeByAddressAll` | `win32/Threads.cpp` |
| `WakeByAddressSingle` | `win32/Threads.cpp` |
| `WakeConditionVariable` | `win32/Threads.cpp` |
| `WideCharToMultiByte` | `win32/Kernel32.cpp` |
| `WriteConsoleW` | `win32/Misc.cpp` |
| `WriteFile` | `win32/Files.cpp` |
| `WritePrivateProfileStringW` | `win32/Profile.cpp` |

### mfplat.dll (4)

| Função | Implementação |
|---|---|
| `MFCreateMediaType` | `win32/Desktop.cpp` |
| `MFInitMediaTypeFromWaveFormatEx` | `win32/Desktop.cpp` |
| `MFShutdown` | `win32/Desktop.cpp` |
| `MFStartup` | `win32/Desktop.cpp` |

### mfreadwrite.dll (1)

| Função | Implementação |
|---|---|
| `MFCreateSourceReaderFromURL` | `win32/Desktop.cpp` |

### msvcrt.dll (158)

| Função | Implementação |
|---|---|
| `___lc_codepage_func` | `win32/Msvcrt.cpp` |
| `___mb_cur_max_func` | `win32/Msvcrt.cpp` |
| `__acrt_iob_func` | `win32/Msvcrt.cpp` |
| `__C_specific_handler` | `win32/Msvcrt.cpp` |
| `__daylight` | `win32/Msvcrt.cpp` |
| `__iob_func` | `win32/Msvcrt.cpp` |
| `__p___argc` | `win32/Msvcrt.cpp` |
| `__p___argv` | `win32/Msvcrt.cpp` |
| `__p___wargv` | `win32/Msvcrt.cpp` |
| `__p__environ` | `win32/Msvcrt.cpp` |
| `__p__wenviron` | `win32/Msvcrt.cpp` |
| `__stdio_common_vfprintf` | `win32/Msvcrt.cpp` |
| `__stdio_common_vfwprintf` | `win32/Msvcrt.cpp` |
| `__stdio_common_vsprintf` | `win32/Msvcrt.cpp` |
| `__stdio_common_vsscanf` | `win32/Msvcrt.cpp` |
| `__timezone` | `win32/Msvcrt.cpp` |
| `__tzname` | `win32/Msvcrt.cpp` |
| `_aligned_free` | `win32/Msvcrt.cpp` |
| `_aligned_malloc` | `win32/Msvcrt.cpp` |
| `_aligned_realloc` | `win32/Msvcrt.cpp` |
| `_amsg_exit` | `win32/Msvcrt.cpp` |
| `_assert` | `win32/Msvcrt.cpp` |
| `_beginthreadex` | `win32/Msvcrt.cpp` |
| `_close` | `win32/Msvcrt.cpp` |
| `_configure_narrow_argv` | `win32/Msvcrt.cpp` |
| `_configure_wide_argv` | `win32/Msvcrt.cpp` |
| `_crt_at_quick_exit` | `win32/Msvcrt.cpp` |
| `_crt_atexit` | `win32/Msvcrt.cpp` |
| `_errno` | `win32/Msvcrt.cpp` |
| `_execute_onexit_table` | `win32/Msvcrt.cpp` |
| `_exit` | `win32/Msvcrt.cpp` |
| `_fdopen` | `win32/Msvcrt.cpp` |
| `_filelengthi64` | `win32/Msvcrt.cpp` |
| `_fileno` | `win32/Msvcrt.cpp` |
| `_fstat64` | `win32/Msvcrt.cpp` |
| `_get_osfhandle` | `win32/Msvcrt.cpp` |
| `_gmtime64_s` | `win32/Msvcrt.cpp` |
| `_initialize_narrow_environment` | `win32/Msvcrt.cpp` |
| `_initialize_onexit_table` | `win32/Msvcrt.cpp` |
| `_initialize_wide_environment` | `win32/Msvcrt.cpp` |
| `_initterm` | `win32/Msvcrt.cpp` |
| `_isatty` | `win32/Msvcrt.cpp` |
| `_localtime64_s` | `win32/Msvcrt.cpp` |
| `_lock` | `win32/Msvcrt.cpp` |
| `_lseeki64` | `win32/Msvcrt.cpp` |
| `_mktime64` | `win32/Msvcrt.cpp` |
| `_open` | `win32/Msvcrt.cpp` |
| `_read` | `win32/Msvcrt.cpp` |
| `_register_onexit_function` | `win32/Msvcrt.cpp` |
| `_set_new_mode` | `win32/Msvcrt.cpp` |
| `_sopen` | `win32/Msvcrt.cpp` |
| `_strtoi64` | `win32/Msvcrt.cpp` |
| `_strtoui64` | `win32/Msvcrt.cpp` |
| `_tzset` | `win32/Msvcrt.cpp` |
| `_unlock` | `win32/Msvcrt.cpp` |
| `_wfopen` | `win32/Msvcrt.cpp` |
| `_write` | `win32/Msvcrt.cpp` |
| `_wsopen` | `win32/Msvcrt.cpp` |
| `_XcptFilter` | `win32/Msvcrt.cpp` |
| `abort` | `win32/Msvcrt.cpp` |
| `acos` | `win32/Msvcrt.cpp` |
| `asin` | `win32/Msvcrt.cpp` |
| `atan` | `win32/Msvcrt.cpp` |
| `atan2` | `win32/Msvcrt.cpp` |
| `bsearch` | `win32/Msvcrt.cpp` |
| `calloc` | `win32/Msvcrt.cpp` |
| `clock` | `win32/Msvcrt.cpp` |
| `cos` | `win32/Msvcrt.cpp` |
| `cosh` | `win32/Msvcrt.cpp` |
| `exp` | `win32/Msvcrt.cpp` |
| `exp2` | `win32/Msvcrt.cpp` |
| `fabs` | `win32/Msvcrt.cpp` |
| `fclose` | `win32/Msvcrt.cpp` |
| `fflush` | `win32/Msvcrt.cpp` |
| `fgetpos` | `win32/Msvcrt.cpp` |
| `fgets` | `win32/Msvcrt.cpp` |
| `floor` | `win32/Msvcrt.cpp` |
| `fmax` | `win32/Msvcrt.cpp` |
| `fopen` | `win32/Msvcrt.cpp` |
| `fputc` | `win32/Msvcrt.cpp` |
| `fputs` | `win32/Msvcrt.cpp` |
| `fputwc` | `win32/Msvcrt.cpp` |
| `fread` | `win32/Msvcrt.cpp` |
| `free` | `win32/Msvcrt.cpp` |
| `frexp` | `win32/Msvcrt.cpp` |
| `fseek` | `win32/Msvcrt.cpp` |
| `fsetpos` | `win32/Msvcrt.cpp` |
| `ftell` | `win32/Msvcrt.cpp` |
| `fwrite` | `win32/Msvcrt.cpp` |
| `getc` | `win32/Msvcrt.cpp` |
| `getenv` | `win32/Msvcrt.cpp` |
| `getwc` | `win32/Msvcrt.cpp` |
| `hypot` | `win32/Msvcrt.cpp` |
| `isalnum` | `win32/Msvcrt.cpp` |
| `isalpha` | `win32/Msvcrt.cpp` |
| `isprint` | `win32/Msvcrt.cpp` |
| `isspace` | `win32/Msvcrt.cpp` |
| `iswctype` | `win32/Msvcrt.cpp` |
| `isxdigit` | `win32/Msvcrt.cpp` |
| `localeconv` | `win32/Msvcrt.cpp` |
| `log` | `win32/Msvcrt.cpp` |
| `log10` | `win32/Msvcrt.cpp` |
| `malloc` | `win32/Msvcrt.cpp` |
| `memchr` | `win32/Msvcrt.cpp` |
| `memcmp` | `win32/Msvcrt.cpp` |
| `memcpy` | `win32/Msvcrt.cpp` |
| `memmove` | `win32/Msvcrt.cpp` |
| `memset` | `win32/Msvcrt.cpp` |
| `pow` | `win32/Msvcrt.cpp` |
| `putc` | `win32/Msvcrt.cpp` |
| `putwc` | `win32/Msvcrt.cpp` |
| `qsort` | `win32/Msvcrt.cpp` |
| `rand` | `win32/Msvcrt.cpp` |
| `rand_s` | `win32/Msvcrt.cpp` |
| `realloc` | `win32/Msvcrt.cpp` |
| `rewind` | `win32/Msvcrt.cpp` |
| `round` | `win32/Msvcrt.cpp` |
| `setlocale` | `win32/Msvcrt.cpp` |
| `setvbuf` | `win32/Msvcrt.cpp` |
| `sin` | `win32/Msvcrt.cpp` |
| `sinf` | `win32/Msvcrt.cpp` |
| `sinh` | `win32/Msvcrt.cpp` |
| `sqrt` | `win32/Msvcrt.cpp` |
| `strchr` | `win32/Msvcrt.cpp` |
| `strcmp` | `win32/Msvcrt.cpp` |
| `strcoll` | `win32/Msvcrt.cpp` |
| `strcpy` | `win32/Msvcrt.cpp` |
| `strcspn` | `win32/Msvcrt.cpp` |
| `strerror` | `win32/Msvcrt.cpp` |
| `strftime` | `win32/Msvcrt.cpp` |
| `strlen` | `win32/Msvcrt.cpp` |
| `strncat` | `win32/Msvcrt.cpp` |
| `strncmp` | `win32/Msvcrt.cpp` |
| `strncpy` | `win32/Msvcrt.cpp` |
| `strrchr` | `win32/Msvcrt.cpp` |
| `strspn` | `win32/Msvcrt.cpp` |
| `strstr` | `win32/Msvcrt.cpp` |
| `strtod` | `win32/Msvcrt.cpp` |
| `strtol` | `win32/Msvcrt.cpp` |
| `strtoll` | `win32/Msvcrt.cpp` |
| `strtoul` | `win32/Msvcrt.cpp` |
| `strtoull` | `win32/Msvcrt.cpp` |
| `strxfrm` | `win32/Msvcrt.cpp` |
| `tan` | `win32/Msvcrt.cpp` |
| `tanh` | `win32/Msvcrt.cpp` |
| `tolower` | `win32/Msvcrt.cpp` |
| `towlower` | `win32/Msvcrt.cpp` |
| `towupper` | `win32/Msvcrt.cpp` |
| `ungetc` | `win32/Msvcrt.cpp` |
| `ungetwc` | `win32/Msvcrt.cpp` |
| `vfprintf` | `win32/Msvcrt.cpp` |
| `vsprintf_s` | `win32/Msvcrt.cpp` |
| `wcscat` | `win32/Msvcrt.cpp` |
| `wcscoll` | `win32/Msvcrt.cpp` |
| `wcscpy` | `win32/Msvcrt.cpp` |
| `wcsftime` | `win32/Msvcrt.cpp` |
| `wcslen` | `win32/Msvcrt.cpp` |
| `wcsxfrm` | `win32/Msvcrt.cpp` |

### ntdll.dll (2)

| Função | Implementação |
|---|---|
| `NtDelayExecution` | `win32/Threads.cpp` |
| `RtlGetVersion` | `win32/Misc.cpp` |

### ole32.dll (10)

| Função | Implementação |
|---|---|
| `CoCreateGuid` | `win32/Misc.cpp` |
| `CoCreateInstance` | `win32/Misc.cpp` |
| `CoGetApartmentType` | `win32/Misc.cpp` |
| `CoGetInterfaceAndReleaseStream` | `win32/Misc.cpp` |
| `CoInitializeEx` | `win32/Misc.cpp` |
| `CoMarshalInterThreadInterfaceInStream` | `win32/Misc.cpp` |
| `CoTaskMemAlloc` | `win32/Misc.cpp` |
| `CoTaskMemFree` | `win32/Misc.cpp` |
| `CoUninitialize` | `win32/Misc.cpp` |
| `CreateStreamOnHGlobal` | `win32/Misc.cpp` |

### shell32.dll (1)

| Função | Implementação |
|---|---|
| `SHCreateItemFromParsingName` | `win32/Misc.cpp` |

### user32.dll (45)

| Função | Implementação |
|---|---|
| `AdjustWindowRectEx` | `win32/User32.cpp` |
| `CharUpperBuffW` | `win32/User32.cpp` |
| `CreateWindowExW` | `win32/User32.cpp` |
| `DefWindowProcW` | `win32/User32.cpp` |
| `DestroyWindow` | `win32/User32.cpp` |
| `DispatchMessageW` | `win32/User32.cpp` |
| `DrawTextW` | `win32/Gdi.cpp` |
| `EnableWindow` | `win32/User32.cpp` |
| `GetAsyncKeyState` | `win32/User32.cpp` |
| `GetClientRect` | `win32/User32.cpp` |
| `GetCursorPos` | `win32/User32.cpp` |
| `GetDC` | `win32/Gdi.cpp` |
| `GetForegroundWindow` | `win32/User32.cpp` |
| `GetKeyState` | `win32/User32.cpp` |
| `GetMessageW` | `win32/User32.cpp` |
| `GetMonitorInfoW` | `win32/User32.cpp` |
| `GetSystemMetrics` | `win32/User32.cpp` |
| `GetWindow` | `win32/User32.cpp` |
| `GetWindowLongPtrW` | `win32/User32.cpp` |
| `GetWindowPlacement` | `win32/User32.cpp` |
| `GetWindowThreadProcessId` | `win32/User32.cpp` |
| `IsWindow` | `win32/User32.cpp` |
| `IsWindowEnabled` | `win32/User32.cpp` |
| `IsWindowVisible` | `win32/User32.cpp` |
| `KillTimer` | `win32/User32.cpp` |
| `LoadCursorW` | `win32/User32.cpp` |
| `LoadImageW` | `win32/User32.cpp` |
| `MessageBoxW` | `win32/User32.cpp` |
| `MonitorFromWindow` | `win32/User32.cpp` |
| `MsgWaitForMultipleObjects` | `win32/User32.cpp` |
| `MsgWaitForMultipleObjectsEx` | `win32/User32.cpp` |
| `PeekMessageW` | `win32/User32.cpp` |
| `PostMessageW` | `win32/User32.cpp` |
| `RegisterClassExW` | `win32/User32.cpp` |
| `ReleaseDC` | `win32/Gdi.cpp` |
| `SetCursor` | `win32/User32.cpp` |
| `SetFocus` | `win32/User32.cpp` |
| `SetForegroundWindow` | `win32/User32.cpp` |
| `SetTimer` | `win32/User32.cpp` |
| `SetWindowLongPtrW` | `win32/User32.cpp` |
| `SetWindowPlacement` | `win32/User32.cpp` |
| `SetWindowPos` | `win32/User32.cpp` |
| `ShowWindow` | `win32/User32.cpp` |
| `SystemParametersInfoW` | `win32/User32.cpp` |
| `TranslateMessage` | `win32/User32.cpp` |

### vulkan_1.dll (2)

| Função | Implementação |
|---|---|
| `vkGetDeviceProcAddr` | `gpu/Vulkan.cpp` |
| `vkGetInstanceProcAddr` | `gpu/Vulkan.cpp` |

### windowscodecs.dll (1)

| Função | Implementação |
|---|---|
| `WICCreateImagingFactory_Proxy` | `win32/Wic.cpp` |

### ws2_32.dll (15)

| Função | Implementação |
|---|---|
| `htonl` | `win32/Desktop.cpp` |
| `htons` | `win32/Desktop.cpp` |
| `inet_addr` | `win32/Desktop.cpp` |
| `inet_ntoa` | `win32/Desktop.cpp` |
| `ntohl` | `win32/Desktop.cpp` |
| `ntohs` | `win32/Desktop.cpp` |
| `WSACleanup` | `win32/Desktop.cpp` |
| `WSACloseEvent` | `win32/Desktop.cpp` |
| `WSACreateEvent` | `win32/Desktop.cpp` |
| `WSAGetLastError` | `win32/Desktop.cpp` |
| `WSAResetEvent` | `win32/Desktop.cpp` |
| `WSASetEvent` | `win32/Desktop.cpp` |
| `WSASetLastError` | `win32/Desktop.cpp` |
| `WSAStartup` | `win32/Desktop.cpp` |
| `WSAWaitForMultipleEvents` | `win32/Desktop.cpp` |

### xaudio2_9.dll (1)

| Função | Implementação |
|---|---|
| `XAudio2Create` | `gpu/D3d12Check.cpp` |

### xinput1_4.dll (5)

| Função | Implementação |
|---|---|
| `XInputEnable` | `win32/Xinput.cpp` |
| `XInputGetBatteryInformation` | `win32/Xinput.cpp` |
| `XInputGetCapabilities` | `win32/Xinput.cpp` |
| `XInputGetState` | `win32/Xinput.cpp` |
| `XInputSetState` | `win32/Xinput.cpp` |

## Interfaces COM implementadas (vtables na convenção do Windows)

Os métodos estão na ordem dos slots do cabeçalho do Windows. Um slot sem implementação devolve `E_NOTIMPL` e registra o próprio nome.

### `gpu/dxgi_abi.c`

- **IUnknown and IDXGIObject**: `QueryInterface`, `AddRef`, `Release`, `SetPrivateData`, `SetPrivateDataInterface`, `GetPrivateData`, `GetParent`
- **IDXGIFactory**: `EnumAdapters`, `MakeWindowAssociation`, `GetWindowAssociation`, `CreateSwapChain`, `EnumAdapters1`, `IsCurrent`, `IsWindowedStereoEnabled`, `CreateSwapChainForHwnd`, `GetCreationFlags`, `EnumAdapterByLuid`, `CheckFeatureSupport`, `EnumAdapterByGpuPreference`
- **IDXGIAdapter**: `EnumOutputs`, `GetDesc`, `CheckInterfaceSupport`, `GetDesc1`, `GetDesc2`, `QueryVideoMemoryInfo`, `SetVideoMemoryReservation`, `GetDesc3`
- **IDXGIVkSurfaceFactory**: `SurfaceQueryInterface`, `SurfaceAddRef`, `SurfaceRelease`, `CreateSurface`
- **IDXGISwapChain**: `ChainQueryInterface`, `ChainAddRef`, `ChainRelease`, `ChainSetPrivateData`, `ChainSetPrivateDataInterface`, `ChainGetPrivateData`, `ChainGetParent`, `ChainGetDevice`, `Present`, `GetBuffer`, `SetFullscreenState`, `GetFullscreenState`, `ChainGetDesc`, `ResizeBuffers`, `ResizeTarget`, `GetContainingOutput`, `GetFrameStatistics`, `GetLastPresentCount`, `ChainGetDesc1`, `GetFullscreenDesc`, `GetHwnd`, `Present1`, `IsTemporaryMonoSupported`, `GetRestrictToOutput`, `SetBackgroundColor`, `GetBackgroundColor`, `SetRotation`, `GetRotation`, `SetSourceSize`, `GetSourceSize`, `SetMaximumFrameLatency`, `GetMaximumFrameLatency`, `GetFrameLatencyWaitableObject`, `GetCurrentBackBufferIndex`, `CheckColorSpaceSupport`, `SetColorSpace1`, `ResizeBuffers1`, `SetHDRMetaData`

### `win32/audio_abi.c`

- **IUnknown**: `QueryInterface`, `AddRef`, `Release`
- **IMMDeviceEnumerator**: `EnumAudioEndpoints`, `GetDefaultAudioEndpoint`, `GetDevice`, `RegisterEndpointNotificationCallback`, `UnregisterEndpointNotificationCallback`
- **IMMDeviceCollection**: `CollectionGetCount`, `CollectionItem`
- **IMMDevice**: `Activate`, `OpenPropertyStore`, `GetId`, `GetState`
- **IPropertyStore**: `StoreGetCount`, `StoreGetAt`, `StoreGetValue`
- **IAudioClient**: `Initialize`, `GetBufferSize`, `GetStreamLatency`, `GetCurrentPadding`, `IsFormatSupported`, `GetMixFormat`, `GetDevicePeriod`, `Start`, `Stop`, `Reset`, `SetEventHandle`, `GetService`, `IsOffloadCapable`, `SetClientProperties`, `GetBufferSizeLimits`
- **IAudioRenderClient**: `GetBuffer`, `ReleaseBuffer`
- **IAudioClock**: `GetFrequency`, `GetPosition`, `GetCharacteristics`
- **ISimpleAudioVolume**: `SetMasterVolume`, `GetMasterVolume`, `SetMute`, `GetMute`
- **IAudioStreamVolume**: `GetChannelCount`, `SetChannelVolume`, `GetChannelVolume`, `SetAllVolumes`, `GetAllVolumes`

### `win32/wic_abi.c`

- **IUnknown**: `QueryInterface`, `AddRef`, `Release`
- **IWICImagingFactory**: `CreateDecoderFromStream`, `CreateFormatConverter`, `CreateStream`
- **IWICStream**: `StreamRead`, `StreamSeek`, `StreamStat`, `InitializeFromMemory`
- **IWICBitmapDecoder**: `QueryCapability`, `DecoderInitialize`, `GetContainerFormat`, `DecoderGetColorContexts`, `GetFrameCount`, `GetFrame`
- **IWICBitmapSource**: `GetSize`, `GetPixelFormat`, `GetResolution`, `CopyPixels`
- **IWICBitmapFrameDecode**: `FrameGetColorContexts`
- **IWICFormatConverter**: `ConverterInitialize`, `CanConvert`

### `win32/xaudio2_abi.c`

- **IXAudio2**: `QueryInterface`, `AddRef`, `Release`, `RegisterForCallbacks`, `UnregisterForCallbacks`, `CreateSourceVoice`, `CreateSubmixVoice`, `CreateMasteringVoice`, `StartEngine`, `StopEngine`, `CommitChanges`, `GetPerformanceData`, `SetDebugConfiguration`
- **IXAudio2Voice**: `GetVoiceDetails`, `SetOutputVoices`, `SetEffectChain`, `EnableEffect`, `DisableEffect`, `GetEffectState`, `SetEffectParameters`, `GetEffectParameters`, `SetFilterParameters`, `GetFilterParameters`, `SetOutputFilterParameters`, `GetOutputFilterParameters`, `SetVolume`, `GetVolume`, `SetChannelVolumes`, `GetChannelVolumes`, `SetOutputMatrix`, `GetOutputMatrix`, `DestroyVoice`
- **IXAudio2SourceVoice**: `Start`, `Stop`, `SubmitSourceBuffer`, `FlushSourceBuffers`, `Discontinuity`, `ExitLoop`, `GetState`, `SetFrequencyRatio`, `GetFrequencyRatio`, `SetSourceSampleRate`
- **IXAudio2MasteringVoice**: `GetChannelMask`

