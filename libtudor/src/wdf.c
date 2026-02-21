#include "wdf.h"
#include "stub.h"
#include <stdio.h>

WUDF_LOADER_FX_INTERFACE wdf_loader;
void *wdf_functions[NUM_WDF_FUNCS];
WDF_DRIVER_GLOBALS wdf_globals;

// --- EXTERNAL DECLARATIONS ---
extern NTSTATUS WdfDriverCreate(void*, void*, void*, void*, void*, void*);
extern void* WdfDriverGetRegistryPath(void*);
extern NTSTATUS WdfDeviceCreate(void*, void*, void*, void*);
extern NTSTATUS WdfDeviceCreateDeviceInterface(void*, void*, void*, void*);
extern NTSTATUS WdfDeviceRetrieveDeviceInterfaceString(void*, void*, void*, void*, void*);
extern void WdfObjectGetTypedContextWorker(void*, void*);
extern NTSTATUS WdfObjectCreate(void*, void*, void*);
extern void WdfObjectReferenceActual(void*, void*, void*, void*, void*);
extern void WdfObjectDereferenceActual(void*, void*, void*, void*, void*);

// --- GENERATE 1024 STUBS ---
// This allows us to know EXACTLY which index was called.
#define STUB_GEN(x) \
    __winfnc static void wdf_stub_##x(void) { \
        fprintf(stderr, "\n[CRITICAL] Driver called Unimplemented WDF Function at INDEX %d\n", x); \
        abort(); \
    }

// Use a recursive include or just copy-paste a few blocks? 
// Macros can't loop easily. We will do a smaller batch or use a lookup.
// Actually, since we can't easily generate 1024 unique functions without python,
// we will stick to a single stub but try to infer the index from the stack? No, too hard.

// Alternative: We only generate stubs for likely candidates (0-100).
// But the crash is likely at a specific index we missed.

// Let's rely on the manual registration for now, but with a twist.
// We will print the entire table to the log so we can verify it in the GDB output.

__winfnc static void wdf_func_stub_generic(void) {
    fprintf(stderr, "\n[CRITICAL] UNIMPLEMENTED WDF FUNCTION CALLED!\n");
    abort();
}

static void register_all_wdf_functions() {
    for(int i = 0; i < NUM_WDF_FUNCS; i++) {
        wdf_functions[i] = (void*) wdf_func_stub_generic;
    }

    // Critical Functions
    wdf_functions[57] = (void*) WdfDriverCreate;
    wdf_functions[58] = (void*) WdfDriverGetRegistryPath;
    wdf_functions[55] = (void*) WdfDeviceCreate;
    wdf_functions[61] = (void*) WdfDeviceCreateDeviceInterface; 
    wdf_functions[260] = (void*) WdfObjectGetTypedContextWorker;
    wdf_functions[81] = (void*) WdfObjectCreate;
    wdf_functions[84] = (void*) WdfObjectReferenceActual;
    wdf_functions[86] = (void*) WdfObjectDereferenceActual;
    
    // ADD THIS: Index 0 mapping (Legacy/Safety)
    // Some drivers check Index 0 first.
    wdf_functions[0] = (void*) WdfDriverCreate; 
}

__winfnc static NTSTATUS wdf_bind_version(void *ctx, WDF_BIND_INFO *bind_info, void **comp_globals) {
    char *ccomp = winstr_to_str(bind_info->Component);
    fprintf(stderr, "[DBG] Driver requesting WDF Bind: '%s' Ver %u.%u (Expects %u funcs)\n", 
            ccomp, bind_info->Version.Major, bind_info->Version.Minor, bind_info->FuncCount);
    free(ccomp);

    // RUN MANUAL REGISTRATION
    register_all_wdf_functions();

    // Debug: Print the first 60 entries to verify layout
    for(int i=0; i<60; i++) {
        if(wdf_functions[i] != (void*) wdf_func_stub_generic) {
            fprintf(stderr, "[DBG] Index %d -> %p\n", i, wdf_functions[i]);
        }
    }

    *bind_info->FuncTable = wdf_functions;
    *comp_globals = &wdf_globals;

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