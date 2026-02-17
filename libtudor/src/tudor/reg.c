#include "internal.h"
#include <strings.h> // Required for strcasecmp

// Placeholder pointers to match existing linker expectations (if referenced elsewhere)
const struct tudor_pair_data *(*tudor_get_pdata_fnc)(const char *name);
void (*tudor_set_pdata_fnc)(const char *name, const struct tudor_pair_data *pdata);

// The CORRECT signature matching your internal.h
bool tudor_reg_handler(void *ctx, void *ctx_obj, const char *key_name, const char *val_name, bool is_write, void *buf, size_t *buf_size, enum winreg_val_type *val_type) {

    // We only handle reads for configuration
    if (is_write) return false;
    if (!buf_size) return false;

    // Debug logging to help you verify the driver is asking for keys
    log_debug("REG: Read Key='%s' Val='%s'", key_name, val_name);

    // --- Egis Driver Parameters ---
    // Key: HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\EgisTouchFP0575\Parameters
    if (strcasecmp(key_name, "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\EgisTouchFP0575\\Parameters") == 0) {

        // 1. SensorType = 1 (Touch)
        if (strcasecmp(val_name, "SensorType") == 0) {
            if (buf && *buf_size >= 4) {
                *((uint32_t*)buf) = 1;
            } else if (buf) return false; // Buffer too small
            *buf_size = 4;
            if (val_type) *val_type = WINREG_DWORD;
            return true;
        }

        // 2. EnrollCount = 12
        if (strcasecmp(val_name, "EnrollCount") == 0) {
            if (buf && *buf_size >= 4) {
                *((uint32_t*)buf) = 12;
            } else if (buf) return false;
            *buf_size = 4;
            if (val_type) *val_type = WINREG_DWORD;
            return true;
        }

        // 3. Power Management (Disable Suspend = 0)
        if (strcasecmp(val_name, "SelectiveSuspendEnabled") == 0) {
            if (buf && *buf_size >= 4) {
                *((uint32_t*)buf) = 0;
            } else if (buf) return false;
            *buf_size = 4;
            if (val_type) *val_type = WINREG_DWORD;
            return true;
        }

        // 4. IdleTimer = 10000ms
        if (strcasecmp(val_name, "IdleTimer") == 0) {
            if (buf && *buf_size >= 4) {
                *((uint32_t*)buf) = 10000;
            } else if (buf) return false;
            *buf_size = 4;
            if (val_type) *val_type = WINREG_DWORD;
            return true;
        }

        // 5. Trace Level (Verbose = 4)
        if (strcasecmp(val_name, "WppRecorder_TraceLevel") == 0) {
            if (buf && *buf_size >= 4) {
                *((uint32_t*)buf) = 4;
            } else if (buf) return false;
            *buf_size = 4;
            if (val_type) *val_type = WINREG_DWORD;
            return true;
        }

        return false; // Unknown value in this key
    }

    // --- Tudor Driver Configuration ---
    if (strcasecmp(key_name, "HKEY_LOCAL_MACHINE\\Tudor\\Driver") == 0) {
        // If the driver asks for generic Tudor params, add them here.
        // For now, return false implies "Value not found", which is safe.
        return false;
    }

    return false;
}
