# synaTudor-egistec

Fork of [Popax21/synaTudor](https://github.com/Popax21/synaTudor), retargeted from
the Synaptics Tudor sensor family to the **EgisTec EH575** (USB `1c7a:0575`).

Goal: make the sensor usable on Linux via fprintd. The sensor is too low
resolution (103x49) for stock libfprint, so the Windows driver's own matching
engine is relinked and run natively instead.

**Work on the `relinking` branch.** `relink` is the older, superseded approach -
do not build or develop on it.

## Architecture: what is and isn't relinked

The Windows stack ships three DLLs. Only two are loaded:

| DLL | Role | Relinked? |
| --- | --- | --- |
| `EgisTouchFPSensor0575.dll` | WinBIO sensor adapter | yes |
| `EgisTouchFPEngine0575.dll` | WinBIO engine adapter (enrollment + matching) | yes |
| `EgisTouchFP0575.dll` | UMDF driver | **no - replaced by `libtudor/src/tudor/egis.c`** |

The reasoning, in full, is in [RELINKING.md](RELINKING.md). The short version:
neither adapter imports a single WDF symbol - between them the whole device-I/O
surface is `CreateFile`, `DeviceIoControl`, `GetOverlappedResult` and
`CancelIoEx`. Only the UMDF driver needed WDF, and its only job was translating
biometric IOCTLs into `EGIS`-framed USB bulk transfers.

libtudor already hands the adapters a synthetic `SensorHandle`
(`pipeline->SensorHandle` in `tudor_open`, `libtudor/src/tudor/device.c`), so
every IOCTL they issue funnels through one native C callback. `tudor_devctrl`
dispatches to `egis_devctrl`, which talks to the sensor over libusb.

**Consequence: the entire `src/winapi/wdf/*` layer and `src/wdf.c` (~2540 lines)
are out of the build.** Do not re-add them or re-introduce the UMDF driver DLL
to `tudor_windrv_dlls[]` - that is the whole point of this branch. `DBGWDF`
still exists as a meson option because `src/stub.c` uses it; that is unrelated.

## Key files

- `libtudor/src/tudor/egis.c` / `egis.h` - EGIS USB transport and the native
  IOCTL handlers. This is the file that replaces the UMDF driver.
- `libtudor/src/tudor/device.c` - `tudor_devctrl` (the cut point), pipeline
  setup, enroll/verify/identify flow.
- `libtudor/src/tudor/driver.c` - which DLLs get loaded, WinBIO interface query.
- `libtudor/src/tudor/winbio.h` - WinBIO structs and the IOCTL constants.
- `libtudor/src/tudor/reg.c` - registry values, taken from the shipped INF.
- `libtudor/src/winapi/` - the Win32 shim. `shim.c` is the grab bag.
- `libtudor/drivers/*.dll` - the Windows DLLs, committed, embedded as blobs.

## Build and run

Sensor is local, so you can iterate directly - do not just reason about the
code, run it.

```sh
meson setup build && ninja -C build
sudo ninja -C build install
```

Bring-up is driven by the verbose trace. `tudor_cli <data store> [flags]`; each
`v` raises verbosity (default is `LOG_INFO`, so **two** are needed to reach
`LOG_VERBOSE`), `t` enables the driver's own WPP traces:

```sh
sudo /sbin/tudor/tudor_cli ~/tudor-store.bin -vvt
```

It prints a warning and waits for `y`. The lines that matter:

- `[DEVCTRL] -> in code 0x...` - every IOCTL the adapters issue, in order.
- `[WRN] Unimplemented EGIS IOCTL 0x...` - one we do not handle yet.
- `[Driver] ...` and `>>> CTouchSensor::EngineAdapter...` - the Egis DLLs'
  own logging. The engine is compiled with verbose entry/exit traces, so it
  narrates its own failures. Use them.

### Debugging without the sensor

`tudor_init()` and most of `tudor_open()` never touch the hardware, and
everything `egis.c` sends leaves through `libusb_bulk_transfer`. So
`tools/bringup_nosensor.c` stands the sensor in by defining the libusb entry
points itself - symbols in the executable win over `libusb.so` for lookups from
`libtudor.so` - and drives the real `tudor_init()` and `tudor_open()` against it:

```sh
./build/tools/bringup_nosensor        # -u also dumps the simulated USB traffic
gdb --args ./build/tools/bringup_nosensor
```

Use this first for anything in DLL bring-up, the Win32 shim or the registry. It
runs unprivileged, so gdb works normally - no sudo, and none of the
`RLIMIT_CORE` trouble below. It is how the `wsprintfW` crash and the
`RegEnumKeyW` hang were found. It exits 3 on a clean `tudor_open` failure.

`meson test` runs the shim unit tests in `tools/` (`test_format`,
`test_convert`). Both deliberately call in through the Microsoft vararg
convention, because calling them the SysV way would not exercise the bug the
formatter exists to fix.

Before pushing, link-check: meson passes `-Wl,--no-undefined`, and a permissive
link will hide dangling references. Two real breaks were caught that way.

Target platform is Arch/CachyOS. Deps: `base-devel meson ninja pkgconf libusb
openssl libcap libseccomp glib2 glib2-devel dbus json-glib libgusb
systemd-libs`, plus `libfprint-tod-git` from the AUR (it replaces `libfprint`).

### Passwordless sudo for iteration

A scoped `NOPASSWD` drop-in at `/etc/sudoers.d/synatudor-dev` covers
`ninja -C <repo>/build install` and `/sbin/tudor/tudor_cli *`, so both can run
without a password prompt during a session. Two gotchas hit already:

- sudoers matches the **literal argument string**, not a resolved path - the
  `ninja -C` rule only fires when invoked with the full absolute build path
  (`-C /home/.../build`), not a relative `-C build` even from the right cwd.
- sudo zeroes `RLIMIT_CORE` for the child process, so a crash under `sudo
  tudor_cli` produces **no coredump** even with `ulimit -c unlimited` in the
  parent shell (`systemd-coredump` logs "Resource limits disable core
  dumping"). Getting a real backtrace under the CLI needs `sudo gdb --args
  /sbin/tudor/tudor_cli ...`, which is not in the NOPASSWD rule (ask the user
  before adding it, or ask them to run the gdb invocation directly). Usually you
  do not have to: if the fault is anywhere in DLL bring-up, the shim or the
  registry, reproduce it under `tools/bringup_nosensor` instead, which needs no
  sudo at all.

## The two adapters are different WinBIO generations

This matters everywhere, so it is worth knowing up front. Each adapter publishes
how much of the interface struct it implements in its own `Size` field:

| Adapter | Version | `Size` | Methods | Last published method |
| --- | --- | --- | --- | --- |
| `EgisTouchFPSensor0575` | 1.0 | `0x98` | 15 | `ControlUnitPrivileged` |
| `EgisTouchFPEngine0575` | 3.0 | `0x168` | 41 | - |

So the **sensor adapter has no `PipelineInit`, `PipelineCleanup`, `Activate` or
`Deactivate`.** Reading those fields walks past the end of its struct into
whatever follows the vtable in `.rdata`; calling the result is a jump to a
garbage address. Windows does not call them on a 1.0 adapter either, so
`WINBIO_CALL_PIPELINE_OPT` (internal.h) checks the published `Size` first and
skips. `tudor_init` logs both interfaces' version and size on every run, and
`tools/test_adapter_iface` pins the layout against the shipped DLLs.

Everything else `device.c` calls on the sensor adapter is inside its first 15.
If you add a new sensor-adapter call, check it against that table.

## Current state

Not working end to end yet, but there is no crash or hang left in the bring-up
path, and the remaining failure is a single located gap.

On hardware the CLI gets through:

- both DLLs relinked, relocated, through `DllMain`, both interfaces queried
- the sensor initialised (`egis_init_sensor`)
- **all three `Attach` calls succeeding**
- the engine reading its hardware key, deriving its paths, and running its own
  code, with its `[Driver]` log visible
- `IOCTL 0x4427c0` logged as unimplemented - see thread 1
- then, before the interface-size fix, a SIGSEGV in `sensor_adapter->PipelineInit`

Note that `tools/bringup_nosensor` diverges here: against the simulated sensor
the **engine's** `Attach` fails with `0x8000ffff`, so it stops before
`PipelineInit`. On real hardware `Attach` succeeds. Something in the simulated
device's replies is wrong enough for the engine to reject it but not wrong enough
to matter earlier - so treat bringup_nosensor as authoritative for crashes and
hangs, and the hardware run as authoritative for whether a step actually
succeeds. Narrowing that divergence would make the harness much more useful.

### The two failures fixed before that (both reproducible without hardware)

**A SIGSEGV inside glibc, which looked like it happened during DLL load.** It
was `wsprintfW`: a variadic `__winfnc` is `ms_abi`, so its arguments arrive in
rcx/rdx/r8/r9 per the Microsoft convention, and building a plain `va_list` in
such a function and passing it to `vsnprintf` makes glibc read a SysV register
save area that the `ms_abi` prologue never wrote. Every argument was stack
garbage. The sensor adapter formats its instance ID into
`"SYSTEM\CurrentControlSet\Enum\%s\Device Parameters"`, so `%s` got a junk
pointer and glibc walked it.

Two things about how this presented are worth remembering:

- It was latent from `19c9156` and only became reachable in `87015fb`, which
  implemented `IOCTL 0x220000` - before that the adapter never got an instance
  ID, so it never formatted the path.
- The crash appeared to be in DLL load because `log_info`/`log_debug` go to
  **stdout** while `[SPY]` goes to stderr. stdout was block-buffered, so the
  trace explaining the crash died in the buffer and the last thing on screen
  was unrelated stderr output. `tudor_init` now line-buffers stdout, and the
  `[DEVCTRL]` line is built and written in one piece instead of being printed
  in fragments around the work it describes.

**An infinite loop in the engine, on a shim that returned a "safe" error.**
`RegEnumKeyW` was a stub returning `ERROR_NO_MORE_ITEMS`. But the engine's
`FUN_18001a910` is:

```c
while ((LVar2 = RegEnumKeyW(hkey, i, name, ...), LVar2 != 0 ||
       (StrCmpNIW(name, L"Egis", 4) != 0))) i++;
```

`ERROR_NO_MORE_ITEMS` does not end that loop - nothing does except a successful
enumeration returning a name starting with `Egis`, because on Windows the key is
always there. The registry layer now supports subkey enumeration
(`winreg_enum_subkey`, `tudor_reg_enum_handler`) and reports `EgisTouchFP0575`
under the device's hardware key, which is where the INF's `HKR,EgisTouchFP0575\ `
values actually live.

Alongside those, three more shims were wrong in ways that produced plausible
but false results rather than errors, and all three were on this path:

- `WideCharToMultiByte`/`MultiByteToWideChar` converted **one character** and
  returned. The engine narrows its hardware-key path before `RegOpenKeyExA`, so
  the registry saw `Key='HKEY_LOCAL_MACHINE\S'` plus uninitialised stack.
- `StrCmpNIW` returned 0 unconditionally, i.e. "equal" - so the loop above would
  have accepted whatever subkey name came back first.
- `BCryptOpenAlgorithmProvider("RNG")` failed, because the algorithm table had
  no RNG entry. `BCryptGenRandom` already ignores the handle and uses
  `RAND_bytes`, so the provider only had to exist.

## Open threads

1. **`IOCTL 0x4427c0` - the current blocker.** The engine sends 208 bytes and
   expects 208 back. This is not a sensor command: the driver's handler
   (`FUN_18001c8c0` -> `FUN_180006f98` in `EgisTouchFP0575.c`) is pure
   computation with no USB in it - a key derivation over a `"U2Vj"`-seeded
   constant, an HMAC check, a 32-byte random, and an ECDH-shaped exchange. It is
   a **mutual-authentication / session-key handshake between the engine adapter
   and the driver**, entirely inside the Windows software stack.

   Returning success with a zeroed 208-byte reply was tried and does not work -
   the engine verifies the response cryptographically, and `Attach` still fails
   with `0x8000ffff`. So this has to be implemented for real. Two routes:

   - Port `FUN_180006f98` and the crypto helpers it calls
     (`FUN_1800072b4` KDF, `FUN_180007b0c`/`FUN_1800074fc` MAC,
     `FUN_180001ec0` exchange, `FUN_180002310`) plus the embedded constants at
     `DAT_18003f888`/`DAT_18003f8b0`. Self-contained, but it is a protocol
     reimplementation.
   - Or map `EgisTouchFP0575.dll` as a PE image for this one function without
     running its WDF `DllMain`, and call it directly. Cheaper if its state
     dependencies are only the relocated statics - `FUN_180006f98` reads a
     context through `param_1 + 8` that holds a 32-byte secret established
     earlier, so check where that comes from first.

   Worth settling before either: whether the engine can be attached at all
   without this handshake, or whether it gates only Egis' own secure-image
   feature. The Python driver in the sibling repo needs none of it.

2. **`HKLM\SYSTEM\CurrentControlSet\Services\EgisFP\FPParameters`.** The engine
   reads `Optimization` and `SmartLearn` from it. This key is *not* in the INF,
   so no values have been invented - `reg.c` returns "not found", which is what
   Windows would do unless something had set them. If `Attach` still fails,
   guessing values here is the next experiment.

3. **Egis-private IOCTLs are unimplemented.** The driver's dispatch table
   (`EgisTouchFP0575.c` around the `0x442xxx` comparisons in the Ghidra dump)
   has named handlers for `0x442004`, `0x44200c`, `0x442010`, `0x442014`,
   `0x442018`, `0x44201c`, `0x442020`, `0x442024` and a `0x442400-0x4427fc`
   range. `0x4427c0` is now confirmed in use and is thread 1; `0x44200c` is
   referenced by the engine but has not been seen on the wire yet. These log a
   warning and return `STATUS_NOT_SUPPORTED` rather than failing silently, so
   the trace shows which are actually needed. Implement on demand, not
   speculatively.

4. **`CALIBRATE` (`0x44000c`) is a guess.** It returns success with a zeroed
   block, because the USB captures show no separate calibration exchange - the
   register programming in `egis_init_sensor` appears to be it. If the adapter
   reports `WINBIO_SENSOR_NOT_CALIBRATED` (the sensor DLL has a string for
   exactly that), revisit this first.

5. **Capture has never run.** `IOCTL_BIOMETRIC_CAPTURE_DATA` and the worker
   thread in `egis.c` are written but untested. Finger detection is a frame
   variance threshold ported from the Python driver; the Windows driver has a
   real finger-detect register path instead (the INF sets `FingerOnThreshold=6`
   / `FingerOnThresholdLoose=2`), which has not been mapped.

6. **CLI interactions to watch.** `cli/src/main.c` runs its own
   `libusb_handle_events` thread while `egis.c` uses synchronous
   `libusb_bulk_transfer`; libusb supports that via its event lock, but if
   transfers hang rather than fail, suspect it. The CLI also calls
   `drop_root_priv()` *before* `tudor_init()`, so `libusb_detach_kernel_driver`
   runs unprivileged.

## Reverse engineering material

Ghidra decompilation of all three DLLs is committed in the sibling repo
`python-egistec-eh575/ghidra_dumps/` (`.c` and `.h` per DLL). USB captures of
the Windows stack are in `python-egistec-eh575/wireshark/`. The INF is in
`egistecLighTuning0575/`. When you need to know what the Windows driver does
with an IOCTL, read the handler in the dump rather than guessing.

Frame geometry facts, derived from those captures: a frame transfer is exactly
**5120 bytes**; autocorrelation peaks at lag **103** (r = 0.94, harmonics at
206/309/412); row alignment is best at byte offset **73**. So the usable image
is 103 x 49 starting at +73. Do not "fix" these to 103 x 50.

## Conventions

- Match the surrounding style: `//` comments, `if(` with no space, 4 spaces.
- Explain *why* in comments, especially where behaviour is derived from the
  decompilation - cite the offset or function so it can be rechecked.
- Where a struct layout has to match what a Windows DLL expects, pin it with
  `_Static_assert` (see `WINBIO_SENSOR_ATTRIBUTES` in `winbio.h`, asserted
  against `+0x21c`, `+0x624`, `+0x628` and size `0x62c`).
- Do not weaken a shim into a stub that returns a plausible-looking constant.
  Three such stubs cost real debugging time here: a semaphore faked as an
  auto-reset event, threadpool waits that never fired, and `BCryptFinishHash`
  zeroing its output so every digest was constant. If something cannot be
  implemented, log loudly and return a real error.

## Related repos

- `python-egistec-eh575` - from-scratch Python driver (PyUSB + SIFT matching)
  and open-fprintd fork. **Currently the only thing that actually works**, so
  do not break it; it is also where the USB protocol and Ghidra dumps live.
- `egistecLighTuning0575` - Windows-side RE workbench: the DLLs, the INF, Wine
  traces, a native WBF loader harness.
