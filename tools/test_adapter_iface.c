// Pins the fact the pipeline-lifecycle fix depends on: the two Egis adapters are
// different WinBIO generations, and the sensor adapter's published Size stops
// short of PipelineInit/PipelineCleanup/Activate/Deactivate. Calling those anyway
// reads past the end of its struct and jumps to whatever follows the vtable in
// .rdata, which is the SIGSEGV that showed up right after
// "Initializing pipeline interfaces...".
//
// Checked against the real DLLs at runtime rather than hardcoded, so it also
// catches winbio.h field order drifting out of sync with them.
#include <stdio.h>
#include <stddef.h>
#include <openssl/evp.h>
#include <tudor/log.h>
#include <tudor/tudor.h>
#include "tudor/winbio.h"

extern WINBIO_SENSOR_INTERFACE *tudor_sensor_adapter;
extern WINBIO_ENGINE_INTERFACE *tudor_engine_adapter;

static const struct tudor_pair_data *get_pdata(const char *n) { return NULL; }
static void set_pdata(const char *n, const struct tudor_pair_data *p) { }

static int fails = 0;
#define HAS(iface, fnc) \
    (offsetof(__typeof__(*(iface)), fnc) + sizeof((iface)->fnc) <= (size_t) (iface)->Size && (iface)->fnc != NULL)
#define EXPECT(what, cond) do { \
    int _c = !!(cond); if(!_c) fails++; \
    printf("%-58s %s\n", what, _c ? "PASS" : "FAIL"); } while(0)

int main(void) {
    LOG_LEVEL = LOG_ERROR;
    OpenSSL_add_all_algorithms();
    tudor_get_pdata_fnc = get_pdata;
    tudor_set_pdata_fnc = set_pdata;
    if(!tudor_init()) { printf("tudor_init failed\n"); return 2; }

    size_t ssize = (size_t) tudor_sensor_adapter->Size;
    size_t esize = (size_t) tudor_engine_adapter->Size;
    printf("sensor: version %u.%u size 0x%zx | engine: version %u.%u size 0x%zx\n\n",
        (unsigned) tudor_sensor_adapter->Version.MajorVersion,
        (unsigned) tudor_sensor_adapter->Version.MinorVersion, ssize,
        (unsigned) tudor_engine_adapter->Version.MajorVersion,
        (unsigned) tudor_engine_adapter->Version.MinorVersion, esize);

    EXPECT("sensor adapter is a 1.0 interface", tudor_sensor_adapter->Version.MajorVersion == 1);
    EXPECT("sensor adapter publishes 0x98 bytes", ssize == 0x98);
    EXPECT("sensor: last published method is ControlUnitPrivileged",
           offsetof(WINBIO_SENSOR_INTERFACE, ControlUnitPrivileged) + sizeof(void*) == ssize);

    // The four that must be skipped.
    EXPECT("sensor: PipelineInit NOT published",    !HAS(tudor_sensor_adapter, PipelineInit));
    EXPECT("sensor: PipelineCleanup NOT published", !HAS(tudor_sensor_adapter, PipelineCleanup));
    EXPECT("sensor: Activate NOT published",        !HAS(tudor_sensor_adapter, Activate));
    EXPECT("sensor: Deactivate NOT published",      !HAS(tudor_sensor_adapter, Deactivate));

    // Everything else device.c calls on the sensor must be present.
    EXPECT("sensor: Attach published",           HAS(tudor_sensor_adapter, Attach));
    EXPECT("sensor: Detach published",           HAS(tudor_sensor_adapter, Detach));
    EXPECT("sensor: ClearContext published",     HAS(tudor_sensor_adapter, ClearContext));
    EXPECT("sensor: QueryStatus published",      HAS(tudor_sensor_adapter, QueryStatus));
    EXPECT("sensor: Reset published",            HAS(tudor_sensor_adapter, Reset));
    EXPECT("sensor: StartCapture published",     HAS(tudor_sensor_adapter, StartCapture));
    EXPECT("sensor: FinishCapture published",    HAS(tudor_sensor_adapter, FinishCapture));
    EXPECT("sensor: ExportSensorData published", HAS(tudor_sensor_adapter, ExportSensorData));
    EXPECT("sensor: PushDataToEngine published", HAS(tudor_sensor_adapter, PushDataToEngine));
    EXPECT("sensor: Cancel published",           HAS(tudor_sensor_adapter, Cancel));

    // The engine is a 3.0 adapter and does have the lifecycle methods.
    EXPECT("engine adapter is a 3.0 interface", tudor_engine_adapter->Version.MajorVersion == 3);
    EXPECT("engine: PipelineInit published",    HAS(tudor_engine_adapter, PipelineInit));
    EXPECT("engine: Activate published",        HAS(tudor_engine_adapter, Activate));
    EXPECT("engine: AcceptSampleData published", HAS(tudor_engine_adapter, AcceptSampleData));
    EXPECT("engine: CreateEnrollment published", HAS(tudor_engine_adapter, CreateEnrollment));

    printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
