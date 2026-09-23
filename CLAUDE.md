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
  dumping"). Getting a real backtrace needs `sudo gdb --args
  /sbin/tudor/tudor_cli ...`, which is not yet in the NOPASSWD rule (ask the
  user before adding it, or ask them to run the gdb invocation directly).

## Current state (as of b192358)

**Blocked before the previously-recorded failure can even be re-tested.** A
fresh hardware run (build from `b192358`, clean build, no code changes)
segfaults:

- `sudo ninja -C build install` then `sudo /sbin/tudor/tudor_cli
  ~/tudor-store.bin -vvt` crashes with SIGSEGV. The trace never reaches a
  `[DEVCTRL]` line or a `>>> CTouchSensor::` line, so this is happening
  earlier than DLL `Attach` - somewhere during/after `DllMain` and Win32 API
  shim resolution (the last output is repeated `[SPY] GetProcAddress`
  lines for the second DLL's imports).
- Kernel log: `segfault at 2 ip 00007eff5e3b4b9d ... in libc.so.6[1b4b9d,...]`
  - address `0x2` is a near-null pointer, small enough to suggest a
    `wcs*`-family (wide-char, 2-byte unit) function called on a bad/null
    pointer, but this is a guess, not confirmed.
  - **No backtrace obtained yet** - see the sudo/`RLIMIT_CORE` gotcha above.
    Next step is `sudo gdb --args /sbin/tudor/tudor_cli ~/tudor-store.bin -vvt`
    (needs a sudoers rule for `gdb`, not yet added) to find exactly where and
    why.

This is a regression (or a previously-latent bug) relative to the `87015fb`
narrative below, which was the last time the flow got as far as `Attach` -
**that state has not been reproduced since.** Whether this crash is new
(introduced somewhere in `19c9156`..`b192358`) or was always there and simply
wasn't hit on the specific run that produced the `87015fb` notes is unknown -
`git bisect` against hardware runs would settle it.

### Last known-good-ish state (87015fb narrative, unverified since)

- both DLLs relinked, relocated and through `DllMain`
- the sensor initialised over USB (`egis_init_sensor` completes)
- `sensor_adapter->Attach` **succeeding** - it consumed our
  `WINBIO_SENSOR_ATTRIBUTES` without complaint
- the engine executing its own code
- then `engine_adapter->Attach` failing with `0x8000ffff` (`E_UNEXPECTED`)

Two causes visible in that run were fixed in `87015fb` (BCrypt hash providers,
IOCTL `0x220000`) but still have not been re-tested against hardware, because
the new segfault above now happens first.

## Open threads

0. **Get a backtrace for the new early segfault** (see "Current state"). This
   blocks everything below it - `Attach` can't be re-tested until the CLI
   survives to issue any `DEVCTRL` calls at all.

1. **Re-test `engine->Attach`** (blocked on #0). If it still fails, the
   engine's `<<< EngineAdapterAttach : ErrorCode [0x%08X]` trace and the
   surrounding `CTouchSensor::` lines say where.

2. **`HKLM\SYSTEM\CurrentControlSet\Services\EgisFP\FPParameters`.** The engine
   reads `Optimization` and `SmartLearn` from it. This key is *not* in the INF,
   so no values have been invented - `reg.c` returns "not found", which is what
   Windows would do unless something had set them. If `Attach` still fails,
   guessing values here is the next experiment.

3. **Egis-private IOCTLs are unimplemented.** The driver's dispatch table
   (`EgisTouchFP0575.c` around the `0x442xxx` comparisons in the Ghidra dump)
   has named handlers for `0x442004`, `0x44200c`, `0x442010`, `0x442014`,
   `0x442018`, `0x44201c`, `0x442020`, `0x442024` and a `0x442400-0x4427fc`
   range. The engine is known to use `0x44200c` and `0x4427c0`. These log a
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
