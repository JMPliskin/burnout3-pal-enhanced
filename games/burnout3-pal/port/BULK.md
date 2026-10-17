# Local bulk PAL workflow

No NTSC-U addresses, hand-written guest bodies or regression promotion are used.
The original `src/lifted` and previous executables remain unchanged. All working
sets, logs, caches and manifests are ignored under `build/bulk/run-*/`.

## Engine audit

`engine/tools/lift.py` selects nonempty database functions; `--only` filters by
address and `--limit` then slices that selection. It mechanically translates with
`ps2decomp.lift`, trying the main body without chunks if joining chunks is refused.
Verification caches are keyed by generated text, ABI mode and an environment
fingerprint including engine/runtime, image, analysis and synthetic memory seeds.
The cache intentionally does not identify compiler versions. A cached verdict
is not transferable to a different fingerprint.

`write_chunks()` removes matching prefix files. It groups by 300 functions and
1,500,000 C++ characters, without splitting a function. `driver.build()` builds
a native checking DLL, excludes functions identified in compiler errors and
retries at most eight times. `verify()` compares whole register state and memory
effects using the engine's cases; its contract requires at least five completed
cases, not exhaustive game behavior. None of these verdicts is the PAL register
runner's separate eight-case regression contract.

## Commands

```powershell
py -3.12 -B games/burnout3-pal/tools/test_lift_bulk.py -v
py -3.12 -B games/burnout3-pal/tools/lift_bulk.py --plan --batch-size 250 --pilot-size 12 --jobs 8
py -3.12 -B games/burnout3-pal/tools/lift_bulk.py --run PATH_TO_PLAN --only-batches 1
py -3.12 -B games/burnout3-pal/tools/lift_bulk.py --run PATH_TO_PLAN
py -3.12 -B games/burnout3-pal/tools/lift_bulk.py --publish PATH_TO_PLAN
py -3.12 -B games/burnout3-pal/tools/build_native.py --sources PATH_TO_PUBLISHED_LIFTED --jobs 2
py -3.12 -B games/burnout3-pal/tools/probe_native.py --executable PATH_TO_NEW_EXE
```

`--run` also resumes: completed batch evidence hashes are checked and no workers
are launched for those batches. Running attempts get a new exclusive directory;
their prior cache and diagnostic files remain available. Structural compilation
or cleanup failures stop the queue rather than being silently retried. There is
no automatic retry loop or automatic function-by-function boot repair.

## Safety and evidence

The 501 existing functions are copied byte-for-byte into the published set;
their original verification annotations are historical evidence, not new strict
checks. Pending functions are translated into staging only. The plan contains
every inventory address, state, rejection and known reason. Compilation DLLs,
compiled input C++ and completed results are hashed. JSON checkpoints use atomic
replacement, and a kernel lock serializes runs of one plan.

New checks use the engine's static pointer seeds, no dynamic pointer discovery,
and `map_faults=False`, as required for final verification by AGENTS.md. This
conservative policy has its own fingerprint. The historical cache is copied,
never overwritten; incompatible entries are not reused. New compatible cache
entries and per-function outcomes are saved as verification finishes.

Each batch has a private Windows Job containing its compiler or up to eight verification
workers. Compiler concurrency is two (`/MP2`), compilation is bounded at 600 s,
and verification at 120 s per batch. At a verification timeout, completed
verdicts survive and the rest are NOT VERIFIED with no claimed equivalence.
A compilation timeout yields `compile_unconfirmed`, not a claim that those
functions cannot compile; that batch is excluded from publication and recorded.
Unconfirmed cleanup stops the queue. Known mismatches and non-compilable
translations are excluded; no stubs stand in for them.

Publication requires every batch to have a terminal record, checks all artifact
and baseline hashes, rejects duplicate symbols and verifies the exact expected
address set. It creates a new `published/lifted` directory by rename, never
replacing the active original sources. CMake's PAL-only `pal_sources.txt` sidecar
selects this complete candidate for a new exclusive native build. Native Ninja
concurrency is also two by default. The original build mode remains available.

The summary's instruction limits, memory faults, inadequate cases, unsupported
instructions, mismatches and wall limits are separate categories. A wall limit
does not show that an individual function loops forever or cannot be translated.
Generated code and diagnostic C++ must never be added to Git.

Resource telemetry is recorded per attempt in resources.json: system RAM,
system CPU utilization, hard page input rate via the English PDH counter,
actual pool process count, active tasks, per-worker CPU/wall time and PID.
At 85 percent RAM, ten seconds above 10,000 pages input/sec (or 1,000 with RAM at least 80 percent), or a Windows
resource exhaustion error, concurrency decreases from eight to six to four.
Existing tasks drain before the smaller pool is created; no in-flight task is
killed merely to resize. Resource exhaustion retries are bounded to one per
function; repeated exhaustion stops with the checkpoint retained. CPU affinity
caps the initial pool size. Mixed function costs do not justify extrapolating
throughput from a single sample. MSVC remains independently capped at two.

Continuation plans pin their predecessor and the exact controller snapshot when
only orchestration/reporting policy changes. Completed DLL/source/result hashes
are checked again and reused without rebuilding. Evidence can refer to earlier
run directories: retain the complete chain. Existing historical plans are not
rewritten to hide an interrupted run or an input version change.

A structural supervisor failure or non-timeout verification infrastructure
failure blocks the queue with partial checkpoints retained. A function-level
untestable verdict remains an individual outcome. MemoryFault load/store are
reported separately from insufficient completed test cases.

The PAL wrapper retries transient Windows replacement failures (WinError 5/32)
up to eight times with bounded backoff. It preserves diagnostic temporary files,
keeps the underlying atomic writer, and propagates permanent failures. This
avoids a live JSON reader aborting an otherwise healthy checkpoint write.
