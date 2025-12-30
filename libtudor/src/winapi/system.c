#include <asm/prctl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdarg.h>
#include <unistd.h>
#include "internal.h"

typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        };
    };
    DWORD dwPageSize;
    void *lpMinimumApplicationAddress;
    void *lpMaximumApplicationAddress;
    ULONG_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO;

typedef struct {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} OSVERSIONINFOW;

typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
    WORD wServicePackMajor;
    WORD wServicePackMinor;
    WORD wSuiteMask;
    BYTE wProductType;
    BYTE wReserved;
} OSVERSIONINFOEXA;

__winfnc NTSTATUS RtlGetVersion(OSVERSIONINFOW *ver) {
    ver->dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
    ver->dwMajorVersion = 10;
    ver->dwMinorVersion = 0;
    ver->dwBuildNumber = 17134;
    ver->dwPlatformId = 2; //VER_PLATFORM_WIN32_NT
    return 0;
}
WINAPI(RtlGetVersion)

__winfnc ULONGLONG VerSetConditionMask(ULONGLONG cond_mask, DWORD type_mask, BYTE condition) {
    return cond_mask;
}
WINAPI(VerSetConditionMask)

__winfnc void GetSystemInfo(SYSTEM_INFO *info) {
    info->wProcessorArchitecture = 9; //PROCESSOR_ARCHITECTURE_AMD64
    info->dwPageSize = 4096;
    info->lpMinimumApplicationAddress = (void*) 0;
    info->lpMaximumApplicationAddress = (void*) UINTPTR_MAX;
    info->dwActiveProcessorMask = 0b1;
    info->dwNumberOfProcessors = 1;
    info->dwProcessorType = 8664; //PROCESSOR_AMD_X8664
    info->dwAllocationGranularity = 8;
    info->wProcessorLevel = 0;
    info->wProcessorRevision = 0;
}
WINAPI(GetSystemInfo)

__winfnc BOOL VerifyVersionInfoW(ULONGLONG condition_mask, DWORD type_mask, ULONGLONG cond_mask) {
    //TODO
    return TRUE;
}
WINAPI(VerifyVersionInfoW)

__winfnc BOOL ConvertStringSecurityDescriptorToSecurityDescriptorW(const char16_t *str, DWORD rev, void *descrpt, ULONG *descrpt_size) {
    //TODO
    if(descrpt_size) *descrpt_size = 0;
    return TRUE;
}
WINAPI(ConvertStringSecurityDescriptorToSecurityDescriptorW)

/* --- ADD THIS TO THE END OF system.c --- */

// --- Registry Stubs ---
__winfnc LSTATUS RegOpenKeyA(HANDLE hKey, const char *lpSubKey, HANDLE *phkResult) {
    return 2; // ERROR_FILE_NOT_FOUND
}
WINAPI(RegOpenKeyA)

__winfnc LSTATUS RegQueryInfoKeyA(HANDLE hKey, char *lpClass, DWORD *lpcchClass, DWORD *lpReserved, DWORD *lpcSubKeys, DWORD *lpcbMaxSubKeyLen, DWORD *lpcbMaxClassLen, DWORD *lpcValues, DWORD *lpcbMaxValueNameLen, DWORD *lpcbMaxValueLen, DWORD *lpcbSecurityDescriptor, void *lpftLastWriteTime) {
    return 2; // ERROR_FILE_NOT_FOUND
}
WINAPI(RegQueryInfoKeyA)


// --- SetupAPI Stubs (Device Enumeration) ---
__winfnc HANDLE SetupDiGetClassDevsA(const void *ClassGuid, const char *Enumerator, void *hwndParent, DWORD Flags) {
    return INVALID_HANDLE_VALUE;
}
WINAPI(SetupDiGetClassDevsA)

__winfnc BOOL SetupDiEnumDeviceInterfaces(HANDLE DeviceInfoSet, void *DeviceInfoData, const void *InterfaceClassGuid, DWORD MemberIndex, void *DeviceInterfaceData) {
    winerr_set_code(259); // ERROR_NO_MORE_ITEMS
    return FALSE;
}
WINAPI(SetupDiEnumDeviceInterfaces)

__winfnc BOOL SetupDiGetDeviceInterfaceDetailA(HANDLE DeviceInfoSet, void *DeviceInterfaceData, void *DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize, DWORD *RequiredSize, void *DeviceInfoData) {
    winerr_set_code(1); // ERROR_INVALID_FUNCTION
    return FALSE;
}
WINAPI(SetupDiGetDeviceInterfaceDetailA)

__winfnc BOOL SetupDiDestroyDeviceInfoList(HANDLE DeviceInfoSet) {
    return TRUE;
}
WINAPI(SetupDiDestroyDeviceInfoList)

// --- Other Misc Stubs ---
__winfnc void ExitProcess(UINT uExitCode) {
    log_info("Driver requested ExitProcess(%d)", uExitCode);
    exit(uExitCode);
}
WINAPI(ExitProcess)

__winfnc BOOL GetConsoleMode(HANDLE hConsoleHandle, DWORD *lpMode) {
    return FALSE;
}
WINAPI(GetConsoleMode)

/* --- ADD TO END OF system.c --- */

__winfnc UINT GetSystemDirectoryA(char *lpBuffer, UINT uSize) {
    // Pretend we are in System32
    const char *path = "C:\\Windows\\System32";
    size_t len = strlen(path);
    if (uSize > len) {
        strcpy(lpBuffer, path);
        return len;
    }
    return len + 1;
}
WINAPI(GetSystemDirectoryA)

__winfnc UINT GetSystemDirectoryW(char16_t *lpBuffer, UINT uSize) {
    // Unicode version stub - return 0 (fail) to force fallback or skip
    return 0; 
}
WINAPI(GetSystemDirectoryW)

__winfnc UINT GetSystemFirmwareTable(DWORD FirmwareTableProviderSignature, DWORD FirmwareTableID, void *pFirmwareTableBuffer, DWORD BufferSize) {
    // Return 0 (failure). This usually tells the driver "I can't check the BIOS",
    // so it might default to "Allow" or skip the check.
    return 0;
}
WINAPI(GetSystemFirmwareTable)

__winfnc void RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments) {
    log_error("Driver raised exception: 0x%x", dwExceptionCode);
    // We cannot recover from this easily without full SEH support.
    // Abort so we get a clean exit instead of undefined behavior.
    abort();
}
WINAPI(RaiseException)

__winfnc BOOL ProcessIdToSessionId(DWORD dwProcessId, DWORD *pSessionId) {
    *pSessionId = 1; // Pretend we are in user session 1
    return TRUE;
}
WINAPI(ProcessIdToSessionId)