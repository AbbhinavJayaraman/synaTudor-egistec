#include "wdf.h"
#include "stub.h"
#include <stdio.h>

WUDF_LOADER_FX_INTERFACE wdf_loader;
void *wdf_functions[NUM_WDF_FUNCS];
WDF_DRIVER_GLOBALS wdf_globals;

// --- Debug Stub ---
// We need a way to know WHICH function was called. 
// Since we can't pass arguments to the stub easily without assembly, 
// we will just print a generic error.
__winfnc static void wdf_func_stub_generic(void) {
    fprintf(stderr, "\n[CRITICAL] UNIMPLEMENTED WDF FUNCTION CALLED!\n");
    fprintf(stderr, "[CRITICAL] The driver tried to call a WDF function we haven't mapped.\n");
    fprintf(stderr, "[CRITICAL] This usually means we need to implement a function at a high index.\n");
    abort();
}

__winfnc static NTSTATUS wdf_bind_version(void *ctx, WDF_BIND_INFO *bind_info, void **comp_globals) {
    char *ccomp = winstr_to_str(bind_info->Component);
    fprintf(stderr, "[DBG] Driver requesting WDF Bind: '%s' Ver %u.%u\n", ccomp, bind_info->Version.Major, bind_info->Version.Minor);
    free(ccomp);

    fprintf(stderr, "[DBG] WDF Function Table @ %p (Size: %d slots)\n", wdf_functions, NUM_WDF_FUNCS);

    // Force Fill Empty Slots
    int filled = 0;
    for(int i = 0; i < NUM_WDF_FUNCS; i++) {
        if(!wdf_functions[i]) {
            wdf_functions[i] = (void*) wdf_func_stub_generic;
        } else {
            filled++;
        }
    }
    
    fprintf(stderr, "[DBG] WDF Table Initialized. %d functions implemented, %d stubs.\n", filled, NUM_WDF_FUNCS - filled);

    // Verify Index 0
    if (wdf_functions[0] == (void*) wdf_func_stub_generic) {
        fprintf(stderr, "[WARN] Index 0 (WdfDriverCreate) is NOT IMPLEMENTED! Check linker.\n");
    } else {
        fprintf(stderr, "[INFO] Index 0 (WdfDriverCreate) is mapped to %p\n", wdf_functions[0]);
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