# From decompilation to a native port

Decompilation produces C++ for each function and proves it equivalent to the
original. A port runs that C++ on a PC. This document describes the layer in
between, the **native host** (`engine/host/`), and how a game becomes an
executable.

## The pieces

```
engine/runtime/   PS2 instruction semantics: FPU, VU0 macro mode, integer edge
                  cases, 128-bit MMI. Shared by verification and the port, so the
                  port runs exactly the semantics every function was verified on.
engine/host/      The PC platform: guest memory, dispatch, the EE kernel, devices.
games/<g>/src/lifted/   The game's translated functions (lift.py --emit).
games/<g>/port/   The game's port: CMakeLists.txt and overrides.cpp.
```

Translated code reaches its environment only through a few hooks
(`ps2rt/lifted.h`, `ps2rt/generated.h`):

| hook | in verification | in the port |
|---|---|---|
| `ReadU32`, `Write32`, ... | the Python interpreter's memory | `ps2host::Memory`: flat RDRAM, scratchpad, register windows |
| `Call`, `TailCall` | the interpreter runs the original callee | the dispatch table runs the translated callee, or an override |
| `Syscall` | a fixed stub | `kernel.cpp`, the EE kernel's services |
| `Tick` | aborts a runaway loop | the same, with a budget large enough to play |

Because every translated function was verified against the interpreter through
these same hooks, a port is correct exactly as far as its host implements them
faithfully.

## Building and running

```sh
python engine/tools/lift.py --game games/<g> --structure --emit   # translate
python engine/tools/port.py --game games/<g>                      # build and run
python engine/tools/port.py --game games/<g> run -- --trace       # log every call
```

Options for finding out why boot stops, passed after `run --`:

| option | what it shows |
|---|---|
| `--trace` | every call, not just the first of each function |
| `--notes` | every host event (DMA, interrupts, SIF, kernel calls), repeats included |
| `--watch ADDR` | each call of the function at ADDR, with its argument registers and `$ra` |
| `--peek ADDR` | 64 bytes of guest memory at ADDR, in the report |
| `--timeout S` | the time limit (30 s); `--budget N` makes a run deterministic |

The guest's own debug output (`kputs`) appears as `[guest]` lines.

`port.py` generates `build/port/generated/dispatch.cpp` (every translated
function by address, with names from `config/names.csv`), builds `port/` with
the CMake and Ninja that ship with Visual Studio, and runs the result on the
executable in the game's `extracted/` directory. Nothing from the disc is built
into the port; the user supplies it.

The run ends with a report: why it stopped, the guest call stack at that point,
each thread's state, the kernel services used, and every device register
touched. That report is the port's progress measure, as `progress.py` is the
decompilation's. The first thing in it that the host lacks is the next thing to
implement. A run that goes on longer than `--timeout` seconds (30 by default)
stops at its next loop head and reports where it was. If the host process
itself outlives that by 90 seconds, port.py kills it and says so: that is a
host bug, not the game's.

## Code that exists only at run time

Some code is never where the analysis looks for it:

* **copied code** - start-up code keeps kernel extensions as data and copies
  them into kernel RAM (Burnout 3 installs three: the alarm services, and two
  more), then runs them there;
* **unreached code** - functions in the executable that no call reaches and no
  pointer table names: virtual methods, stored callbacks, tail-call targets.

The analysis recovers what it can of the second kind from the gaps between the
functions it found (`engine/ps2decomp/gaps.py`). For the rest, the host records
every call to an address with no translation whose bytes it finds in the
executable (`--discover`), and `port.py` translates that code at the address it
runs from and builds again, until a run finds nothing new. A call to a copy of a
function already translated runs that translation directly.

These translations are not verified - the interpreter never runs them - and the
generated source says so.

## Overrides

`ps2host::Override(address, function, name)` replaces the guest function at an
address with a host implementation. It is the mechanism for:

* **hardware redirection** - library routines that drive the GS, VU1 or the IOP
  are replaced by host code that does the same job with the PC's facilities;
* **enhancements** - resolution, frame rate, input, widescreen: hook the
  function that decides it.

Prefer overriding a whole library routine (`sceGsSyncV`, `sceSifCallRpc`) to
emulating the hardware it touches: the routine's contract is smaller than the
hardware's, and `names.csv` already identifies many of them. Routines every
game links - the SDK's - are overridden by name in the engine
(`engine/host/src/sdk.cpp`), wherever the game's names table gives that name,
so a new game gets them as soon as its routines are named.

## What the host provides, by stage

1. **Boot** (done): memory map with its mirrors (kuseg, kseg0, kseg1) and IOP
   RAM; ELF loading; dispatch; kernel services for start-up (SetupThread,
   SetupHeap, FlushCache); the syscall table in guest kernel RAM, so SetSyscall
   and programs that read or patch the table behave as on the console; DMA
   completing at once; GS vertical blank; timers that advance.
2. **Threads** (done): EE threads as host fibers under the kernel's
   strict-priority scheduler; semaphores, sleep and wake-up block and resume.
3. **Interrupts** (vertical blank and DMA done): handlers run at a safe point,
   which is the running thread's next loop head (`lifted::loop_head_hook`) or
   between threads. Vertical blank comes 60 times a second in real time, or at
   once when every thread waits. DMA completion is posted by the transfer and
   delivered after the store that started it. Alarms are next.
4. **IOP services** (begun): the host answers the SDK's SIF commands as the
   IOP would (`iop.cpp`): RPC bind and call answered with RPC-end packets in
   the EE's receive buffer and the SIF0 DMA interrupt, run by the scheduler.
   Servers are host functions registered by id (`RegisterRpcServer`); a server
   with none still binds and its calls are noted with their callers, which
   names the library function behind each. CD/DVD (`cdvd.cpp`) reads the
   user's disc image (`disc.cpp`, ISO 9660, `--disc`). Also done: file I/O
   (`fileio.cpp`), module loading (`loadfile.cpp`: loads succeed and run
   nothing) and the controller manager's handshake (`dbc.cpp`). Servers for
   modules a game ships on its own disc belong to its port
   (`games/<g>/port/iop_servers.cpp`). Pads, sound and memory cards follow.
   DMA channels VIF0, VIF1 and GIF are walked (`dma.cpp`), chains included,
   and VIF1's stream is decoded (`vif.cpp`).
5. **Graphics**: GIF packets to a GS renderer; VU1 microprograms translated or
   emulated.
6. **Audio, input, saves**, then enhancements.

Each stage is measured the same way: run, read the report, implement what
stopped it.
