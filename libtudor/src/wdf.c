#include "wdf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WUDF_LOADER_FX_INTERFACE wdf_loader;
void *wdf_functions[NUM_WDF_FUNCS];
WDF_DRIVER_GLOBALS wdf_globals;

// --- EXTERNAL DECLARATIONS ---
extern void* WdfDriverCreate;
extern void* WdfDriverGetRegistryPath;
extern void* WdfDeviceCreate;
extern void* WdfObjectGetTypedContextWorker;
extern void* WdfObjectCreate;
extern void* WdfObjectReferenceActual;
extern void* WdfObjectDereferenceActual;

// Pre-emptive externs from device.c
extern void* WdfDeviceInitSetPnpPowerEventCallbacks;
extern void* WdfDeviceInitSetPowerPolicyEventCallbacks;
extern void* WdfDeviceInitSetPowerPolicyOwnership;
extern void* WdfDeviceInitSetIoType;
extern void* WdfDeviceInitSetFileObjectConfig;
extern void* WdfDeviceRetrieveDeviceInterfaceString;
extern void* WdfDeviceCreateDeviceInterface;
extern void* WdfDeviceSetDeviceInterfaceState;
extern void* WdfDeviceAssignS0IdleSettings;
extern void* WdfDeviceAssignSxWakeSettings;

// Pre-emptive externs from queue.c
extern void* WdfIoQueueCreate;
extern void* WdfDeviceConfigureRequestDispatching;
extern void* WdfIoQueueStart;

// Pre-emptive externs from reg.c
extern void* WdfDeviceOpenRegistryKey;
extern void* WdfRegistryOpenKey;
extern void* WdfRegistryClose;
extern void* WdfRegistryQueryValue;
extern void* WdfRegistryAssignValue;
extern void* WdfRegistryAssignULong;

__winfnc static NTSTATUS wdf_stub_141_WdfRegistryQueryULong(void* globals, void* key, void* val_name, ULONG* out_val) {
    fprintf(stderr, "[WDF] Stubbed WdfRegistryQueryULong called! Returning default 0.\n");
    // The driver expects a value to be written to the pointer. We provide 0 as a safe default.
    if (out_val) *out_val = 0; 
    return 0; // STATUS_SUCCESS
}

// --- STUB FUNCTION ---
__winfnc static void wdf_func_stub_generic(void) {
    fprintf(stderr, "\n[CRITICAL] DRIVER CALLED UNIMPLEMENTED WDF FUNCTION!\n");
    abort();
}

static void register_all_wdf_functions() {
    // 1. Fill with loud stubs
    for(int i = 0; i < NUM_WDF_FUNCS; i++) {
        wdf_functions[i] = (void*) wdf_func_stub_generic;
    }

    // 2. Register Critical Functions
    wdf_functions[57] = (void*) &WdfDriverCreate;
    wdf_functions[58] = (void*) &WdfDriverGetRegistryPath;
    wdf_functions[55] = (void*) &WdfDeviceCreate;
    wdf_functions[25] = (void*) &WdfDeviceCreate;
    
    // UMDF 2.0 ABI Object methods
    wdf_functions[123] = (void*) &WdfObjectGetTypedContextWorker;
    wdf_functions[126] = (void*) &WdfObjectReferenceActual;
    wdf_functions[127] = (void*) &WdfObjectDereferenceActual;
    wdf_functions[128] = (void*) &WdfObjectCreate;

    // --- NEW DEVICE INIT FUNCTIONS ---
    wdf_functions[19] = (void*) &WdfDeviceInitSetPnpPowerEventCallbacks;
    wdf_functions[20] = (void*) &WdfDeviceInitSetPowerPolicyEventCallbacks;
    wdf_functions[21] = (void*) &WdfDeviceInitSetPowerPolicyOwnership;
    wdf_functions[22] = (void*) &WdfDeviceInitSetIoType;
    wdf_functions[23] = (void*) &WdfDeviceInitSetFileObjectConfig;

    // --- NEW DEVICE / INTERFACE FUNCTIONS ---
    wdf_functions[27] = (void*) &WdfDeviceCreateDeviceInterface;
    wdf_functions[28] = (void*) &WdfDeviceSetDeviceInterfaceState;
    wdf_functions[29] = (void*) &WdfDeviceRetrieveDeviceInterfaceString;
    wdf_functions[16] = (void*) &WdfDeviceAssignS0IdleSettings;
    wdf_functions[17] = (void*) &WdfDeviceAssignSxWakeSettings;

    // --- NEW QUEUE FUNCTIONS ---
    wdf_functions[85] = (void*) &WdfIoQueueCreate;
    wdf_functions[87] = (void*) &WdfIoQueueStart;
    wdf_functions[40] = (void*) &WdfDeviceConfigureRequestDispatching;

    // --- NEW REGISTRY FUNCTIONS ---
    wdf_functions[18]  = (void*) &WdfDeviceOpenRegistryKey;
    wdf_functions[131] = (void*) &WdfRegistryOpenKey;
    wdf_functions[133] = (void*) &WdfRegistryClose;
    wdf_functions[136] = (void*) &WdfRegistryQueryValue;
    wdf_functions[142] = (void*) &WdfRegistryAssignValue;
    wdf_functions[147] = (void*) &WdfRegistryAssignULong;
    wdf_functions[141] = (void*) wdf_stub_141_WdfRegistryQueryULong;

    // Safety: Map Index 0 to DriverCreate just in case
    wdf_functions[0] = (void*) &WdfDriverCreate;
}

__winfnc static NTSTATUS wdf_bind_version(void *ctx, WDF_BIND_INFO *bind_info, void **comp_globals) {
    char *ccomp = winstr_to_str(bind_info->Component);
    fprintf(stderr, "[DBG] Driver requesting WDF Bind: '%s' Ver %u.%u (Expects %u funcs)\n", 
            ccomp, bind_info->Version.Major, bind_info->Version.Minor, bind_info->FuncCount);
    free(ccomp);

    register_all_wdf_functions();

    void **driver_table = (void **)bind_info->FuncTable;
    for (ULONG i = 0; i < bind_info->FuncCount && i < NUM_WDF_FUNCS; i++) {
        driver_table[i] = wdf_functions[i];
    }

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