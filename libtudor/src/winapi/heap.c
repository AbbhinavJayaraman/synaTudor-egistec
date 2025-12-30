#include <stdlib.h>
#include <string.h>
#include "internal.h"

#define PROC_HEAP_HANDLE ((HANDLE) (uintptr_t) 0x50524f4348454150) /* PROCHEAP */

#define HEAP_GENERATE_EXCEPTIONS 0x00000004
#define HEAP_NO_SERIALIZE 0x00000001
#define HEAP_ZERO_MEMORY 0x00000008

__winfnc HANDLE GetProcessHeap() { return PROC_HEAP_HANDLE; }
WINAPI(GetProcessHeap)

__winfnc void *HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size) {
    if(heap != PROC_HEAP_HANDLE) {
        log_warn("HeapAlloc called with invalid heap handle");
        winerr_set();
        return NULL;
    }

    //Allocate the memory
    void *mem = malloc(size);
    if(mem) {
        if(flags & HEAP_ZERO_MEMORY) memset(mem, 0, size);
        return mem;
    }

    //There was an error allocating the memory
    if(flags & HEAP_GENERATE_EXCEPTIONS) {
        perror("Error allocating memory for HeapAlloc");
        log_error("HeapAlloc: HEAP_GENERATE_EXCEPTIONS flag set and memory allocation failed!");
        abort();
    }

    winerr_set_errno();
    return NULL;
}
WINAPI(HeapAlloc)

__winfnc BOOL HeapFree(HANDLE heap, DWORD flags, void *mem) {
    if(heap != PROC_HEAP_HANDLE) {
        log_warn("HeapAlloc called with invalid heap handle");
        winerr_set();
        return FALSE;
    }

    free(mem);
    return TRUE;
}
WINAPI(HeapFree)

__winfnc void *LocalFree(void *mem) {
    free(mem);
    return NULL;
}
WINAPI(LocalFree)

/* --- ADD THIS TO THE END OF heap.c --- */

__winfnc void *EncodePointer(void *Ptr) {
    return Ptr; // Bypass security encoding
}
WINAPI(EncodePointer)

__winfnc void *DecodePointer(void *Ptr) {
    return Ptr; // Bypass security decoding
}
WINAPI(DecodePointer)

/* --- ADD TO END OF heap.c --- */

__winfnc void *HeapReAlloc(HANDLE hHeap, DWORD dwFlags, void *lpMem, SIZE_T dwBytes) {
    // Simple wrapper around realloc
    return realloc(lpMem, dwBytes);
}
WINAPI(HeapReAlloc)

__winfnc SIZE_T HeapSize(HANDLE hHeap, DWORD dwFlags, const void *lpMem) {
    // In Linux, we can't easily get the size of a malloc'd block portably.
    // However, many drivers check this. We can try malloc_usable_size if using glibc,
    // or just return a "safe" lie if that fails.
    #ifdef __GLIBC__
    extern size_t malloc_usable_size(void *);
    return malloc_usable_size((void*)lpMem);
    #else
    return 0; // Unknown
    #endif
}
WINAPI(HeapSize)

// Local* functions often map directly to Heap* functions in modern Windows
__winfnc HLOCAL LocalAlloc(UINT uFlags, SIZE_T uBytes) {
    // Ignore flags (LMEM_FIXED/ZEROINIT) for now, just malloc
    void *ptr = malloc(uBytes);
    if (ptr && (uFlags & 0x0040)) memset(ptr, 0, uBytes); // LMEM_ZEROINIT
    return ptr;
}
WINAPI(LocalAlloc)

__winfnc HLOCAL LocalReAlloc(HLOCAL hMem, SIZE_T uBytes, UINT uFlags) {
    return realloc(hMem, uBytes);
}
WINAPI(LocalReAlloc)

__winfnc UINT LocalSize(HLOCAL hMem) {
    return (UINT)HeapSize(NULL, 0, hMem);
}
WINAPI(LocalSize)