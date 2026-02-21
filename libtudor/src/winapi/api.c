#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include <stdio.h> // For debug logs

// --- EXTERNAL DECLARATIONS (Force Link) ---
extern NTSTATUS WdfDriverCreate(void*, void*, void*, void*, void*, void*);
extern void* WdfDriverGetRegistryPath(void*);
extern NTSTATUS WdfDeviceCreate(void*, void*, void*, void*);
extern NTSTATUS WdfDeviceCreateDeviceInterface(void*, void*, void*, void*);
extern NTSTATUS WdfDeviceRetrieveDeviceInterfaceString(void*, void*, void*, void*, void*);
extern void WdfObjectGetTypedContextWorker(void*, void*);
extern NTSTATUS WdfObjectCreate(void*, void*, void*);
extern void WdfObjectReferenceActual(void*, void*, void*, void*, void*);
extern void WdfObjectDereferenceActual(void*, void*, void*, void*, void*);

static struct __winapi_descr *descr_head;

void __register_windows_api(struct __winapi_descr *descr) {
    descr->next = descr_head;
    descr_head = descr;
}

void *resolve_windows_api(const char *name) {
    // 1. Try the dynamic list first
    for(struct __winapi_descr *d = descr_head; d; d = d->next) {
        if(strcmp(d->name, name) == 0) return d->func;
    }

    // 2. CRITICAL FALLBACK: Manual Resolution
    // This catches functions when constructors fail to run.
    if (strcmp(name, "WdfDriverCreate") == 0) return (void*) WdfDriverCreate;
    if (strcmp(name, "WdfDriverGetRegistryPath") == 0) return (void*) WdfDriverGetRegistryPath;
    if (strcmp(name, "WdfDeviceCreate") == 0) return (void*) WdfDeviceCreate;
    if (strcmp(name, "WdfDeviceCreateDeviceInterface") == 0) return (void*) WdfDeviceCreateDeviceInterface;
    if (strcmp(name, "WdfDeviceRetrieveDeviceInterfaceString") == 0) return (void*) WdfDeviceRetrieveDeviceInterfaceString;
    if (strcmp(name, "WdfObjectGetTypedContextWorker") == 0) return (void*) WdfObjectGetTypedContextWorker;
    if (strcmp(name, "WdfObjectCreate") == 0) return (void*) WdfObjectCreate;
    if (strcmp(name, "WdfObjectReferenceActual") == 0) return (void*) WdfObjectReferenceActual;
    if (strcmp(name, "WdfObjectDereferenceActual") == 0) return (void*) WdfObjectDereferenceActual;

    // Log failure to help debug missing imports
    fprintf(stderr, "[WARN] resolve_windows_api failed for '%s'\n", name);
    return NULL;
}

int winstr_len(const char16_t *str) {
    int len = 0;
    for(const char16_t *p = str; *p; p++) len++;
    return len;
}

char16_t *winstr_from_str(const char *str) {
    if(!str) return NULL;
    mbstate_t mstate;

    //Determine length of converted string
    int len = 0;
    mstate = (mbstate_t) {0};
    for(const char *p = str; *p;) {
        p += mbrtoc16(NULL, p, MB_CUR_MAX, &mstate);
        len++;
    }

    //Convert string
    char16_t *wstr = (char16_t*) malloc((len + 1) * sizeof(char16_t));
    if(!wstr) { perror("Couldn't allocate memory for string"); abort(); }
    memset(wstr, 0, (len + 1) * sizeof(char16_t));
    mstate = (mbstate_t) {0};
    for(char16_t *d = wstr; *str; d++) {
        str += mbrtoc16(d, str, MB_CUR_MAX, &mstate);
    }

    return wstr;
}

char *winstr_to_str(const char16_t *wstr) {
    if(!wstr) return NULL;
    mbstate_t mstate;

    //Determine length of converted string
    int len = 0;
    mstate = (mbstate_t) {0};
    for(const char16_t *p = wstr; *p; p++) {
        len += c16rtomb(NULL, *p, &mstate);
    }

    //Convert string
    char *str = (char*) malloc(len + 1);
    if(!str) { perror("Couldn't allocate memory for string"); abort(); }
    memset(str, 0, len + 1);
    mstate = (mbstate_t) {0};
    for(char *d = str; *wstr; wstr++) {
        d += c16rtomb(d, *wstr, &mstate);
    }

    return str;
}