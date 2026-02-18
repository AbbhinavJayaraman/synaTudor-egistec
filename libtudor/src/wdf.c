#include "wdf.h"
#include "stub.h"
#include <stdio.h>

WUDF_LOADER_FX_INTERFACE wdf_loader;
void *wdf_functions[NUM_WDF_FUNCS];
WDF_DRIVER_GLOBALS wdf_globals;

// --- EXTERNAL DECLARATIONS (So we can link them manually) ---
extern NTSTATUS WdfDriverCreate(void*, void*, void*, void*, void*, void*);
extern void* WdfDriverGetRegistryPath(void*);
extern NTSTATUS WdfDeviceCreate(void*, void*, void*, void*);
extern NTSTATUS WdfDeviceCreateDeviceInterface(void*, void*, void*, void*);
extern NTSTATUS WdfDeviceRetrieveDeviceInterfaceString(void*, void*, void*, void*, void*);
extern void WdfObjectGetTypedContextWorker(void*, void*);
extern NTSTATUS WdfObjectCreate(void*, void*, void*);
extern void WdfObjectReferenceActual(void*, void*, void*, void*, void*);
extern void WdfObjectDereferenceActual(void*, void*, void*, void*, void*);

// --- DEBUG STUB ---
__winfnc static void wdf_func_stub_generic(void) {
    fprintf(stderr, "\n[CRITICAL] UNIMPLEMENTED WDF FUNCTION CALLED!\n");
    fprintf(stderr, "[CRITICAL] Aborting.\n");
    abort();
}

// --- MANUAL REGISTRATION ---
static void register_all_wdf_functions() {
    // 1. Initialize everything to the loud stub
    for(int i = 0; i < NUM_WDF_FUNCS; i++) {
        wdf_functions[i] = (void*) wdf_func_stub_generic;
    }

    // 2. Register Critical Functions (Indices from Ghidra/Standard)
    // Driver
    wdf_functions[57] = (void*) WdfDriverCreate;          // Offset 0x1C8 / 8
    wdf_functions[58] = (void*) WdfDriverGetRegistryPath; // Offset 0x1D0 / 8

    // Device
    wdf_functions[55] = (void*) WdfDeviceCreate;          // Offset 0x1B8 / 8
    wdf_functions[61] = (void*) WdfDeviceCreateDeviceInterface; // Standard 2.0? Verify if crash.
    
    // Object (Standard Indices)
    wdf_functions[260] = (void*) WdfObjectGetTypedContextWorker;
    wdf_functions[81] = (void*) WdfObjectCreate;
    wdf_functions[84] = (void*) WdfObjectReferenceActual;
    wdf_functions[86] = (void*) WdfObjectDereferenceActual;

    fprintf(stderr, "[DBG] Manually registered functions at indices: 57, 58, 55, 260, 81...\n");
}

__winfnc static NTSTATUS wdf_bind_version(void *ctx, WDF_BIND_INFO *bind_info, void **comp_globals) {
    char *ccomp = winstr_to_str(bind_info->Component);
    fprintf(stderr, "[DBG] Driver requesting WDF Bind: '%s' Ver %u.%u (Expects %u funcs)\n", 
            ccomp, bind_info->Version.Major, bind_info->Version.Minor, bind_info->FuncCount);
    free(ccomp);

    if (bind_info->FuncCount > NUM_WDF_FUNCS) {
        fprintf(stderr, "[FATAL] Driver needs %u functions, we have %d.\n", bind_info->FuncCount, NUM_WDF_FUNCS);
        return 0xC0000001;
    }

    // RUN MANUAL REGISTRATION
    register_all_wdf_functions();

    // Verify Index 57 (DriverCreate) is filled
    if (wdf_functions[57] == (void*) wdf_func_stub_generic) {
        fprintf(stderr, "[FATAL] Index 57 (WdfDriverCreate) IS STILL A STUB! Linker error?\n");
    } else {
        fprintf(stderr, "[INFO] Index 57 (WdfDriverCreate) is mapped to %p\n", wdf_functions[57]);
    }

    *bind_info->FuncTable = wdf_functions;
    *comp_globals = &wdf_globals;

    fprintf(stderr, "[DBG] WDF Bind Complete. Returning Success.\n");
    return 0;
}

void init_winwdf() {
    wdf_loader.Size = sizeof(wdf_loader);
    wdf_loader.VersionBind = wdf_bind_version;

    wdf_globals.Driver = NULL;
    wdf_globals.DriverFlags = 0;
    wdf_globals.DriverTag = 0;
    strcpy(wdf_globals.DriverName, "TUDOR-DRIVER");
    wdf_globals.DisplaceDriverUnload = TRUE;
}