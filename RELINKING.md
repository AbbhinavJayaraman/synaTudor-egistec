# Relinking approach for the EgisTec EH575

This branch changes *where* the Windows stack is cut. The previous approach
relinked all three Egis DLLs and tried to make the WDF/UMDF emulation layer
good enough to host the UMDF driver. This branch drops the UMDF driver and
services its IOCTLs natively instead.

## Why

The Egis stack ships three DLLs:

| DLL | Role | Needs WDF? |
| --- | --- | --- |
| `EgisTouchFPSensor0575.dll` | WinBIO sensor adapter | no |
| `EgisTouchFPEngine0575.dll` | WinBIO engine adapter (enrollment + matching) | no |
| `EgisTouchFP0575.dll` | UMDF driver | **yes** |

Import tables tell the story. The sensor adapter imports 76 symbols, and its
entire device-I/O surface is:

```
CreateFileA/W, DeviceIoControl, WriteFile, GetOverlappedResult,
CancelIoEx, CreateEventA, ResetEvent
+ RegOpenKeyA, RegQueryValueExA, RegCloseKey
```

Everything else is MSVC CRT boilerplate. The engine adapter is the same shape.
Neither imports a single WDF symbol. Only the UMDF driver does — and its only
job is turning biometric IOCTLs into `EGIS`-framed USB bulk transfers.

`libtudor` already hands the adapters a synthetic `SensorHandle`
(`tudor_open` in `src/tudor/device.c`), and every `DeviceIoControl` the
adapters issue lands in one native C callback. So the UMDF driver can be
replaced with an implementation of that callback.

## The IOCTL surface

`FILE_DEVICE_BIOMETRIC` is `0x44`, so the codes are `0x44xxxx`. Taken from the
dispatch table in the decompiled driver:

| Code | Meaning | Observed shape |
| --- | --- | --- |
| `0x440004` | `GET_ATTRIBUTES` | 0 in, `0x62c` out |
| `0x440008` | `RESET` | 0 in, 8 out |
| `0x44000c` | `CALIBRATE` | 0 in, `0x10` out |
| `0x440010` | `GET_SENSOR_STATUS` | 0 in, `0x14` out |
| `0x440014` | `CAPTURE_DATA` | `0x20` in, image out |
| `0x44001c` | `GET_INDICATOR` | 0 in, `0xc` out |
| `0x440020` | `SET_INDICATOR` | 0 in, `0xc` out |
| `0x440024` | firmware / control | 8 in, 8 out |

The `WINBIO_SENSOR_ATTRIBUTES` layout in `src/tudor/winbio.h` is checked
against the decompilation with `_Static_assert`: the Windows adapter allocates
`0x62c` bytes, reads `SupportedFormatEntries` at `+0x624`, walks the format
array at `+0x628`, and reads a model-name string at `+0x21c`. All four match.

The adapter advertises `0x0401001b` as its supported format — owner `0x001b`,
type `0x0401` — i.e. **ANSI INCITS 381 finger image**. Images go in, the engine
does the matching.

## What changed

- **New** `src/tudor/egis.c` / `egis.h` — USB transport plus the IOCTL
  handlers. The register programming is transcribed from the working Python
  driver in `python-egistec-eh575`, which recovered it from USB captures.
- `src/tudor/device.c` — `tudor_devctrl` dispatches to `egis_devctrl` instead
  of `winwdf_devctrl_file`; open/close bring the sensor up directly.
- `src/tudor/driver.c` — `EgisTouchFP0575.dll` is no longer embedded or
  loaded, and the `FxDriverEntryUm` / WDF driver bring-up is gone.
- `libtudor/meson.build` — the `src/winapi/wdf/*` layer and `src/wdf.c`
  (~2540 lines) are out of the build, as is the UMDF driver blob.
- `src/tudor/reg.c` — rewritten as a table of the values actually present in
  `EgisTouchFP0575.inf`, replacing invented ones.
- `src/winapi/shim.c` — three stubs that were actively wrong are now real
  implementations (see below).
- `src/winapi/io.c` — the experimental `0x44000c` trap is removed.

After dropping the WDF layer, the two remaining DLLs need 135 unique imports
and the build registers 245. Nothing is missing.

## Frame geometry

A frame transfer is exactly **5120 bytes**. Autocorrelation over the 639 image
frames in `python-egistec-eh575/wireshark` peaks at lag **103** (r = 0.94,
harmonics at 206/309/412), and row alignment is best at byte offset **73**
(row-to-row r = 0.9802). So the usable image is 103 × 49 starting at +73; the
leading 73 bytes are a partial row.

## Shim fixes

Three stubs were not merely inert, they were wrong:

- `CreateSemaphoreExW` was mapped onto an auto-reset Event, which collapses a
  counting semaphore into a binary flag. N releases before a wait satisfied
  one waiter and dropped the rest. Now a real counting semaphore.
- `CreateThreadpoolWait` / `CreateThreadpoolTimer` returned `NULL` with no-op
  setters, so any completion registered through them never fired. Each object
  now gets a real worker thread.
- `wsprintfA` / `wsprintfW` returned 0 and wrote nothing, so every string the
  driver formatted came back empty.

## Status

Not yet run against hardware. The documented WinBIO IOCTL subset above is
implemented; the Egis-private `0x442xxx` codes are not — they log a warning
and return `STATUS_NOT_SUPPORTED` rather than failing silently, so the
`[DEVCTRL]` trace at verbose log level shows exactly which ones the adapters
actually require. Each has a named handler in the decompiled driver to work
from.

Expect the first bring-up to be driven by that trace: run at verbose, see the
call order the adapters want, and fill in from there.
