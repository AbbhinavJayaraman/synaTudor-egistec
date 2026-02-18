#include "wdf.h"
#include "stub.h"
#include <stdio.h> // For direct logging

WUDF_LOADER_FX_INTERFACE wdf_loader;
void *wdf_functions[NUM_WDF_FUNCS];
WDF_DRIVER_GLOBALS wdf_globals;

// --- Debug Stub ---
// We use __winfnc to ensure the calling convention matches what the driver expects (stdcall)
__winfnc static void wdf_func_stub_generic(void) {
    // Print to stderr directly to guarantee we see it before any crash
    fprintf(stderr, "\n[CRITICAL] UNIMPLEMENTED WDF FUNCTION CALLED!\n");
    fprintf(stderr, "[CRITICAL] The driver tried to call a WDF function we haven't mapped.\n");
    fprintf(stderr, "[CRITICAL] Execution trapped. Aborting.\n");
    abort();
}

__winfnc static NTSTATUS wdf_bind_version(void *ctx, WDF_BIND_INFO *bind_info, void **comp_globals) {
    char *ccomp = winstr_to_str(bind_info->Component);
    fprintf(stderr, "[DBG] Driver requesting WDF Bind: '%s' Ver %u.%u\n", ccomp, bind_info->Version.Major, bind_info->Version.Minor);
    free(ccomp);

    // 1. Log the table address we are giving the driver
    fprintf(stderr, "[DBG] WDF Function Table @ %p (Size: %d slots)\n", wdf_functions, NUM_WDF_FUNCS);

    // 2. Initialize function table (Force Fill)
    int filled = 0;
    for(int i = 0; i < NUM_WDF_FUNCS; i++) {
        if(!wdf_functions[i]) {
            // Force fill empty slots with our loud stub
            wdf_functions[i] = (void*) wdf_func_stub_generic;
        } else {
            filled++;
        }
    }
    
    fprintf(stderr, "[DBG] WDF Table Initialized. %d functions implemented, %d stubs.\n", filled, NUM_WDF_FUNCS - filled);

    // 3. Debug: Check Index 0 (WdfDriverCreate)
    if (wdf_functions[0] == (void*) wdf_func_stub_generic) {
        fprintf(stderr, "[WARN] Index 0 (WdfDriverCreate) is NOT IMPLEMENTED! Driver will likely crash.\n");
    } else {
        fprintf(stderr, "[INFO] Index 0 (WdfDriverCreate) is mapped to %p\n", wdf_functions[0]);
    }

    // Bind functions
    *bind_info->FuncTable = wdf_functions;
    *comp_globals = &wdf_globals;

    fprintf(stderr, "[DBG] WDF Bind Complete. Returning Success.\n");
    return 0; // STATUS_SUCCESS
}

void init_winwdf() {
    //Initialize WDF loader
    wdf_loader.Size = sizeof(wdf_loader);
    wdf_loader.VersionBind = wdf_bind_version;

    //Initialize globals
    wdf_globals.Driver = NULL;
    wdf_globals.DriverFlags = 0;
    wdf_globals.DriverTag = 0;
    strcpy(wdf_globals.DriverName, "TUDOR-DRIVER");
    wdf_globals.DisplaceDriverUnload = TRUE;
}