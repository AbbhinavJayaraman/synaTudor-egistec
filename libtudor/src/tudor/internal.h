#ifndef LIBTUDOR_TUDOR_INTERNAL_H
#define LIBTUDOR_TUDOR_INTERNAL_H

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <tudor/tudor.h>
#include <tudor/log.h>
#include "winbio.h"
#include "loader.h"
#include "winapi/api.h"

struct async_args_enroll {
    bool *done;
};

struct async_args_verify {
    RECGUID guid;
    enum tudor_finger finger;
    bool *retry;
    bool *matches;
};

struct async_args_identify {
    bool *retry;
    bool *found_match;
    RECGUID *guid;
    enum tudor_finger *finger;
};

struct _async_res {
    struct tudor_device *dev;
    OVERLAPPED *ovlp;
    union {
        struct async_args_enroll enroll;
        struct async_args_verify verify;
        struct async_args_identify identify; 
    } args;

    pthread_mutex_t lock;
    int success;
    pthread_cond_t compl_cond;
    tudor_async_cb_fnc *cb_fnc;
    void *cb_ctx;
};

tudor_async_res_t async_new_res(struct tudor_device *dev, OVERLAPPED *ovlp);
void async_complete_op(tudor_async_res_t res, bool success);

struct windrv_dll {
    struct winmodule module;
    uint8_t *pe_image, *pe_image_end;
    struct dll_image image;
    bool is_adapter, is_driver, is_engine;
};

//Sensor adapter + engine adapter. The UMDF driver DLL is not relinked - see
//the comment at the top of driver.c.
#define NUM_WINDRV_DLLS 2
extern struct windrv_dll tudor_windrv_dlls[];

#define WINBIO_CALL_PIPELINE(fnc, ...) if((hres = fnc(__VA_ARGS__)) != ERROR_SUCCESS) { log_error("Error in WINBIO pipeline function '%s': 0x%x!", #fnc, hres); return false; }

//A WinBIO adapter states in its own Size field how much of the interface struct
//it actually implements, and the two Egis adapters are different generations:
//
//  EgisTouchFPSensor0575  Version 1.0  Size 0x98  = 15 methods, last is ControlUnitPrivileged
//  EgisTouchFPEngine0575  Version 3.0  Size 0x168 = 41 methods
//
//So the sensor adapter has no PipelineInit, PipelineCleanup, Activate or
//Deactivate. Those fields sit past the end of its struct, where reading them
//picks up whatever follows the vtable in .rdata - calling that is a jump to a
//garbage address, which is exactly the SIGSEGV seen right after "Initializing
//pipeline interfaces...". Windows does not call them on a 1.0 adapter either, so
//skipping them is correct rather than a workaround.
//
//Every other method device.c uses is within the sensor adapter's first 15.
#define WINBIO_HAS_FNC(iface, fnc) \
    (offsetof(__typeof__(*(iface)), fnc) + sizeof((iface)->fnc) <= (size_t) (iface)->Size && (iface)->fnc != NULL)

//Calls a method that an older adapter version may not publish at all.
#define WINBIO_CALL_PIPELINE_OPT(iface, fnc, ...) { \
    if(WINBIO_HAS_FNC(iface, fnc)) { \
        WINBIO_CALL_PIPELINE((iface)->fnc, __VA_ARGS__) \
    } else { \
        log_debug("Adapter doesn't publish " #fnc " (interface size 0x%zx) - skipping", (size_t) (iface)->Size); \
    } \
}

// External declarations for global pointers
extern struct windrv_dll *tudor_adapter_dll, *tudor_engine_dll;
extern WINBIO_SENSOR_INTERFACE *tudor_sensor_adapter;
extern WINBIO_ENGINE_INTERFACE *tudor_engine_adapter;
extern WINBIO_STORAGE_INTERFACE *tudor_storage_adapter;

bool tudor_reg_handler(void *ctx, void *ctx_obj, const char *key_name, const char *val_name, bool is_write, void *buf, size_t *buf_size, enum winreg_val_type *val_type);
bool tudor_reg_enum_handler(void *ctx, const char *key_name, uint32_t index, char *name_buf, size_t name_buf_size);

#endif