#include "internal.h"

// void register_shims();

//Only the two WinBIO adapters are relinked. EgisTouchFP0575.dll - the UMDF
//driver - is deliberately not loaded: its entire job was translating biometric
//IOCTLs into EGIS bulk transfers, which src/tudor/egis.c now does natively.
//Dropping it is what removes the need for the WDF/UMDF emulation layer.
extern uint8_t _binary____libtudor_drivers_EgisTouchFPSensor0575_dll_start, _binary____libtudor_drivers_EgisTouchFPSensor0575_dll_end;
extern uint8_t _binary____libtudor_drivers_EgisTouchFPEngine0575_dll_start, _binary____libtudor_drivers_EgisTouchFPEngine0575_dll_end;

struct windrv_dll tudor_windrv_dlls[] = {
    {
        .module = {
            .name = "EgisTouchFPSensor0575.dll",
            .cmdline = "EgisTouchFPSensor0575.dll",
            .environ = (const char*[]) { NULL }
        },
        .pe_image = &_binary____libtudor_drivers_EgisTouchFPSensor0575_dll_start, .pe_image_end = &_binary____libtudor_drivers_EgisTouchFPSensor0575_dll_end,
        .is_adapter = true, .is_driver = false, .is_engine = false
    },
    {
        .module = {
            .name = "EgisTouchFPEngine0575.dll",
            .cmdline = "EgisTouchFPEngine0575.dll",
            .environ = (const char*[]) { NULL }
        },
        .pe_image = &_binary____libtudor_drivers_EgisTouchFPEngine0575_dll_start, .pe_image_end = &_binary____libtudor_drivers_EgisTouchFPEngine0575_dll_end,
        .is_adapter = false, .is_driver = false, .is_engine = true
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

struct windrv_dll *tudor_adapter_dll, *tudor_engine_dll;
WINBIO_SENSOR_INTERFACE *tudor_sensor_adapter;
WINBIO_ENGINE_INTERFACE *tudor_engine_adapter;


bool tudor_init() {
    //The log splits across stdout (verbose/debug/info) and stderr (warn/error).
    //stderr is unbuffered but stdout is block-buffered as soon as it is not a
    //terminal, so a crash discards up to a bufferful of the trace - including the
    //lines identifying where it died. Line buffering costs nothing at these rates
    //and makes the trace trustworthy as a crash record.
    setvbuf(stdout, NULL, _IOLBF, 0);

    //Register dummy modules
    winmodule_register(&ntdll_module);

    // register_shims();

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

    //Set registry handlers
    winreg_set_handler(tudor_reg_handler, NULL);
    winreg_set_enum_handler(tudor_reg_enum_handler, NULL);

    //Load driver DLLs
    tudor_adapter_dll = tudor_engine_dll = NULL;
    for(int i = 0; i < NUM_WINDRV_DLLS; i++) {
        struct windrv_dll *dll = &tudor_windrv_dlls[i];
        if(!load_dll(&dll->image, dll->module.name, dll->pe_image, dll->pe_image_end - dll->pe_image)) {
            log_error("Error loading driver DLL!");
            return false;
        }
        winmodule_register(&dll->module);
        log_info("Loaded driver DLL '%s' [%ld bytes]", dll->module.name, dll->pe_image_end - dll->pe_image);

        if(dll->is_adapter) tudor_adapter_dll = dll;
        if(dll->is_engine) tudor_engine_dll = dll;
    }
    if(!tudor_adapter_dll) abort();
    if(!tudor_engine_dll) abort();

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

    //Query WINBIO interfaces
    winmodule_set_cur(&tudor_adapter_dll->module);

    HRESULT hres;
    if((hres = ((api_WbioQuerySensorInterface) find_dll_export(&tudor_adapter_dll->image, "WbioQuerySensorInterface"))(&tudor_sensor_adapter)) != 0) {
        log_error("Error querying sensor interface: 0x%x!", hres);
        return false;
    }

    //Query the engine interface - this is where enrollment and matching live
    winmodule_set_cur(&tudor_engine_dll->module);
    if((hres = ((api_WbioQueryEngineInterface) find_dll_export(&tudor_engine_dll->image, "WbioQueryEngineInterface"))(&tudor_engine_adapter)) != 0) {
        log_error("Error querying engine interface: 0x%x!", hres);
        return false;
    }

    //Worth logging, because the two adapters are different generations and Size
    //is what says which methods actually exist - see WINBIO_HAS_FNC in
    //internal.h. The sensor adapter is a 1.0 adapter with no pipeline
    //init/activate methods at all.
    log_info("Sensor adapter interface: version %u.%u type %u size 0x%zx (%zu methods)",
        (unsigned) tudor_sensor_adapter->Version.MajorVersion, (unsigned) tudor_sensor_adapter->Version.MinorVersion,
        (unsigned) tudor_sensor_adapter->Type, (size_t) tudor_sensor_adapter->Size,
        ((size_t) tudor_sensor_adapter->Size - offsetof(WINBIO_SENSOR_INTERFACE, Attach)) / sizeof(void*));
    log_info("Engine adapter interface: version %u.%u type %u size 0x%zx (%zu methods)",
        (unsigned) tudor_engine_adapter->Version.MajorVersion, (unsigned) tudor_engine_adapter->Version.MinorVersion,
        (unsigned) tudor_engine_adapter->Type, (size_t) tudor_engine_adapter->Size,
        ((size_t) tudor_engine_adapter->Size - offsetof(WINBIO_ENGINE_INTERFACE, Attach)) / sizeof(void*));


    return true;
}

bool tudor_shutdown() {
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
