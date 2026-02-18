#include "internal.h"
#include <tudor/log.h>
#include <string.h>
#include <stdio.h>

#define SHIM_LOG(fmt, ...) fprintf(stderr, "[SHIM] " fmt "\n", ##__VA_ARGS__)

// --- External Functions from sync.c ---
extern HANDLE CreateEventW(void *attrs, BOOL manual_reset, BOOL initial_state, const char16_t *name);
extern BOOL SetEvent(HANDLE handle); // Used to fake ReleaseSemaphore

// --- Exception Handling ---
__winfnc void RtlUnwindEx(void *TargetFrame, void *TargetIp, void *ExceptionRecord, void *ReturnValue, void *ContextRecord, void *HistoryTable) {
    SHIM_LOG("RtlUnwindEx called");
}
WINAPI(RtlUnwindEx)

__winfnc void* RtlVirtualUnwind(DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc, void *FunctionEntry, void *ContextRecord, void *HandlerData, void *EstablisherFrame, void *ContextPointers) {
    return NULL;
}
WINAPI(RtlVirtualUnwind)

__winfnc void* RtlPcToFileHeader(void *PcValue, void *BaseOfImage) { return NULL; }
WINAPI(RtlPcToFileHeader)

// --- Console / IO ---
__winfnc UINT GetConsoleCP() { return 65001; }
WINAPI(GetConsoleCP)

__winfnc UINT GetOEMCP() { return 65001; }
WINAPI(GetOEMCP)

__winfnc BOOL SetStdHandle(DWORD nStdHandle, HANDLE hHandle) { return TRUE; }
WINAPI(SetStdHandle)

__winfnc BOOL WriteConsoleW(HANDLE hConsoleOutput, const void *lpBuffer, DWORD nNumberOfCharsToWrite, DWORD *lpNumberOfCharsWritten, void *lpReserved) {
    if(lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    return TRUE;
}
WINAPI(WriteConsoleW)

// --- Resources ---
__winfnc void* FindResourceW(HANDLE hModule, const char16_t *lpName, const char16_t *lpType) { 
    return NULL; 
}
WINAPI(FindResourceW)

__winfnc void* LoadResource(HANDLE hModule, void *hResInfo) { return NULL; }
WINAPI(LoadResource)

__winfnc void* LockResource(void *hResData) { return NULL; }
WINAPI(LockResource)

__winfnc DWORD SizeofResource(HANDLE hModule, void *hResInfo) { return 0; }
WINAPI(SizeofResource)

// --- Strings / Path / Locale ---
__winfnc char16_t* StrStrIW(const char16_t *pszFirst, const char16_t *pszSrch) { return NULL; }
WINAPI(StrStrIW)

__winfnc BOOL PathFileExistsW(const char16_t *pszPath) { return FALSE; }
WINAPI(PathFileExistsW)

__winfnc BOOL PathFileExistsA(const char *pszPath) { return FALSE; }
WINAPI(PathFileExistsA)

__winfnc BOOL SHGetSpecialFolderPathA(HANDLE hwnd, char *pszPath, int csidl, BOOL fCreate) { return FALSE; }
WINAPI(SHGetSpecialFolderPathA)

__winfnc int wsprintfA(char *dest, const char *fmt, ...) { return 0; }
WINAPI(wsprintfA)

__winfnc int wsprintfW(char16_t *dest, const char16_t *fmt, ...) { return 0; }
WINAPI(wsprintfW)

__winfnc char* lstrcpyA(char *dest, const char *src) { return strcpy(dest, src); }
WINAPI(lstrcpyA)

__winfnc char16_t* lstrcpyW(char16_t *dest, const char16_t *src) { 
    char16_t *d = dest;
    if (!dest || !src) return dest;
    while((*d++ = *src++));
    return dest;
}
WINAPI(lstrcpyW)

__winfnc int lstrlenW(const char16_t *lpString) {
    int i = 0;
    if(!lpString) return 0;
    while(lpString[i] != 0) i++;
    return i;
}
WINAPI(lstrlenW)

__winfnc int StrCmpNIW(const char16_t *s1, const char16_t *s2, int n) { return 0; }
WINAPI(StrCmpNIW)

__winfnc char16_t* StrStrW(const char16_t *pszFirst, const char16_t *pszSrch) { return NULL; }
WINAPI(StrStrW)

__winfnc int CompareStringEx(const char16_t *lpLocaleName, DWORD dwCmpFlags, const char16_t *lpString1, int cchCount1, const char16_t *lpString2, int cchCount2, void *lpVersionInformation, void *lpReserved, LPARAM lParam) { return 0; }
WINAPI(CompareStringEx)

__winfnc int LCMapStringEx(const char16_t *lpLocaleName, DWORD dwMapFlags, const char16_t *lpSrcStr, int cchSrc, char16_t *lpDestStr, int cchDest, void *lpVersionInformation, void *lpReserved, LPARAM lParam) { return 0; }
WINAPI(LCMapStringEx)

__winfnc BOOL EnumSystemLocalesEx(void *lpLocaleEnumProcEx, DWORD dwFlags, LPARAM lParam, void *lpReserved) { return FALSE; }
WINAPI(EnumSystemLocalesEx)

__winfnc int GetDateFormatEx(const char16_t *lpLocaleName, DWORD dwFlags, const void *lpDate, const char16_t *lpFormat, char16_t *lpDateStr, int cchDate, const char16_t *lpCalendar) { return 0; }
WINAPI(GetDateFormatEx)

__winfnc int GetTimeFormatEx(const char16_t *lpLocaleName, DWORD dwFlags, const void *lpTime, const char16_t *lpFormat, char16_t *lpTimeStr, int cchTime) { return 0; }
WINAPI(GetTimeFormatEx)

__winfnc int GetLocaleInfoEx(const char16_t *lpLocaleName, DWORD LCType, char16_t *lpLCData, int cchData) { return 0; }
WINAPI(GetLocaleInfoEx)

__winfnc int GetUserDefaultLocaleName(char16_t *lpLocaleName, int cchLocaleName) { return 0; }
WINAPI(GetUserDefaultLocaleName)

__winfnc BOOL IsValidLocaleName(const char16_t *lpLocaleName) { return FALSE; }
WINAPI(IsValidLocaleName)

// --- Registry / Security ---
__winfnc LSTATUS RegDeleteValueW(HANDLE hKey, const char16_t *lpValueName) { return ERROR_SUCCESS; }
WINAPI(RegDeleteValueW)

__winfnc LSTATUS RegDeleteValueA(HANDLE hKey, const char *lpValueName) { return ERROR_SUCCESS; }
WINAPI(RegDeleteValueA)

__winfnc LSTATUS RegDeleteKeyValueW(HANDLE hKey, const char16_t *lpSubKey, const char16_t *lpValueName) { return ERROR_SUCCESS; }
WINAPI(RegDeleteKeyValueW)

__winfnc LSTATUS RegSetKeyValueW(HANDLE hKey, const char16_t *lpSubKey, const char16_t *lpValueName, DWORD dwType, const void *lpData, DWORD cbData) { return ERROR_SUCCESS; }
WINAPI(RegSetKeyValueW)

__winfnc LSTATUS RegEnumKeyW(HANDLE hKey, DWORD dwIndex, char16_t *lpName, DWORD *lpcchName, void *lpReserved, char16_t *lpClass, DWORD *lpcchClass, void *lpftLastWriteTime) { return 259; } 
WINAPI(RegEnumKeyW)

__winfnc LSTATUS RegEnumValueA(HANDLE hKey, DWORD dwIndex, char *lpValueName, DWORD *lpcchValueName, void *lpReserved, DWORD *lpType, BYTE *lpData, DWORD *lpcbData) { return 259; }
WINAPI(RegEnumValueA)

__winfnc BOOL LookupAccountSidA(const char *lpSystemName, void *Sid, char *Name, DWORD *cchName, char *ReferencedDomainName, DWORD *cchReferencedDomainName, int *peUse) { return FALSE; }
WINAPI(LookupAccountSidA)

__winfnc BOOL IsValidSid(void *pSid) { return TRUE; }
WINAPI(IsValidSid)

// --- Tracing ---
__winfnc DWORD GetTraceEnableFlags(DWORD64 TraceHandle) { return 0; }
WINAPI(GetTraceEnableFlags)

__winfnc BYTE GetTraceEnableLevel(DWORD64 TraceHandle) { return 0; }
WINAPI(GetTraceEnableLevel)

__winfnc DWORD64 GetTraceLoggerHandle(void *Buffer) { return 0; }
WINAPI(GetTraceLoggerHandle)

// --- WTS / System ---
__winfnc DWORD WTSGetActiveConsoleSessionId() { return 1; }
WINAPI(WTSGetActiveConsoleSessionId)

__winfnc BOOL WTSQuerySessionInformationW(HANDLE hServer, DWORD SessionId, DWORD WTSInfoClass, void **ppBuffer, DWORD *pBytesReturned) { return FALSE; }
WINAPI(WTSQuerySessionInformationW)

__winfnc void WTSFreeMemory(void *pMemory) {}
WINAPI(WTSFreeMemory)

__winfnc void GetSystemTime(void *lpSystemTime) {
    if(lpSystemTime) memset(lpSystemTime, 0, 16); 
}
WINAPI(GetSystemTime)

__winfnc void DebugBreak() { SHIM_LOG("DebugBreak hit!"); }
WINAPI(DebugBreak)

__winfnc LONG RtlCompareUnicodeString(const UNICODE_STRING *String1, const UNICODE_STRING *String2, BOOLEAN CaseInSensitive) { return 0; }
WINAPI(RtlCompareUnicodeString)

// --- Synchronization (Critical Updates) ---

#define CREATE_EVENT_MANUAL_RESET 0x00000001
#define CREATE_EVENT_INITIAL_SET  0x00000002

__winfnc HANDLE CreateEventExW(void *lpEventAttributes, const char16_t *lpName, DWORD dwFlags, DWORD dwDesiredAccess) { 
    BOOL manual = (dwFlags & CREATE_EVENT_MANUAL_RESET) ? TRUE : FALSE;
    BOOL initial = (dwFlags & CREATE_EVENT_INITIAL_SET) ? TRUE : FALSE;
    return CreateEventW(lpEventAttributes, manual, initial, lpName);
}
WINAPI(CreateEventExW)

__winfnc HANDLE CreateSemaphoreExW(void *lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, const char16_t *lpName, DWORD dwFlags, DWORD dwDesiredAccess) {
    // HACK: Map Semaphore to Auto-Reset Event
    BOOL initial = (lInitialCount > 0) ? TRUE : FALSE;
    SHIM_LOG("CreateSemaphoreExW calling CreateEventW (FAKE SEMAPHORE)");
    return CreateEventW(lpSemaphoreAttributes, FALSE, initial, lpName);
}
WINAPI(CreateSemaphoreExW)

// CRITICAL: Driver likely calls this to signal the semaphore
__winfnc BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LONG *lpPreviousCount) {
    // Since we faked the semaphore as an Event, we "release" it by setting the event.
    // This allows WaitForSingleObject to unblock.
    if(lpPreviousCount) *lpPreviousCount = 0; 
    return SetEvent(hSemaphore); 
}
WINAPI(ReleaseSemaphore)

// --- Threadpool Stubs ---
__winfnc BOOL SetThreadStackGuarantee(ULONG *StackSizeInBytes) { return TRUE; }
WINAPI(SetThreadStackGuarantee)

__winfnc void* CreateThreadpoolTimer(void *pfpti, void *pv, void *pcbe) { return NULL; }
WINAPI(CreateThreadpoolTimer)

__winfnc void SetThreadpoolTimer(void *pti, void *pftDueTime, DWORD msPeriod, DWORD msWindowLength) {}
WINAPI(SetThreadpoolTimer)

__winfnc void WaitForThreadpoolTimerCallbacks(void *pti, BOOL fCancelPendingCallbacks) {}
WINAPI(WaitForThreadpoolTimerCallbacks)

__winfnc void CloseThreadpoolTimer(void *pti) {}
WINAPI(CloseThreadpoolTimer)

__winfnc void* CreateThreadpoolWait(void *pfnwa, void *pv, void *pcbe) { return NULL; }
WINAPI(CreateThreadpoolWait)

__winfnc void SetThreadpoolWait(void *pwa, HANDLE h, void *pftTimeout) {}
WINAPI(SetThreadpoolWait)

__winfnc void CloseThreadpoolWait(void *pwa) {}
WINAPI(CloseThreadpoolWait)

__winfnc void FlushProcessWriteBuffers() {}
WINAPI(FlushProcessWriteBuffers)

__winfnc void FreeLibraryWhenCallbackReturns(void *ptp, HANDLE hLibModule) {}
WINAPI(FreeLibraryWhenCallbackReturns)

__winfnc DWORD GetCurrentProcessorNumber() { return 0; }
WINAPI(GetCurrentProcessorNumber)

__winfnc BOOL GetLogicalProcessorInformation(void *Buffer, DWORD *ReturnedLength) { return FALSE; }
WINAPI(GetLogicalProcessorInformation)

// --- File / DLL Loading Stubs ---
__winfnc BOOL SetDefaultDllDirectories(DWORD DirectoryFlags) { return TRUE; }
WINAPI(SetDefaultDllDirectories)

__winfnc BOOL CreateSymbolicLinkW(const char16_t *lpSymlinkFileName, const char16_t *lpTargetFileName, DWORD dwFlags) { return FALSE; }
WINAPI(CreateSymbolicLinkW)

__winfnc LONG GetCurrentPackageId(UINT32 *bufferLength, BYTE *buffer) { return 15700; } // APPMODEL_ERROR_NO_PACKAGE
WINAPI(GetCurrentPackageId)

__winfnc BOOL GetFileInformationByHandleExW(HANDLE hFile, int FileInformationClass, void *lpFileInformation, DWORD dwBufferSize) {
    SHIM_LOG("GetFileInformationByHandleExW called (Returning FALSE)");
    return FALSE; 
}
WINAPI(GetFileInformationByHandleExW)

__winfnc BOOL SetFileInformationByHandleW(HANDLE hFile, int FileInformationClass, void *lpFileInformation, DWORD dwBufferSize) {
    SHIM_LOG("SetFileInformationByHandleW called (Returning TRUE)");
    return TRUE; 
}
WINAPI(SetFileInformationByHandleW)