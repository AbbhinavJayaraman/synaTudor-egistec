# synaTudor-egistec

Fork of [Popax21/synaTudor](https://github.com/Popax21/synaTudor), retargeted from
the Synaptics Tudor sensor family to the **EgisTec EH575** (USB `1c7a:0575`).

Goal: make the sensor usable on Linux via fprintd. The sensor is too low
resolution (103x52) for stock libfprint, so the Windows driver's own matching
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
- `[Driver] [SensorAdapter] ... >>> SensorAdapterStartCapture` / `<<< ... :
  ErrorCode [0x...]` - the adapters' own entry/exit traces. **This is the best
  diagnostic in the project**: both DLLs name every function they enter and
  print the HRESULT they return, so they narrate their own failures. Use them
  before reading the decompilation.

  They are gated on `HKLM\SOFTWARE\EgisSDKDBG` (`EnableBlock` = component
  mask, `DisplayFlag ` - trailing space is the DLL's own - > 0 to emit). `-t`
  makes `reg.c` serve those two values; see `FUN_180001000` / `FUN_1800010e0`.
  They were invisible for most of this project because `RegOpenKeyA` was a stub
  returning `ERROR_FILE_NOT_FOUND`, so the DLLs never got to read the key.

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

Before pushing, link-check. Note the meson files do **not** pass
`-Wl,--no-undefined` (an earlier version of this file claimed they do), and
`libtudor.so` is deliberately linked permissively so `bringup_nosensor` can
interpose libusb. So check explicitly:

```sh
ldd -r build/libtudor/libtudor.so   # should report no undefined symbols
```

Two real breaks were caught by link-checking.

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

## The engine picks its sensor class from ModelName

`FUN_18001ad60` in `EgisTouchFPEngine0575` searches the `ModelName` field of
`WINBIO_SENSOR_ATTRIBUTES` (at `+0x21c`) with `StrStrW` and instantiates a
different sensor class, each with its own image geometry, from
`FUN_18001c070(obj, width, height)`:

| ModelName contains | Class | Geometry |
| --- | --- | --- |
| `ET310` | `CET310Sensor` | 144 x 64 |
| `ET320` | `CET320Sensor` | 114 x 57 |
| `ET510` | `CET510Sensor` | **103 x 52** |
| none of them | - | 128 x 128 default |

The EH575 is an **ET510**, and `egis_ioctl_get_attributes` reports
`u"Fingerprint ET510"` - the string the Windows UMDF driver itself uses, at
offset `0x3ea10` of `EgisTouchFP0575.dll`, alongside `"EgisTec."` and the
firmware string `"FW575"`. Do not change it to a friendlier name: anything not
containing one of those three tokens silently selects the 128 x 128 default.

If `FUN_18001ad60` returns NULL the engine's `Attach` returns `0x8000ffff`
immediately, so this is on the critical path.

The 103 x 52 here matched the captures once they were read properly - see
"Frame geometry (settled)" below. Two independent sources agreeing on the same
geometry is the strongest evidence in this project so far.

## Current state

`tudor_open` succeeds on hardware. The CLI opens the device, lists records,
starts an enrollment, polls real frames off the sensor, and **detects a real
finger press** and hands that frame to the engine. The frontier is what the
engine does with it: `AcceptSampleData` has not yet returned success, so
nothing has been enrolled or matched end to end.

The last thing seen on hardware was `AcceptSampleData` rejecting the sample
with `E_INVALIDARG`, traced to the `WINBIO_CAPTURE_DATA` layout (thread 5).
That fix is in but **has not been confirmed on hardware** - it needs a press to
test, and it was reasoned from the decompilation. Re-run `e` with `-t` and read
`<<< EngineAdapterAcceptSampleData : hr = [0x%08X]` before assuming it works.

On hardware the CLI gets through:

- both DLLs relinked, relocated, through `DllMain`, both interfaces queried
- the sensor initialised (`egis_init_sensor`)
- all three `Attach` calls succeeding
- the engine reading its hardware key, deriving its paths, and running its own
  code, with both its `[Driver]` log and (under `-t`) the adapters' own
  `>>>`/`<<<` entry/exit traces visible
- `IOCTL 0x4427c0` logged as unimplemented - **and the run continues anyway**,
  see thread 1
- `QueryStatus` returning `WINBIO_SENSOR_READY`, so `tudor_open` returns true
- `q` (query records) working, and a clean shutdown: deactivate, pipeline
  cleanup, detach, both DLLs uninitialised
- `e` (enroll) reaching `CreateEnrollment`, then `StartCapture`, then the
  capture thread polling frames at 20/s, **detecting a real finger press**, and
  handing the frame to `FinishCapture` and `PushDataToEngine`

**`tools/bringup_nosensor` no longer diverges** through `tudor_open` - it now
reaches the same point as hardware and reports `tudor_open SUCCEEDED`. The
earlier note here said the engine's `Attach` failed against the simulated
sensor with `0x8000ffff`; that was the interface-size bug (`48e6aa8`), not the
simulated device. Both are worth running: the harness is still the one you can
put under gdb without sudo.

### Two earlier bring-up failures (both reproducible without hardware)

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

1. **`IOCTL 0x4427c0` - not a blocker after all.** The engine sends 208 bytes
   and expects 208 back. This is not a sensor command: the driver's handler
   (`FUN_18001c8c0` -> `FUN_180006f98` in `EgisTouchFP0575.c`) is pure
   computation with no USB in it - a key derivation over a `"U2Vj"`-seeded
   constant, an HMAC check, a 32-byte random, and an ECDH-shaped exchange. It is
   a mutual-authentication / session-key handshake between the engine adapter
   and the driver, entirely inside the Windows software stack.

   **Settled: it does not gate `Attach`.** Returning `STATUS_NOT_SUPPORTED`, the
   engine's `Attach` still succeeds, `tudor_open` completes, and enrollment gets
   as far as capture - on hardware and under `bringup_nosensor` alike. The older
   note here claimed `Attach` failed with `0x8000ffff` without it; that failure
   was really the interface-size bug, and this was misattributed. So it gates
   at most Egis' own secure-image feature, which matches the Python driver
   needing none of it.

   Leave it unimplemented until something actually demands it. If that happens,
   the two routes are still: port `FUN_180006f98` and its crypto helpers
   (`FUN_1800072b4` KDF, `FUN_180007b0c`/`FUN_1800074fc` MAC, `FUN_180001ec0`
   exchange, `FUN_180002310`) plus the constants at
   `DAT_18003f888`/`DAT_18003f8b0`; or map `EgisTouchFP0575.dll` as a PE image
   for this one function without running its WDF `DllMain` and call it directly.

2. **`HKLM\SYSTEM\CurrentControlSet\Services\EgisFP\FPParameters`.** The engine
   reads `Optimization` and `SmartLearn` from it. This key is *not* in the INF,
   so no values have been invented - `reg.c` returns "not found", which is what
   Windows would do unless something had set them. `Attach` and `tudor_open`
   now both succeed with them missing, so this is no longer a suspect for
   bring-up; revisit only if matching quality turns out poor, since
   `Optimization` and `SmartLearn` sound like they tune exactly that.

3. **Egis-private IOCTLs are unimplemented.** The driver's dispatch table
   (`EgisTouchFP0575.c` around the `0x442xxx` comparisons in the Ghidra dump)
   has named handlers for `0x442004`, `0x44200c`, `0x442010`, `0x442014`,
   `0x442018`, `0x44201c`, `0x442020`, `0x442024` and a `0x442400-0x4427fc`
   range. `0x4427c0` is now confirmed in use and is thread 1; `0x44200c` is
   referenced by the engine but has not been seen on the wire yet. These log a
   warning and return `STATUS_NOT_SUPPORTED` rather than failing silently, so
   the trace shows which are actually needed. Implement on demand, not
   speculatively.

4. **`CALIBRATE` (`0x44000c`) is a guess, and is not currently reached.** It
   returns success with a zeroed block, because the USB captures show no
   separate calibration exchange - the register programming in
   `egis_init_sensor` appears to be it.

   The adapter traces now show why this has never mattered.
   `SensorAdapterStartCapture` only calibrates when its first status check
   comes back not-ready, and on hardware it logs:

   ```
   SensorAdapterStartCapture : called SensorAdapterQueryStatus(1) = 3
   ```

   3 is `WINBIO_SENSOR_READY`, so the `IOCTL_BIOMETRIC_CALIBRATE` branch is
   skipped entirely. If the sensor ever reports `WINBIO_SENSOR_NOT_CALIBRATED`
   (5) the branch fires and this guess starts to matter - revisit it then.

5. **Capture works; the engine handoff is the frontier.** A real press is
   detected and the frame reaches the engine. `StartCapture`, the poll loop,
   `FinishCapture` and `PushDataToEngine` are all exercised on hardware. What
   has not been seen yet is `AcceptSampleData` returning success, an enrollment
   committing, or a match.

   Three struct/protocol fixes got it this far, all of the same kind - **this
   driver's IOCTL structs are not the documented WinBIO ones**, so check every
   field against the code that builds or reads it:

   - `0x440014` is a **two-call size negotiation** (the adapter probes with a
     0x18-byte buffer and reads the required size as a DWORD at offset 0).
   - `WINBIO_CAPTURE_PARAMETERS` is `0x20` bytes with **no `WinBioType`**.
   - `WINBIO_CAPTURE_DATA` has **no `Format`/`VendorFormat`**: the sample size
     is a ULONG at `+0x10` and the sample starts at `+0x14`. The old layout put
     the format pair at `+0x10`, so the adapter read `001b:0401` as a 67MB
     sample size and passed the GUID field as the sample - `E_INVALIDARG`.

   The adapter's own traces confirm the first two from its side, which is worth
   knowing as a way to check the third:

   ```
   SensorAdapterStartCapture : IOCTL_BIOMETRIC_GET_ATTRIBUTES ... bytesReturned = [1580]
   SensorAdapterStartCapture : IOCTL_BIOMETRIC_CAPTURE_DATA   ... bytesReturned = [4]
   SensorAdapterStartCapture : Call DeviceIoControl, GetLastError() = [997], result = [0]
   <<< SensorAdapterStartCapture : ErrorCode [0x00000000]
   ```

   1580 is `0x62c`, the asserted `WINBIO_SENSOR_ATTRIBUTES` size; the 4 bytes
   are the size probe answering with just the required length; 997 is
   `ERROR_IO_PENDING`, the real capture going async as it should.

   Finger detection is the frame-variance threshold ported from the Python
   driver, and it works: on hardware an **idle platen reads 40-600 and a press
   reads 1000-2500, against a threshold of 961**. That margin is thinner than
   it looks - idle frames are noisy, not flat, and an idle reading of 1017 has
   been seen trigger the clear-wait. If false triggers show up, this is why;
   the Windows driver uses a real finger-detect register path instead (the INF
   sets `FingerOnThreshold=6` / `FingerOnThresholdLoose=2`), still unmapped.

   Note the capture thread waits for the platen to be **clear** before it waits
   for a finger, so a press held from the start is not consumed. Both phases
   now announce themselves at `LOG_INFO`.

6. **CLI interactions to watch.** `cli/src/main.c` runs its own
   `libusb_handle_events` thread while `egis.c` uses synchronous
   `libusb_bulk_transfer`; libusb supports that via its event lock, but if
   transfers hang rather than fail, suspect it. The CLI also calls
   `drop_root_priv()` *before* `tudor_init()`, so `libusb_detach_kernel_driver`
   runs unprivileged.

7. **Five storage adapter entry points are still stubs.** `CreateDatabase`,
   `EraseDatabase`, `OpenDatabase`, `CloseDatabase` and `GetDatabaseSize` in
   `storage.c` log their own name and return a real WinBIO error. They used to
   share one stub that `abort()`ed, which told you a storage call had been hit
   but not which one, and killed the process before the trace could show what
   the engine did next - that is how `GetDataFormat` stayed hidden. It is
   implemented now (the INF declares the database format as the null GUID);
   the other five have not been reached yet. If one shows up in the log,
   implement it against `device->records_head`, which is the whole database.

## Reverse engineering material

Ghidra decompilation of all three DLLs is committed in the sibling repo
`python-egistec-eh575/ghidra_dumps/` (`.c` and `.h` per DLL). USB captures of
the Windows stack are in `python-egistec-eh575/wireshark/`. The INF is in
`egistecLighTuning0575/`. When you need to know what the Windows driver does
with an IOCTL, read the handler in the dump rather than guessing.

### Frame geometry (settled)

A frame is **one bulk IN transfer of 5356 bytes, which is exactly 103 x 52 with
no header, no trailer and no offset.**

USBPcap reports it as a 5120-byte read followed by a 236-byte read, but all 499
such pairs in `windows-helllo.pcapng` carry the **same IRP id** - one logical
transfer the host controller split - and 5120 + 236 = 5356. The pairing holds
for all 639 frames across the three captures, and each is preceded by the
`"EGIS" 64 14 ec` trigger that `egis.c` already sends.

Evidence, reproducible from the captures:

- width scan 95..115 over the highest-variance frames: **103** gives mean
  consecutive-row r = 0.874, versus 0.819 for 102 and 104
- offset scan 0..102 at width 103, fixed 50 rows for fairness: **offset 0 ranks
  first of 103**, and the whole spread is only 0.006, as expected once the frame
  divides evenly
- rendering confirms it: at 103 x 52 offset 0 the frames are coherent
  fingerprints; the old reading tears every frame with a displaced block

Independent confirmation: the engine's `CET510Sensor` allocates 103 x 52.

**Correcting the earlier note in this file:** it claimed 5120 bytes, offset 73,
103 x 49, and warned against "fixing" it. The width was right and the rest was
an artifact of only ever looking at the first chunk - `5120 mod 103 = 73`, so
dropping 73 bytes is just what it takes to make a truncated buffer divide
evenly. It was circular, not derived. If you find yourself picking an offset
that happens to equal `len mod width`, suspect exactly this.

## Conventions

- Match the surrounding style: `//` comments, `if(` with no space, 4 spaces.
- Explain *why* in comments, especially where behaviour is derived from the
  decompilation - cite the offset or function so it can be rechecked.
- Where a struct layout has to match what a Windows DLL expects, pin it with
  `_Static_assert` (see `WINBIO_SENSOR_ATTRIBUTES` in `winbio.h`, asserted
  against `+0x21c`, `+0x624`, `+0x628` and size `0x62c`).
- Do not weaken a shim into a stub that returns a plausible-looking constant.
  Four such stubs cost real debugging time here: a semaphore faked as an
  auto-reset event, threadpool waits that never fired, `BCryptFinishHash`
  zeroing its output so every digest was constant, and - the most expensive -
  `RegOpenKeyA` returning `ERROR_FILE_NOT_FOUND`, which silently disabled both
  adapters' entire trace output for most of the project. If something cannot be
  implemented, log loudly and return a real error.
- A stub that disables *diagnostics* is the worst kind, because it removes the
  evidence you would use to find it. When something is inexplicably silent,
  check what the DLL had to call to decide whether to speak.

## Related repos

- `python-egistec-eh575` - from-scratch Python driver (PyUSB + SIFT matching)
  and open-fprintd fork. **Currently the only thing that actually works**, so
  do not break it; it is also where the USB protocol and Ghidra dumps live.
- `egistecLighTuning0575` - Windows-side RE workbench: the DLLs, the INF, Wine
  traces, a native WBF loader harness.
