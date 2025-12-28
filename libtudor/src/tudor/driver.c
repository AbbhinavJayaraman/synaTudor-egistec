#include "internal.h"

extern uint8_t _binary____libtudor_drivers_EgisTouchFPSensor0575_dll_start, _binary____libtudor_drivers_EgisTouchFPSensor0575_dll_end;
extern uint8_t _binary____libtudor_drivers_EgisTouchFP0575_dll_start, _binary____libtudor_drivers_EgisTouchFP0575_dll_end;

#define NUM_WINDRV_DLLS 2
struct windrv_dll tudor_windrv_dlls[] = {
    {
        .module = {
            .name = "EgisTouchFPSensor0575.dll",
            .cmdline = "EgisTouchFPSensor0575.dll",
            .environ = (const char*[]) { NULL }
        },
        .pe_image = &_binary____libtudor_drivers_EgisTouchFPSensor0575_dll_start, .pe_image_end = &_binary____libtudor_drivers_EgisTouchFPSensor0575_dll_end,
        .is_adapter = true, .is_driver = false
    },
    {
        .module = {
            .name = "EgisTouchFP0575.dll",
            .cmdline = "EgisTouchFP0575.dll",
            .environ = (const char*[]) { NULL }
        },
        .pe_image = &_binary____libtudor_drivers_EgisTouchFP0575_dll_start, .pe_image_end = &_binary____libtudor_drivers_EgisTouchFP0575_dll_end,
        .is_adapter = false, .is_driver = true
    }
};

bool tudor_log_traces;

static struct winmodule ntdll_module = {
    .name = "ntdll.dll",
    .cmdline = "ntdll.dll",
    .environ = (const char*[]) { NULL }
};

#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
typedef BOOL __winfnc (*api_DllMain)(HANDLE hinstDLL, int fdwReason, void *lpReserved);

struct windrv_dll *tudor_adapter_dll, *tudor_driver_dll;
WINBIO_SENSOR_INTERFACE *tudor_sensor_adapter;
WINBIO_ENGINE_INTERFACE *tudor_engine_adapter;

static DRIVER_OBJECT umdf_driver;
struct winwdf_driver *tudor_wdf_driver;

bool tudor_init() {
    //Register dummy modules
    winmodule_register(&ntdll_module);

    if(tudor_log_traces) {
        //Register trace messages
        winlog_register_trace_msg(DEFINE_GUID(58f95b1a, 8efd, 39f0, 5626, 3e620b587295), 0x0c, "%s<X> checkpoint hit <X>");
        winlog_register_trace_msg(DEFINE_GUID(58f95b1a, 8efd, 39f0, 5626, 3e620b587295), 0x0d, "%s<X> checkpoint hit <X>");

        winlog_register_trace_msg(DEFINE_GUID(824d7f8b, e993, 3db5, 6a1a, 91a0d317b75a), 0x0a, "%s-> %s");
        winlog_register_trace_msg(DEFINE_GUID(824d7f8b, e993, 3db5, 6a1a, 91a0d317b75a), 0x0b, "%s<- %s");
        winlog_register_trace_msg(DEFINE_GUID(824d7f8b, e993, 3db5, 6a1a, 91a0d317b75a), 0x0c, "%s<- %s [0x%x]");
        winlog_register_trace_msg(DEFINE_GUID(824d7f8b, e993, 3db5, 6a1a, 91a0d317b75a), 0x0d, "%s-> %s");
        winlog_register_trace_msg(DEFINE_GUID(824d7f8b, e993, 3db5, 6a1a, 91a0d317b75a), 0x0f, "%s<- %s [0x%x]");

        winlog_register_trace_msg(DEFINE_GUID(2c18840b, 2ee0, 377e, f168, 1552bbd307c4), 0x0a, "VFM LOG | %s\033[1A");
    }

    //Set registry handler
    winreg_set_handler(tudor_reg_handler, NULL);

    //Load driver DLLs
    tudor_adapter_dll = tudor_driver_dll = NULL;
    for(int i = 0; i < NUM_WINDRV_DLLS; i++) {
        struct windrv_dll *dll = &tudor_windrv_dlls[i];
        if(!load_dll(&dll->image, dll->module.name, dll->pe_image, dll->pe_image_end - dll->pe_image)) {
            log_error("Error loading driver DLL!");
            return false;
        }
        winmodule_register(&dll->module);
        log_info("Loaded driver DLL '%s' [%ld bytes]", dll->module.name, dll->pe_image_end - dll->pe_image);

        if(dll->is_adapter) tudor_adapter_dll = dll;
        if(dll->is_driver) tudor_driver_dll = dll;
    }
    if(!tudor_adapter_dll) abort();
    if(!tudor_driver_dll) abort();

    //Initialize driver DLLs
    for(int i = 0; i < NUM_WINDRV_DLLS; i++) {
        struct windrv_dll *dll = &tudor_windrv_dlls[i];

        if(dll->image.entry_point) {
            log_info("Initializing driver DLL '%s'...", dll->module.name);
            winmodule_set_cur(&dll->module);
            if(!((api_DllMain) dll->image.entry_point)(dll->module.handle, DLL_PROCESS_ATTACH, NULL)) {
                log_error("Error initializing driver DLL '%s'!", dll->module.name);
                return false;
            }
        }
    }

    //Call UMDF driver entry function
    init_winwdf();
    winmodule_set_cur(&tudor_driver_dll->module);

    char16_t *reg_path_wstr = winstr_from_str("HKEY_LOCAL_MACHINE\\Tudor\\Driver");
    UNICODE_STRING reg_path = {
        .Length = winstr_len(reg_path_wstr)+1,
        .MaximumLength = winstr_len(reg_path_wstr)+1,
        .Buffer = reg_path_wstr
    };

    NTSTATUS status;
    if((status = ((api_FxDriverEntryUm) find_dll_export(&tudor_driver_dll->image, "FxDriverEntryUm"))(&wdf_loader, NULL, &umdf_driver, &reg_path)) != 0) {
        log_error("Error in UMDF driver entry function: 0x%x!", status);
        return false;
    }

    free(reg_path_wstr);

    if(!(tudor_wdf_driver = winwdf_get_driver(&wdf_globals))) {
        log_error("UMDF entry function didn't create a WDF driver!");
        return false;
    }

    //Query WINBIO interfaces
    winmodule_set_cur(&tudor_adapter_dll->module);

    HRESULT hres;
    if((hres = ((api_WbioQuerySensorInterface) find_dll_export(&tudor_adapter_dll->image, "WbioQuerySensorInterface"))(&tudor_sensor_adapter)) != 0) {
        log_error("Error querying sensor interface: 0x%x!", hres);
        return false;
    }

    // // --- Egis 0575 Production Fix (Struct-Mapped) ---
    // // We calculate function addresses relative to the 'Attach' anchor
    // // and assign them to the CORRECT WINBIO_SENSOR_INTERFACE members.
    
    // log_info("[*] Applying Egis 0575 Function Mapping...");

    // // 1. Recover the Anchor (Attach)
    // // We know the DLL puts Attach (0x1460) into slot 4 (normally Detach).
    // void** rawTable = (void**)tudor_sensor_adapter;
    // uintptr_t anchor = (uintptr_t)rawTable[4]; 
    
    // log_info("[*] Anchor Address (0x1460): %p", (void*)anchor);

    // // 2. Define the Function Pointers (Calculated via RVAs)
    // // All offsets calculated from your Ghidra list relative to Attach (0x1460)
    // void* fnAttach       = (void*)(anchor);          // 0x1460
    // void* fnDetach       = (void*)(anchor + 0x100);  // 0x1560
    // void* fnClear        = (void*)(anchor + 0x210);  // 0x1670
    // void* fnQueryStatus  = (void*)(anchor + 0x2A0);  // 0x1700
    // void* fnReset        = (void*)(anchor + 0x4B0);  // 0x1910
    // void* fnSetMode      = (void*)(anchor + 0x670);  // 0x1AD0
    // void* fnStartCapture = (void*)(anchor + 0xAA0);  // 0x1F00
    // void* fnCancel       = (void*)(anchor - 0x90);   // 0x13D0
    
    // // Indicator functions (Optional but good for completeness)
    // void* fnSetInd       = (void*)(anchor + 0x6D0);  // 0x1B30 (1B30 - 1460 = 6D0)
    // void* fnGetInd       = (void*)(anchor + 0x8B0);  // 0x1D10 (1D10 - 1460 = 8B0)

    // // 3. Populate the Struct Members
    // // This maps the code explicitly to the valid struct members you provided.
    // tudor_sensor_adapter->Attach       = (IBIO_SENSOR_ATTACH_FN*)fnAttach;
    // tudor_sensor_adapter->Detach       = (IBIO_SENSOR_DETACH_FN*)fnDetach;
    // tudor_sensor_adapter->ClearContext = (IBIO_SENSOR_CLEAR_CONTEXT_FN*)fnClear;
    // tudor_sensor_adapter->QueryStatus  = (IBIO_SENSOR_QUERY_STATUS_FN*)fnQueryStatus;
    // tudor_sensor_adapter->Reset        = (IBIO_SENSOR_RESET_FN*)fnReset;
    // tudor_sensor_adapter->SetMode      = (IBIO_SENSOR_SET_MODE_FN*)fnSetMode;
    // tudor_sensor_adapter->StartCapture = (IBIO_SENSOR_START_CAPTURE_FN*)fnStartCapture;
    // tudor_sensor_adapter->Cancel       = (IBIO_SENSOR_CANCEL_FN*)fnCancel;
    
    // // Optional indicator support
    // tudor_sensor_adapter->SetIndicatorStatus = (IBIO_SENSOR_SET_INDICATOR_STATUS_FN*)fnSetInd;
    // tudor_sensor_adapter->GetIndicatorStatus = (IBIO_SENSOR_GET_INDICATOR_STATUS_FN*)fnGetInd;

    // log_info("[*] Mapped StartCapture: %p", tudor_sensor_adapter->StartCapture);
    // ---------------------------------

    // --- Egis 0575 Final Map ---
    log_info("[*] Applying Explicit Function Map...");

    void** raw = (void**)tudor_sensor_adapter;
    
    // 1. Connectivity (Shifted by 1)
    tudor_sensor_adapter->Attach       = (WINBIO_SENSOR_ATTACH_FN)raw[4];
    tudor_sensor_adapter->Detach       = (WINBIO_SENSOR_DETACH_FN)raw[5];
    tudor_sensor_adapter->ClearContext = (WINBIO_SENSOR_CLEAR_CONTEXT_FN)raw[6];

    // 2. Enrollment (Shifted by 1)
    // We map only what fits in the 0x98-byte table.
    tudor_sensor_adapter->EnrollBegin   = (WINBIO_SENSOR_ENROLL_BEGIN_FN)raw[16];
    tudor_sensor_adapter->EnrollCapture = (WINBIO_SENSOR_ENROLL_CAPTURE_FN)raw[17];
    tudor_sensor_adapter->EnrollCommit  = (WINBIO_SENSOR_ENROLL_COMMIT_FN)raw[18];

    // 3. Dangerous / Missing Functions
    // EnrollDiscard falls off the table (Slot 19). Force it to NULL.
    // The driver will skip calling it or handle it gracefully.
    tudor_sensor_adapter->EnrollDiscard = NULL;

    // 4. Cancel (Special Case)
    // Cancel is not in the table, it is located before Attach.
    // Attach is at raw[4]. Cancel is Attach - 0x90.
    uintptr_t attachAddr = (uintptr_t)raw[4];
    tudor_sensor_adapter->Cancel = (WINBIO_SENSOR_CANCEL_FN)(attachAddr - 0x90);

    log_info("[*] Fixed Attach: %p", tudor_sensor_adapter->Attach);
    log_info("[*] Fixed EnrollBegin: %p", tudor_sensor_adapter->EnrollBegin);
    log_info("[*] Fixed Cancel: %p", tudor_sensor_adapter->Cancel);
    // ---------------------------------

    // // --- FORCE DEBUG PRINT (Unbuffered) ---
    // fprintf(stderr, "\n\n[!!!] ENTERING TUDOR_INIT TABLE DUMP [!!!]\n");
    
    // void** raw = (void**)tudor_sensor_adapter;
    
    // // Dump slots directly to stderr
    // for(int i=0; i<32; i++) {
    //     fprintf(stderr, "RAW DUMP [%02d]: %p\n", i, raw[i]);
    // }
    // fprintf(stderr, "[!!!] DUMP COMPLETE [!!!]\n\n");
    // fflush(stderr); // Force output immediately
    
    // // Minimal Keep-Alive Fix
    // tudor_sensor_adapter->Attach = raw[4];
    // tudor_sensor_adapter->Detach = raw[5];
    // // -------------------------
    
    // if((hres = ((api_WbioQueryEngineInterface) find_dll_export(&tudor_adapter_dll->image, "WbioQueryEngineInterface"))(&tudor_engine_adapter)) != 0) {
    //     log_error("Error querying engine interface: 0x%x!", hres);
    //     return false;
    // }
    
    
    return true;
}

bool tudor_shutdown() {
    //Unload the driver
    winmodule_set_cur(&tudor_driver_dll->module);

    log_debug("Unloading WDF driver...");
    winwdf_unload_driver(tudor_wdf_driver);
    
    if(umdf_driver.DriverUnload) {
        log_debug("Unloading UMDF driver...");
        umdf_driver.DriverUnload(&umdf_driver);
    }
    umdf_driver = (DRIVER_OBJECT) {0};

    //Uninitialize driver DLLs
    for(int i = 0; i < NUM_WINDRV_DLLS; i++) {
        struct windrv_dll *dll = &tudor_windrv_dlls[i];

        if(dll->image.entry_point) {
            log_info("Uninitializing driver DLL '%s'...", dll->module.name);
            winmodule_set_cur(&dll->module);
            if(!((api_DllMain) dll->image.entry_point)(dll->module.handle, DLL_PROCESS_DETACH, NULL)) {
                log_error("Error uninitializing driver DLL '%s'!", dll->module.name);
                return false;
            }
        }
        winmodule_unregister(&dll->module);
    }

    //Destroy driver DLLs
    for(int i = 0; i < NUM_WINDRV_DLLS; i++) destroy_dll(&tudor_windrv_dlls[i].image);

    //Unregister dummy modules
    winmodule_unregister(&ntdll_module);

    return true;
}