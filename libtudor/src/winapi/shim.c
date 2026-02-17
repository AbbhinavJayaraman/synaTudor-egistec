#include "internal.h"
#include <tudor/log.h>
#include <string.h>
#include <stdio.h> // For fprintf

// Helper to force logs to appear
#define SHIM_LOG(fmt, ...) fprintf(stderr, "[SHIM] " fmt "\n", ##__VA_ARGS__)

// --- Exception Handling ---
__winfnc void RtlUnwindEx(void *TargetFrame, void *TargetIp, void *ExceptionRecord, void *ReturnValue, void *ContextRecord, void *HistoryTable) {
    SHIM_LOG("RtlUnwindEx called");
}
WINAPI(RtlUnwindEx)

__winfnc void* RtlVirtualUnwind(DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc, void *FunctionEntry, void *ContextRecord, void *HandlerData, void *EstablisherFrame, void *ContextPointers) {
    SHIM_LOG("RtlVirtualUnwind called (Returning NULL)");
    return NULL;
}
WINAPI(RtlVirtualUnwind)

__winfnc void* RtlPcToFileHeader(void *PcValue, void *BaseOfImage) {
    // SHIM_LOG("RtlPcToFileHeader"); // Too spammy usually
    return NULL;
}
WINAPI(RtlPcToFileHeader)

// --- Console / IO ---
__winfnc UINT GetConsoleCP() { 
    SHIM_LOG("GetConsoleCP");
    return 65001; 
}
WINAPI(GetConsoleCP)

__winfnc UINT GetOEMCP() { return 65001; }
WINAPI(GetOEMCP)

__winfnc BOOL SetStdHandle(DWORD nStdHandle, HANDLE hHandle) { 
    SHIM_LOG("SetStdHandle(%d)", nStdHandle);
    return TRUE; 
}
WINAPI(SetStdHandle)

__winfnc BOOL WriteConsoleW(HANDLE hConsoleOutput, const void *lpBuffer, DWORD nNumberOfCharsToWrite, DWORD *lpNumberOfCharsWritten, void *lpReserved) {
    if(lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    return TRUE;
}
WINAPI(WriteConsoleW)

// --- Resources ---
__winfnc void* FindResourceW(HANDLE hModule, const char16_t *lpName, const char16_t *lpType) { 
    SHIM_LOG("FindResourceW");
    return NULL; 
}
WINAPI(FindResourceW)

__winfnc void* LoadResource(HANDLE hModule, void *hResInfo) { return NULL; }
WINAPI(LoadResource)

__winfnc void* LockResource(void *hResData) { return NULL; }
WINAPI(LockResource)

__winfnc DWORD SizeofResource(HANDLE hModule, void *hResInfo) { return 0; }
WINAPI(SizeofResource)

// --- Strings / Path ---
__winfnc char16_t* StrStrIW(const char16_t *pszFirst, const char16_t *pszSrch) { return NULL; }
WINAPI(StrStrIW)

__winfnc BOOL PathFileExistsW(const char16_t *pszPath) { 
    SHIM_LOG("PathFileExistsW");
    return FALSE; 
}
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

// --- CRITICAL FIX: Actually copy the string ---
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

// --- Registry / Security ---
__winfnc LSTATUS RegDeleteValueW(HANDLE hKey, const char16_t *lpValueName) { return ERROR_SUCCESS; }
WINAPI(RegDeleteValueW)

__winfnc LSTATUS RegDeleteValueA(HANDLE hKey, const char *lpValueName) { return ERROR_SUCCESS; }
WINAPI(RegDeleteValueA)

__winfnc LSTATUS RegDeleteKeyValueW(HANDLE hKey, const char16_t *lpSubKey, const char16_t *lpValueName) { return ERROR_SUCCESS; }
WINAPI(RegDeleteKeyValueW)

__winfnc LSTATUS RegSetKeyValueW(HANDLE hKey, const char16_t *lpSubKey, const char16_t *lpValueName, DWORD dwType, const void *lpData, DWORD cbData) { return ERROR_SUCCESS; }
WINAPI(RegSetKeyValueW)

__winfnc LSTATUS RegEnumKeyW(HANDLE hKey, DWORD dwIndex, char16_t *lpName, DWORD *lpcchName, void *lpReserved, char16_t *lpClass, DWORD *lpcchClass, void *lpftLastWriteTime) { return 259; } // ERROR_NO_MORE_ITEMS
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
    SHIM_LOG("GetSystemTime");
    if(lpSystemTime) memset(lpSystemTime, 0, 16); 
}
WINAPI(GetSystemTime)

__winfnc void DebugBreak() { SHIM_LOG("DebugBreak hit!"); }
WINAPI(DebugBreak)

__winfnc LONG RtlCompareUnicodeString(const UNICODE_STRING *String1, const UNICODE_STRING *String2, BOOLEAN CaseInSensitive) { return 0; }
WINAPI(RtlCompareUnicodeString)