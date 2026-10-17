# PAL native bootstrap

This is an independent native target, `burnout3-pal`, based on the engine's
official game template. It links `engine/host`, `engine/runtime`, and only the
PAL C++ emission in `../src/lifted`. No NTSC-U game sources or address overrides
are linked. `RegisterGameOverrides()` deliberately registers nothing: missing
guest functions must remain diagnostic failures.

The first bootstrap emission contained 500 PAL functions, including 23 explicitly marked
NOT VERIFIED. Building these does not change their verification status. The
entry at `0x00100008` is among those unverified functions. The remaining 8,884
database entries were represented as untranslated entries in its dispatch table.

## Incremental additions

```powershell
py -3.12 -B games/burnout3-pal/tools/test_lift_incremental.py -v
py -3.12 -B games/burnout3-pal/tools/lift_incremental.py 0x00115108
```

The incremental tool reuses `ps2decomp.lift.lift_function` without restructuring
the result. It calls the original chunk writer only in a fresh staging directory
under `build/incremental/`, then publishes one `incremental_<ADDRESS>.cpp` in
`src/lifted` with atomic no-replace creation. It never invokes `lift.py --emit`
on the existing emission directory. Duplicate symbols or filenames are errors;
existing C++ hashes and input hashes are checked before publication. The staged
file and `evidence.json` are retained. Generated C++ stays ignored by Git.

Every incremental addition is explicitly NOT VERIFIED. A previous outcome is
recorded as provenance, not accepted as a new verification. The first addition,
`0x00115108`, previously reached ExecutionLimit in the reference interpreter;
this does not establish an infinite loop in that function. Its eight direct
calls, their delay slots and final tail jump to `0x00116E18` were inspected in
the original lifter's output before inclusion. Existing 500 translations remain
unchanged; the experimental emission now contains 501 unique functions.

## Configuration and build

`game.json` defaults resolve `generated` to `src/generated` and `status` to
`config/functions.csv`. `port.py` obtains the lifted directory from the parent
of `generated`, then scans `void F_<ADDRESS>(lifted::Context& c)` definitions.
It generates dispatch from those addresses and the PAL database. Missing
`config/names.csv` is accepted; names are diagnostic labels and also select
shared SDK overrides. This bootstrap uses no inferred names. `functions.csv`
is verification bookkeeping, not a native link input. A game's `syscalls.csv`
is not read by this build; the shared host implements the EE syscall ABI.

The original build command remains:

```powershell
py -3.12 -B engine/tools/port.py --game games/burnout3-pal build
```

On installations with standalone CMake/Ninja rather than Visual Studio's bundled
copies, use the PAL adapter:

```powershell
py -3.12 -B games/burnout3-pal/tools/build_native.py
```

It delegates to `port.generate()` and `port.build()`, resolves missing bundled
tools from PATH, and uses an exclusive `build/native-bootstrap/run-*/` directory.
The selected tools, commands, input hashes, emission addresses and result are
recorded in `build.json`; subprocess output is preserved in numbered logs.
The engine's build also runs `ps2host_tests.exe`. No new guest translation or
automatic build/run discovery rounds are requested.

The standard Vulkan backend is enabled. The attempted software-only build
compiled the PAL sources but could not link: the shared host's non-Vulkan branch
lacks `GpuCaptureScreen`, `GpuCaptureBegin`, `GpuPrimary`, and
`GpuBusyMicroseconds`. The engine and NTSC-U port have not been modified to hide
this failure. Public host dependencies are fetched by the existing CMake files.

## Controlled diagnostic launch

```powershell
py -3.12 -B games/burnout3-pal/tools/probe_native.py --executable PATH_TO_PAL_EXE
```

The probe supplies the local PAL ELF explicitly, runs headless, enables tracing,
limits host time to three seconds and the loop budget to one million, and uses
the existing private Windows Job helper with an outer 15-second deadline.
Logs and containment diagnostics are exclusive under `build/native-probes/`.
It supplies the configured disc image if available, without a GUI, and never
starts further translation or discovery/build rounds.
It does not pass `--gpu`, `--window` or `--audio`; linking Vulkan does not require
using its renderer in this diagnostic. A compiled executable, host startup,
execution of PAL instructions and actual game boot are separate milestones.

## Regional dependencies still requiring evidence

The NTSC-U port's kernel-store and TTY hooks, memory-card handlers, DMA idle-loop
addresses, movie hooks, frontend modifications, graphics hooks and input hooks
cannot be copied by address. Its IOP adapters run RWA.IRX, GTFSCDVD.IRX and
B3ROUTE.IRX and implement a Logitech RPC protocol. These may be needed later,
but the PAL module identity/protocol must first be checked. None is copied here.
The shared loader, EE kernel, memory, scheduler, DMA/VIF, GS, VU, SDK services,
disc access and IOP infrastructure are reused as provided by the engine.
The host's documented real-time 60 Hz assumptions also need a regional audit
before claiming faithful PAL timing.

## First local result

At repository HEAD `893b0e23a9f0b3f561be9fbefa6c869d5464772b`, the standard
backend configured and compiled successfully, including the host's synthetic
VU corpus. The host tests returned zero and printed `host tests: all passed`.
Evidence: `../build/native-bootstrap/run-a86fadf8fe7d/build.json` and its
`driver.log`, numbered subprocess logs, and `port/cmake/` outputs.

The executable is
`../build/native-bootstrap/run-a86fadf8fe7d/port/cmake/burnout3-pal.exe`.
It ran the PAL entry `0x00100008`, performed SetupThread and SetupHeap, then
stopped at a call to untranslated `0x00115108`. That address is a PAL database
function (84 bytes, 21 instructions, ending at `0x0011515C`) outside the current
500-function emission. It is not evidence of a missing function boundary,
despite the host's generic `code no function covers` diagnostic. No function
identity beyond that structural evidence is claimed.

Evidence: `../build/native-probes/run-9e0f00d44302/host.log` and
`containment.json`. The contained child exited with code 2, no timeout,
confirmed cleanup and zero remaining active processes. No GS primitives or
VU instruction pairs ran. This is initial PAL code execution, not game boot.
The two existing lifted files, ELF, database and game configuration hashes
still matched the build's recorded inputs after the probe.

## Second local result

After adding only `incremental_00115108.cpp`, the second executable compiled and
the host tests again printed `host tests: all passed`, exit zero. Evidence is
under `../build/native-bootstrap/run-44aa775c1cb5/`; its dispatch has 501 concrete
entries and 8,883 untranslated entries, with `F_00115108` present exactly once.

The one bounded probe executed both `0x00100008` and `0x00115108`, then stopped
on the latter's first direct call, to untranslated `0x00114ED0`. That is also an
existing PAL database function (72 bytes, 18 instructions, ending `0x00114F18`).
No additional translations were attempted. The child exited with code 2, no
timeout, confirmed cleanup and zero remaining processes. Evidence:
`../build/native-probes/run-d5c7ed94deb7/host.log` and `containment.json`.

The first executable, original lifted files, ELF and game configuration still
match their recorded hashes. Executing part of the added function does not
establish its dynamic equivalence or successful game boot.

Next action: investigate and verify the narrowly selected PAL dependency
`0x00114ED0` before its next experimental incremental inclusion.
