#include "internal.h"
#include <strings.h>

// Placeholder pointers to match existing linker expectations (if referenced elsewhere)
const struct tudor_pair_data *(*tudor_get_pdata_fnc)(const char *name);
void (*tudor_set_pdata_fnc)(const char *name, const struct tudor_pair_data *pdata);

//Every value below is taken from EgisTouchFP0575.inf as shipped, rather than
//guessed. The INF sections are named in the comments so they can be checked
//against the original.

struct reg_dword {
    const char *name;
    uint32_t value;
};

struct reg_key {
    const char *key;
    const struct reg_dword *values;
    size_t num_values;
};

//[DriverPlugInAddReg] - HKR,WinBio\Configurations\0
static const struct reg_dword winbio_config_values[] = {
    { "SensorMode", 1 },     //Basic
    { "SystemSensor", 1 },   //Usable for UAC / Winlogon
};

//[ContactSensor_AddReg] - "HKR,EgisTouchFP0575\ ,..." in the INF. HKR is the
//device's hardware key, so on Windows these live under
//  HKLM\SYSTEM\CurrentControlSet\Enum\<instance id>\Device Parameters\EgisTouchFP0575
//which is also the path EgisTouchFP0575.dll builds for itself (see the format
//string at EgisTouchFP0575.c:9915 in the Ghidra dump).
//These are the sensor tuning parameters the driver reads at startup.
static const struct reg_dword contact_sensor_values[] = {
    { "RemoteWakeupEnable", 0x00000001 },
    { "FetchImageMode", 0x80000003 },
    { "FingerOnMode", 0x00000000 },
    { "ResumingDetectModeParameterTuningEnabled", 0 },
    { "FingerOnThresholdLoose", 2 },
    { "FingerOnThreshold", 6 },
};

//[Biometric_Device_AddReg] - power management and device characteristics.
static const struct reg_dword device_values[] = {
    { "DeviceCharacteristics", 0x0100 },
    { "Exclusive", 1 },
    { "DeviceIdleEnabled", 1 },
    { "DefaultIdleTimeout", 10000 },
    { "WinUsbPowerPolicyOwnershipDisabled", 1 },
    //[Biometric_Device_AddReg] WudfPowerPolicySettings
    { "WdfDefaultIdleInWorkingState", 1 },
    { "WdfDefaultWakeFromSleepState", 0 },
    { "WdfDirectedPowerTransitionEnable", 1 },
    //Selective suspend stays off - the host owns power for this sensor.
    { "SelectiveSuspendEnabled", 0 },
    { "IdleTimer", 10000 },
    //WPP tracing verbosity. Not from the INF; 4 = verbose, which is what makes
    //the driver's own trace messages show up in the libtudor log.
    { "WppRecorder_TraceLevel", 4 },
};

//Both adapters carry a full set of entry/exit traces - ">>> SensorAdapterAttach",
//"<<< EngineAdapterAcceptSampleData : hr = [0x%08X], Purpose = %d" and so on -
//and emit them through OutputDebugStringA, which libtudor already prints as
//[Driver]. They are gated, and the gate is this registry key. From
//FUN_180001000 in EgisTouchFPSensor0575 (the same code is in the engine):
//
//    RegOpenKeyA(HKLM, "SOFTWARE\\EgisSDKDBG", &k);
//    RegQueryValueExA(k, "EnableBlock",  NULL, &t, &DAT_180018664, &len);
//    RegQueryValueExA(k, "DisplayFlag ", NULL, &t, &DAT_180018668, &len);
//
//and the trace function itself (FUN_1800010e0) then does:
//
//    if((DAT_180018664 & component) != 0 && fmt != NULL) { ...
//        if(0 < DAT_180018668) OutputDebugStringA(buf);
//        if(1 < DAT_180018668) ...                      // also writes a file
//
//where component is 1 for EngineAdapter, 2 for SensorAdapter and anything else
//for WBF. RegOpenKeyExA in the shim always succeeds, so the key "existing" was
//never the problem - the two values simply read as missing, leaving the mask at
//zero and every trace suppressed.
//
//Note the **trailing space** in "DisplayFlag ". That is how the DLL spells it,
//so that is what has to match; without it the value is never found.
//
//DisplayFlag is 1, not 2: at 2 and above the DLL also opens a log file of its
//own, which is not something we want it doing.
#define EGIS_SDK_DBG_KEY "HKEY_LOCAL_MACHINE\\SOFTWARE\\EgisSDKDBG"

static const struct reg_dword egis_sdk_dbg_values[] = {
    { "EnableBlock", 0xffffffff },  //every component
    { "DisplayFlag ", 1 },          //OutputDebugString only - see above
};

#define REG_KEY(k, v) { .key = (k), .values = (v), .num_values = sizeof(v) / sizeof((v)[0]) }

static const struct reg_key reg_keys[] = {
    REG_KEY("HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\EgisTouchFP0575\\Parameters", device_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Device", contact_sensor_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Device\\EgisTouchFP0575", contact_sensor_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Driver", device_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Driver\\WinBio\\Configurations\\0", winbio_config_values),
};

//The device hardware key's name contains the USB instance ID, which is only
//known at runtime, so these two cannot be matched literally like the table
//above. Both are "HKLM\SYSTEM\CurrentControlSet\Enum\" + <instance> + suffix.
#define ENUM_KEY_PREFIX "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\"
#define DEVICE_PARAMS_SUFFIX "\\Device Parameters"

//The subkey the INF creates under Device Parameters. The engine only checks the
//first four characters against "Egis" (FUN_18001a910), but the driver's own
//format string spells it out in full, so use that.
#define EGIS_PARAMS_SUBKEY "EgisTouchFP0575"

static bool str_has_prefix(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return strncasecmp(s, prefix, n) == 0;
}

static bool str_has_suffix(const char *s, const char *suffix) {
    size_t sl = strlen(s), fl = strlen(suffix);
    return sl >= fl && strcasecmp(s + sl - fl, suffix) == 0;
}

//True for the device's own hardware key.
static bool is_device_params_key(const char *key_name) {
    return str_has_prefix(key_name, ENUM_KEY_PREFIX) && str_has_suffix(key_name, DEVICE_PARAMS_SUFFIX);
}

//True for the EgisTouchFP0575 subkey underneath it.
static bool is_device_params_subkey(const char *key_name) {
    return str_has_prefix(key_name, ENUM_KEY_PREFIX) &&
           str_has_suffix(key_name, DEVICE_PARAMS_SUFFIX "\\" EGIS_PARAMS_SUBKEY);
}

bool tudor_reg_enum_handler(void *ctx, const char *key_name, uint32_t index, char *name_buf, size_t name_buf_size) {
    if(!key_name || !name_buf) return false;

    //Only the hardware key has subkeys worth reporting. Everything else
    //enumerates empty, which is what a real key with no children does.
    if(is_device_params_key(key_name) && index == 0) {
        if(strlen(EGIS_PARAMS_SUBKEY) + 1 > name_buf_size) return false;
        strcpy(name_buf, EGIS_PARAMS_SUBKEY);
        return true;
    }

    return false;
}

static bool put_dword(uint32_t value, void *buf, size_t *buf_size, enum winreg_val_type *val_type) {
    //A NULL buffer is a size query; a buffer that is too small is an error.
    if(buf) {
        if(*buf_size < sizeof(uint32_t)) return false;
        *((uint32_t*) buf) = value;
    }
    *buf_size = sizeof(uint32_t);
    if(val_type) *val_type = WINREG_DWORD;
    return true;
}

bool tudor_reg_handler(void *ctx, void *ctx_obj, const char *key_name, const char *val_name, bool is_write, void *buf, size_t *buf_size, enum winreg_val_type *val_type) {
    //Only reads are served; the adapters never need to persist anything here.
    if(is_write) return false;
    if(!buf_size || !key_name || !val_name) return false;

    log_debug("REG: Read Key='%s' Val='%s'", key_name, val_name);

    //Only answer the adapters' debug gate when traces were actually asked for
    //(-t), since turning it on makes both of them narrate every call.
    if(strcasecmp(key_name, EGIS_SDK_DBG_KEY) == 0) {
        if(!tudor_log_traces) return false;
        for(size_t j = 0; j < sizeof(egis_sdk_dbg_values) / sizeof(egis_sdk_dbg_values[0]); j++) {
            if(strcmp(val_name, egis_sdk_dbg_values[j].name) != 0) continue;
            return put_dword(egis_sdk_dbg_values[j].value, buf, buf_size, val_type);
        }
        return false;
    }

    //The hardware-key path is matched by shape, not by name - see above.
    const struct reg_dword *pattern_values = NULL;
    size_t num_pattern_values = 0;
    if(is_device_params_subkey(key_name)) {
        pattern_values = contact_sensor_values;
        num_pattern_values = sizeof(contact_sensor_values) / sizeof(contact_sensor_values[0]);
    } else if(is_device_params_key(key_name)) {
        //[Biometric_Device_AddReg] writes its values directly to HKR.
        pattern_values = device_values;
        num_pattern_values = sizeof(device_values) / sizeof(device_values[0]);
    }
    for(size_t j = 0; j < num_pattern_values; j++) {
        if(strcasecmp(val_name, pattern_values[j].name) != 0) continue;
        return put_dword(pattern_values[j].value, buf, buf_size, val_type);
    }

    for(size_t i = 0; i < sizeof(reg_keys) / sizeof(reg_keys[0]); i++) {
        const struct reg_key *key = &reg_keys[i];
        if(strcasecmp(key_name, key->key) != 0) continue;

        for(size_t j = 0; j < key->num_values; j++) {
            if(strcasecmp(val_name, key->values[j].name) != 0) continue;
            return put_dword(key->values[j].value, buf, buf_size, val_type);
        }
    }

    //The string values the INF also sets - SensorAdapterBinary,
    //EngineAdapterBinary, StorageAdapterBinary, DatabaseId - are deliberately
    //not served: libtudor resolves the adapters itself and supplies its own
    //storage adapter, so nothing should be asking for them. If this fires,
    //the log says which value to add.
    log_verbose("REG: no value for '%s' under '%s'", val_name, key_name);
    return false;
}
