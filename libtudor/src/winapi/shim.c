#include "internal.h"
#include <tudor/log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define SHIM_LOG(fmt, ...) fprintf(stderr, "[SHIM] " fmt "\n", ##__VA_ARGS__)

// --- External Functions from sync.c ---
extern HANDLE CreateEventW(void *attrs, BOOL manual_reset, BOOL initial_state, const char16_t *name);
extern BOOL SetEvent(HANDLE handle);

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

//These used to return 0 without writing anything, so every string the driver
//formatted came back empty. wsprintf's documented cap is 1024 chars including
//the terminator.
#define WSPRINTF_MAX 1024

__winfnc int wsprintfA(char *dest, const char *fmt, ...) {
    if(!dest || !fmt) return 0;

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(dest, WSPRINTF_MAX, fmt, args);
    va_end(args);

    if(len < 0) { dest[0] = '\0'; return 0; }
    if(len >= WSPRINTF_MAX) len = WSPRINTF_MAX - 1;
    return len;
}
WINAPI(wsprintfA)

__winfnc int wsprintfW(char16_t *dest, const char16_t *fmt, ...) {
    if(!dest || !fmt) return 0;

    //Narrow the format, run it, then widen the result. The driver only uses
    //this for log and identifier strings, so the round trip is acceptable.
    char *cfmt = winstr_to_str(fmt);
    if(!cfmt) { dest[0] = 0; return 0; }

    char buf[WSPRINTF_MAX];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), cfmt, args);
    va_end(args);
    free(cfmt);

    if(len < 0) { dest[0] = 0; return 0; }
    if(len >= WSPRINTF_MAX) len = WSPRINTF_MAX - 1;

    for(int i = 0; i < len; i++) dest[i] = (char16_t) (unsigned char) buf[i];
    dest[len] = 0;
    return len;
}
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

//A real counting semaphore. The previous version mapped this onto an
//auto-reset Event, which silently collapses the count: N releases before a
//wait only ever satisfy one waiter, and the rest are lost. Anything using a
//semaphore to track queued work then stalls once the queue depth exceeds one.
struct win_semaphore {
    struct win_sync_object sync_obj;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    LONG count, max_count;
};

static void sem_destr(struct win_semaphore *sem) {
    cant_fail_ret(pthread_cond_destroy(&sem->cond));
    cant_fail_ret(pthread_mutex_destroy(&sem->lock));
    free(sem);
}

static DWORD sem_wait(struct win_semaphore *sem, DWORD timeout) {
    DWORD res = 0;
    cant_fail_ret(pthread_mutex_lock(&sem->lock));

    while(sem->count <= 0) {
        if(timeout != INFINITE) {
            struct timespec time;
            clock_gettime(CLOCK_REALTIME, &time);
            time.tv_sec += timeout / 1000;
            time.tv_nsec += (timeout % 1000) * 1000000L;
            if(time.tv_nsec >= 1000000000L) { time.tv_sec++; time.tv_nsec -= 1000000000L; }

            int err = pthread_cond_timedwait(&sem->cond, &sem->lock, &time);
            if(err == ETIMEDOUT) { res = WAIT_TIMEOUT; break; }
            cant_fail(err);
        } else cant_fail_ret(pthread_cond_wait(&sem->cond, &sem->lock));
    }

    if(res == 0) sem->count--;

    cant_fail_ret(pthread_mutex_unlock(&sem->lock));
    return res;
}

__winfnc HANDLE CreateSemaphoreExW(void *lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, const char16_t *lpName, DWORD dwFlags, DWORD dwDesiredAccess) {
    struct win_semaphore *sem = (struct win_semaphore*) malloc(sizeof(struct win_semaphore));
    if(!sem) { winerr_set_errno(); return NULL; }

    sem->sync_obj.wait_fnc = (win_sync_obj_wait_fnc*) sem_wait;
    sem->count = lInitialCount;
    sem->max_count = lMaximumCount > 0 ? lMaximumCount : 0x7fffffff;
    cant_fail_ret(pthread_mutex_init(&sem->lock, NULL));
    cant_fail_ret(pthread_cond_init(&sem->cond, NULL));

    return winhandle_create(sem, (winhandle_destr_fnc*) sem_destr);
}
WINAPI(CreateSemaphoreExW)

__winfnc HANDLE CreateSemaphoreW(void *lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, const char16_t *lpName) {
    return CreateSemaphoreExW(lpSemaphoreAttributes, lInitialCount, lMaximumCount, lpName, 0, 0);
}
WINAPI(CreateSemaphoreW)

__winfnc BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LONG *lpPreviousCount) {
    if(!hSemaphore || hSemaphore == INVALID_HANDLE_VALUE) { winerr_set(); return FALSE; }
    struct win_semaphore *sem = (struct win_semaphore*) hSemaphore->data;

    cant_fail_ret(pthread_mutex_lock(&sem->lock));
    LONG prev = sem->count;
    if(prev + lReleaseCount > sem->max_count) {
        cant_fail_ret(pthread_mutex_unlock(&sem->lock));
        winerr_set();
        return FALSE;
    }
    sem->count = prev + lReleaseCount;
    cant_fail_ret(pthread_cond_broadcast(&sem->cond));
    cant_fail_ret(pthread_mutex_unlock(&sem->lock));

    if(lpPreviousCount) *lpPreviousCount = prev;
    return TRUE;
}
WINAPI(ReleaseSemaphore)

// --- Threadpool ---
//These used to return NULL and do nothing, which means any completion the
//caller registers through them never fires. A driver that arms a threadpool
//wait on an overlapped I/O and then blocks would simply hang forever. Each
//object here gets a real worker thread instead.

__winfnc BOOL SetThreadStackGuarantee(ULONG *StackSizeInBytes) { return TRUE; }
WINAPI(SetThreadStackGuarantee)

typedef void __winfnc (*tp_timer_cb)(void *instance, void *context, void *timer);
typedef void __winfnc (*tp_wait_cb)(void *instance, void *context, void *wait, DWORD result);

struct tp_timer {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t thread;
    bool armed, running, shutdown, in_callback;
    DWORD due_ms, period_ms;
    tp_timer_cb cb;
    void *ctx;
};

static void tp_sleep_until(struct tp_timer *t, DWORD ms) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    time.tv_sec += ms / 1000;
    time.tv_nsec += (ms % 1000) * 1000000L;
    if(time.tv_nsec >= 1000000000L) { time.tv_sec++; time.tv_nsec -= 1000000000L; }
    pthread_cond_timedwait(&t->cond, &t->lock, &time);
}

static void *tp_timer_thread(void *arg) {
    struct tp_timer *t = (struct tp_timer*) arg;

    cant_fail_ret(pthread_mutex_lock(&t->lock));
    for(;;) {
        while(!t->armed && !t->shutdown) cant_fail_ret(pthread_cond_wait(&t->cond, &t->lock));
        if(t->shutdown) break;

        DWORD wait_ms = t->due_ms;
        tp_sleep_until(t, wait_ms);
        if(t->shutdown) break;
        if(!t->armed) continue;

        if(!t->period_ms) t->armed = false;
        else t->due_ms = t->period_ms;

        tp_timer_cb cb = t->cb;
        void *ctx = t->ctx;
        t->in_callback = true;
        cant_fail_ret(pthread_mutex_unlock(&t->lock));
        if(cb) cb(NULL, ctx, t);
        cant_fail_ret(pthread_mutex_lock(&t->lock));
        t->in_callback = false;
        cant_fail_ret(pthread_cond_broadcast(&t->cond));
    }
    t->running = false;
    cant_fail_ret(pthread_cond_broadcast(&t->cond));
    cant_fail_ret(pthread_mutex_unlock(&t->lock));
    return NULL;
}

__winfnc void* CreateThreadpoolTimer(void *pfpti, void *pv, void *pcbe) {
    struct tp_timer *t = (struct tp_timer*) malloc(sizeof(struct tp_timer));
    if(!t) { winerr_set_errno(); return NULL; }
    *t = (struct tp_timer) { .cb = (tp_timer_cb) pfpti, .ctx = pv, .running = true };
    cant_fail_ret(pthread_mutex_init(&t->lock, NULL));
    cant_fail_ret(pthread_cond_init(&t->cond, NULL));

    if(pthread_create(&t->thread, NULL, tp_timer_thread, t) != 0) {
        cant_fail_ret(pthread_cond_destroy(&t->cond));
        cant_fail_ret(pthread_mutex_destroy(&t->lock));
        free(t);
        winerr_set_errno();
        return NULL;
    }
    return t;
}
WINAPI(CreateThreadpoolTimer)

__winfnc void SetThreadpoolTimer(void *pti, void *pftDueTime, DWORD msPeriod, DWORD msWindowLength) {
    struct tp_timer *t = (struct tp_timer*) pti;
    if(!t) return;

    cant_fail_ret(pthread_mutex_lock(&t->lock));
    if(!pftDueTime) {
        //A NULL due time cancels the timer.
        t->armed = false;
    } else {
        //FILETIME due times are negative for a relative delay, in 100ns ticks.
        int64_t due = *(const int64_t*) pftDueTime;
        t->due_ms = due < 0 ? (DWORD) ((-due) / 10000) : 0;
        t->period_ms = msPeriod;
        t->armed = true;
    }
    cant_fail_ret(pthread_cond_broadcast(&t->cond));
    cant_fail_ret(pthread_mutex_unlock(&t->lock));
}
WINAPI(SetThreadpoolTimer)

__winfnc void WaitForThreadpoolTimerCallbacks(void *pti, BOOL fCancelPendingCallbacks) {
    struct tp_timer *t = (struct tp_timer*) pti;
    if(!t) return;

    cant_fail_ret(pthread_mutex_lock(&t->lock));
    if(fCancelPendingCallbacks) t->armed = false;
    while(t->in_callback) cant_fail_ret(pthread_cond_wait(&t->cond, &t->lock));
    cant_fail_ret(pthread_mutex_unlock(&t->lock));
}
WINAPI(WaitForThreadpoolTimerCallbacks)

__winfnc void CloseThreadpoolTimer(void *pti) {
    struct tp_timer *t = (struct tp_timer*) pti;
    if(!t) return;

    cant_fail_ret(pthread_mutex_lock(&t->lock));
    t->shutdown = true;
    t->armed = false;
    cant_fail_ret(pthread_cond_broadcast(&t->cond));
    cant_fail_ret(pthread_mutex_unlock(&t->lock));

    pthread_join(t->thread, NULL);
    cant_fail_ret(pthread_cond_destroy(&t->cond));
    cant_fail_ret(pthread_mutex_destroy(&t->lock));
    free(t);
}
WINAPI(CloseThreadpoolTimer)

struct tp_wait {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t thread;
    bool armed, shutdown, in_callback;
    HANDLE obj;
    DWORD timeout_ms;
    tp_wait_cb cb;
    void *ctx;
};

static void *tp_wait_thread(void *arg) {
    struct tp_wait *w = (struct tp_wait*) arg;

    cant_fail_ret(pthread_mutex_lock(&w->lock));
    for(;;) {
        while(!w->armed && !w->shutdown) cant_fail_ret(pthread_cond_wait(&w->cond, &w->lock));
        if(w->shutdown) break;

        HANDLE obj = w->obj;
        DWORD timeout = w->timeout_ms;
        w->armed = false;
        cant_fail_ret(pthread_mutex_unlock(&w->lock));

        DWORD res = win_wait_sync_obj(obj, timeout);

        cant_fail_ret(pthread_mutex_lock(&w->lock));
        if(w->shutdown) break;

        tp_wait_cb cb = w->cb;
        void *ctx = w->ctx;
        w->in_callback = true;
        cant_fail_ret(pthread_mutex_unlock(&w->lock));
        if(cb) cb(NULL, ctx, w, res);
        cant_fail_ret(pthread_mutex_lock(&w->lock));
        w->in_callback = false;
        cant_fail_ret(pthread_cond_broadcast(&w->cond));
    }
    cant_fail_ret(pthread_cond_broadcast(&w->cond));
    cant_fail_ret(pthread_mutex_unlock(&w->lock));
    return NULL;
}

__winfnc void* CreateThreadpoolWait(void *pfnwa, void *pv, void *pcbe) {
    struct tp_wait *w = (struct tp_wait*) malloc(sizeof(struct tp_wait));
    if(!w) { winerr_set_errno(); return NULL; }
    *w = (struct tp_wait) { .cb = (tp_wait_cb) pfnwa, .ctx = pv };
    cant_fail_ret(pthread_mutex_init(&w->lock, NULL));
    cant_fail_ret(pthread_cond_init(&w->cond, NULL));

    if(pthread_create(&w->thread, NULL, tp_wait_thread, w) != 0) {
        cant_fail_ret(pthread_cond_destroy(&w->cond));
        cant_fail_ret(pthread_mutex_destroy(&w->lock));
        free(w);
        winerr_set_errno();
        return NULL;
    }
    return w;
}
WINAPI(CreateThreadpoolWait)

__winfnc void SetThreadpoolWait(void *pwa, HANDLE h, void *pftTimeout) {
    struct tp_wait *w = (struct tp_wait*) pwa;
    if(!w) return;

    cant_fail_ret(pthread_mutex_lock(&w->lock));
    if(!h) {
        w->armed = false;
    } else {
        w->obj = h;
        if(pftTimeout) {
            int64_t t = *(const int64_t*) pftTimeout;
            w->timeout_ms = t < 0 ? (DWORD) ((-t) / 10000) : 0;
        } else w->timeout_ms = INFINITE;
        w->armed = true;
    }
    cant_fail_ret(pthread_cond_broadcast(&w->cond));
    cant_fail_ret(pthread_mutex_unlock(&w->lock));
}
WINAPI(SetThreadpoolWait)

__winfnc void WaitForThreadpoolWaitCallbacks(void *pwa, BOOL fCancelPendingCallbacks) {
    struct tp_wait *w = (struct tp_wait*) pwa;
    if(!w) return;

    cant_fail_ret(pthread_mutex_lock(&w->lock));
    if(fCancelPendingCallbacks) w->armed = false;
    while(w->in_callback) cant_fail_ret(pthread_cond_wait(&w->cond, &w->lock));
    cant_fail_ret(pthread_mutex_unlock(&w->lock));
}
WINAPI(WaitForThreadpoolWaitCallbacks)

__winfnc void CloseThreadpoolWait(void *pwa) {
    struct tp_wait *w = (struct tp_wait*) pwa;
    if(!w) return;

    cant_fail_ret(pthread_mutex_lock(&w->lock));
    w->shutdown = true;
    w->armed = false;
    cant_fail_ret(pthread_cond_broadcast(&w->cond));
    cant_fail_ret(pthread_mutex_unlock(&w->lock));

    //The worker may be parked inside win_wait_sync_obj; it exits once that
    //returns and it sees the shutdown flag.
    pthread_join(w->thread, NULL);
    cant_fail_ret(pthread_cond_destroy(&w->cond));
    cant_fail_ret(pthread_mutex_destroy(&w->lock));
    free(w);
}
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