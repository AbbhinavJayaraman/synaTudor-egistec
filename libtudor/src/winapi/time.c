#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "internal.h"

typedef struct {
    DWORD dwLowDateTime, dwHighDateTime;
} FILETIME;

__winfnc BOOL QueryPerformanceCounter(uint64_t *counter) {
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        winerr_set_errno();
        return FALSE;
    }

    *counter = time.tv_sec * 1000000lu + time.tv_nsec;
    return TRUE;
}
WINAPI(QueryPerformanceCounter)

__winfnc DWORD GetTickCount() {
    uint64_t counter;
    if(!QueryPerformanceCounter(&counter)) { log_error("QueryPerformanceCounter failed!"); abort(); }
    return (DWORD) counter;
}
WINAPI(GetTickCount)

__winfnc void GetSystemTimeAsFileTime(FILETIME *outTime) {
    struct timeval time;
    assert(gettimeofday(&time, NULL) == 0);

    uint64_t us = time.tv_sec * 10000lu + time.tv_usec;
    outTime->dwLowDateTime =  (DWORD) ((us >>  0) & 0xffffffffu);
    outTime->dwHighDateTime = (DWORD) ((us >> 32) & 0xffffffffu);
}
WINAPI(GetSystemTimeAsFileTime)

/* --- ADD THIS TO THE END OF time.c --- */

typedef struct {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

__winfnc void GetLocalTime(SYSTEMTIME *lpSystemTime) {
    time_t rawtime;
    struct tm *info;
    
    time(&rawtime);
    info = localtime(&rawtime);

    lpSystemTime->wYear = info->tm_year + 1900;
    lpSystemTime->wMonth = info->tm_mon + 1;
    lpSystemTime->wDayOfWeek = info->tm_wday;
    lpSystemTime->wDay = info->tm_mday;
    lpSystemTime->wHour = info->tm_hour;
    lpSystemTime->wMinute = info->tm_min;
    lpSystemTime->wSecond = info->tm_sec;
    lpSystemTime->wMilliseconds = 0;
}
WINAPI(GetLocalTime)

/* --- ADD TO END OF time.c --- */

__winfnc BOOL QueryPerformanceFrequency(int64_t *lpFrequency) {
    // Return a standard frequency (e.g., 1 microsecond resolution)
    *lpFrequency = 1000000LL; 
    return TRUE;
}
WINAPI(QueryPerformanceFrequency)

__winfnc uint64_t GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
WINAPI(GetTickCount64)

__winfnc BOOL SystemTimeToFileTime(const void *lpSystemTime, void *lpFileTime) {
    // Stub: Convert current system time to file time if needed, 
    // or just zero it out to prevent crashes.
    if (lpFileTime) memset(lpFileTime, 0, 8);
    return TRUE;
}
WINAPI(SystemTimeToFileTime)