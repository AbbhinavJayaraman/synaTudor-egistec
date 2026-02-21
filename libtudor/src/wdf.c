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

// object.c?
extern void wdf_create_obj(void *parent, void *obj, void *destr, void *attrs);

// You might need to define this struct if it's not in your headers
typedef struct _WDF_USB_PIPE_INFORMATION {
    ULONG Size;
    ULONG MaximumPacketSize;
    UCHAR EndpointAddress;
    UCHAR Interval;
    UCHAR SettingIndex;
    ULONG PipeType; // WdfUsbPipeType
    ULONG MaximumTransferSize;
} WDF_USB_PIPE_INFORMATION;

__winfnc static void wdf_stub_220_Pass(void* globals, void* Pipe) {
    fprintf(stderr, "[WDF] WdfUsbTargetPipeSetNoMaximumPacketSizeCheck (Index 220) called.\n");
}

// Index 221: WdfUsbTargetPipeSetNoMaximumPacketSizeCheck
__winfnc static void wdf_stub_221_Pass(void* globals, void* Pipe) {
    fprintf(stderr, "[WDF] WdfUsbTargetPipeSetNoMaximumPacketSizeCheck called.\n");
}

// Index 103: WdfIoTargetStart
__winfnc static NTSTATUS wdf_stub_103_WdfIoTargetStart(void* globals, void* IoTarget) {
    fprintf(stderr, "[WDF] WdfIoTargetStart (Index 103) called. Returning SUCCESS.\n");
    return 0;
}

// Index 231: WdfUsbTargetPipeConfigContinuousReader
// This is a big one. Egis uses this to constantly poll the sensor.
__winfnc static NTSTATUS wdf_stub_231_ConfigReader(void* globals, void* Pipe, void* Config) {
    fprintf(stderr, "[WDF] WdfUsbTargetPipeConfigContinuousReader (Index 231) called. Success.\n");
    return 0;
}

// Index 198: WdfUsbTargetDeviceGetDeviceDescriptor
__winfnc static void wdf_stub_198_GetDevDesc(void* globals, void* Device, void* Descriptor) {
    fprintf(stderr, "[WDF] WdfUsbTargetDeviceGetDeviceDescriptor (Index 198) called.\n");
}

__winfnc static BYTE wdf_stub_238_WdfUsbInterfaceGetNumConfiguredPipes(void* globals, void* Interface) {
    fprintf(stderr, "[WDF] WdfUsbInterfaceGetNumConfiguredPipes (Index 238) called. Returning 2.\n");
    return 2; 
}

__winfnc static void* wdf_stub_239_WdfUsbInterfaceGetConfiguredPipe(void* globals, void* Interface, BYTE PipeIndex, WDF_USB_PIPE_INFORMATION* PipeInfo) {
    fprintf(stderr, "[WDF] WdfUsbInterfaceGetConfiguredPipe (Index 239) called for Pipe %u.\n", PipeIndex);
    
    // Allocate our dummy pipe object
    void* new_obj = calloc(1, 512);
    wdf_create_obj(Interface, new_obj, NULL, NULL);
    
    // If the driver asks for info, give it the exact profile of an Egis sensor
    if (PipeInfo && (uintptr_t)PipeInfo > 0x1000) {
        PipeInfo->MaximumPacketSize = 64;
        
        if (PipeIndex == 0) {
            PipeInfo->PipeType = 2; // WdfUsbPipeTypeBulk
            fprintf(stderr, "      -> Mocked Pipe 0 as BULK.\n");
        } else {
            PipeInfo->PipeType = 3; // WdfUsbPipeTypeInterrupt
            fprintf(stderr, "      -> Mocked Pipe 1 as INTERRUPT.\n");
        }
    }
    
    return new_obj;
}

__winfnc static void* wdf_stub_236_WdfUsbTargetDeviceGetInterface(void* globals, void* UsbDevice, UCHAR InterfaceIndex) {
    fprintf(stderr, "[WDF] WdfUsbTargetDeviceGetInterface (Index 236) called for index %u!\n", InterfaceIndex);
    
    // We allocate another dummy object to represent the USB Interface
    void* new_obj = calloc(1, 512); 
    // Initialize it as a WDF object. We use the UsbDevice as the parent.
    wdf_create_obj(UsbDevice, new_obj, NULL, NULL);
    
    fprintf(stderr, "      -> Provided dummy Interface handle at %p\n", new_obj);
    return new_obj; 
}

__winfnc static BOOLEAN wdf_stub_210_WdfUsbTargetDeviceIsConfigured(void* globals, void* UsbDevice) {
    fprintf(stderr, "[WDF] WdfUsbTargetDeviceIsConfigured (Index 210) called! Returning TRUE.\n");
    return TRUE; // Tell the driver the "hardware" is ready.
}

__winfnc static NTSTATUS wdf_stub_54_WdfDeviceQueryProperty(void* Device, ULONG DeviceProperty, ULONG BufferLength, void* PropertyBuffer, ULONG* ResultLength) {
    fprintf(stderr, "[WDF] WdfDeviceQueryProperty called! Forcing STATUS_SUCCESS.\n");

    // Tell the driver it needs 64 bytes (so it thinks it succeeded)
    if (ResultLength) *ResultLength = 64; 

    // If it gave us a buffer, zero it out so it doesn't read garbage string data.
    if (PropertyBuffer && BufferLength > 0) {
        memset(PropertyBuffer, 0, BufferLength);
    }

    // Never return an error. Always pretend it worked.
    return 0; // STATUS_SUCCESS
}

__winfnc static NTSTATUS wdf_stub_141_WdfRegistryQueryULong(void* globals, void* key, void* val_name, ULONG* out_val) {
    fprintf(stderr, "[WDF] Stubbed WdfRegistryQueryULong called! Returning default 0.\n");
    // The driver expects a value to be written to the pointer. We provide 0 as a safe default.
    if (out_val) *out_val = 0; 
    return 0; // STATUS_SUCCESS
}

__winfnc static NTSTATUS wdf_stub_202_CreateObject(void* globals, void* parent, void* attributes, void** out_handle) {
    fprintf(stderr, "[WDF] WdfUsbTargetDeviceCreate (Index 202) called! Allocating dummy target.\n");
    
    if (out_handle) {
        // Allocate a generic chunk of memory
        void* new_obj = calloc(1, 512); 
        // Initialize it as a real synaTudor WDF object so its internal locks work!
        wdf_create_obj(parent, new_obj, NULL, attributes);
        *out_handle = new_obj;
        fprintf(stderr, "      -> Provided dummy object handle at %p\n", new_obj);
    }
    
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
    wdf_functions[54] = (void*) wdf_stub_54_WdfDeviceQueryProperty;
    wdf_functions[202] = (void*) wdf_stub_202_CreateObject;
    wdf_functions[210] = (void*) wdf_stub_210_WdfUsbTargetDeviceIsConfigured;
    wdf_functions[236] = (void*) wdf_stub_236_WdfUsbTargetDeviceGetInterface;
    wdf_functions[238] = (void*) wdf_stub_238_WdfUsbInterfaceGetNumConfiguredPipes;
    wdf_functions[239] = (void*) wdf_stub_239_WdfUsbInterfaceGetConfiguredPipe;
    
    /* --- USB MEGA-MAPPINGS --- */
    wdf_functions[103] = (void*) wdf_stub_103_WdfIoTargetStart;
    wdf_functions[198] = (void*) wdf_stub_198_GetDevDesc;
    wdf_functions[220] = (void*) wdf_stub_220_Pass;
    wdf_functions[221] = (void*) wdf_stub_221_Pass;
    wdf_functions[231] = (void*) wdf_stub_231_ConfigReader;
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