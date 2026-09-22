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

//[ContactSensor_AddReg] - the INF writes these under a subkey whose name is
//"EgisTouchFP0575" followed by a trailing space.
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

#define REG_KEY(k, v) { .key = (k), .values = (v), .num_values = sizeof(v) / sizeof((v)[0]) }

static const struct reg_key reg_keys[] = {
    REG_KEY("HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\EgisTouchFP0575\\Parameters", device_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Device", contact_sensor_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Device\\EgisTouchFP0575", contact_sensor_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Driver", device_values),
    REG_KEY("HKEY_LOCAL_MACHINE\\Tudor\\Driver\\WinBio\\Configurations\\0", winbio_config_values),
};

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
