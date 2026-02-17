#include "internal.h"

// Emulated Registry for Egis Driver (Derived from egistouchfp0575.inf)
bool tudor_reg_handler(struct winreg_key *key, const char *name, uint32_t type, void *data, uint32_t *size) {

    // Debug: Log all requests to see if we missed anything
    if(type == REG_NONE) {
        log_debug("REG: Open Key -> '%s'", name);
    } else {
        log_debug("REG: Read Value -> '%s'", name);
    }

    // --- HANDLE KEY OPENS ---
    if(type == REG_NONE) {
        // The driver looks for its parameters in these locations:

        // 1. The main driver service key
        if(winstr_casecmp(name, "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\EgisTouchFP0575\\Parameters") == 0) return true;

        // 2. The WDF driver key
        if(winstr_casecmp(name, "HKEY_LOCAL_MACHINE\\Tudor\\Driver") == 0) return true;

        // 3. The Device key (often used for hardware-specifics)
        if(winstr_casecmp(name, "HKEY_LOCAL_MACHINE\\Tudor\\Device") == 0) return true;

        return false;
    }

    // --- HANDLE VALUE READS ---

    // [Configuration from egistouchfp0575.inf]

    if(winstr_casecmp(name, "SensorType") == 0) {
        if(type == REG_DWORD && *size >= 4) {
            *(uint32_t*)data = 1; // 1 = Touch Sensor (Not Swipe)
            *size = 4;
            return true;
        }
    }

    if(winstr_casecmp(name, "EnrollCount") == 0) {
        if(type == REG_DWORD && *size >= 4) {
            *(uint32_t*)data = 12; // Driver expects 12 touches, not 15
            *size = 4;
            return true;
        }
    }

    // Power Management (Crucial for initialization stability)
    if(winstr_casecmp(name, "SelectiveSuspendEnabled") == 0) {
        if(type == REG_DWORD && *size >= 4) {
            *(uint32_t*)data = 0; // Disable suspend for stability during RE
            *size = 4;
            return true;
        }
    }

    if(winstr_casecmp(name, "IdleTimer") == 0) {
        if(type == REG_DWORD && *size >= 4) {
            *(uint32_t*)data = 10000; // 10,000ms (10s) default
            *size = 4;
            return true;
        }
    }

    // Trace Flags (Enable driver-internal logging if possible)
    if(winstr_casecmp(name, "WppRecorder_TraceLevel") == 0) {
        if(type == REG_DWORD && *size >= 4) {
            *(uint32_t*)data = 4; // Verbose
            *size = 4;
            return true;
        }
    }

    log_warn("REG: MISSING VALUE -> '%s'", name);
    return false;
}
