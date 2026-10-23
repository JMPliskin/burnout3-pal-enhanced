# PAL batch execution and evidence

This supersedes the original MVP note that dynamic execution/Jobs were unavailable.
The runner/pipeline/queue now implement explicit execution, containment and
persistent recovery. Not all PAL functions are admitted or semantically verified.
See the root docs/pal/VALIDATION.md and docs/SESSION_HANDOFF.md.

## Explicit mode selection

Run from a compatible repository root with Python 3.12 and legitimate local
inputs when required. These examples do not authorize execution:

```powershell
py -3.12 -B games/burnout3-pal/tools/run_batch.py --manifest games/burnout3-pal/tools/batches/batch-001.json --validate-only
py -3.12 -B games/burnout3-pal/tools/run_pal_pipeline.py --batches batch-001 --validate-only
py -3.12 -B games/burnout3-pal/tools/pal_auto.py --plan --manifest games/burnout3-pal/tools/batches/batch-001.json
```

Validate-only writes a new ignored result; it does not execute guest instructions,
derive/compile or prove coverage/equivalence. Pipeline --execute enables actual
batches. Queue --run PLAN and --resume PLAN use explicit immutable snapshots.
There is no implicit run-all, automatic repair or promotion.

## Contracts and independent comparison

Manifests require safe unique IDs, addresses/cases, exact counts, limits, hashed
inputs/tools, synthetic initial memory and explicit coverage. Arguments retain
u64 images; the historical return harness is s32. Full registers, HI/LO, 64-bit
returns and arbitrary runtime pointer provenance are not thereby established.

For admitted profiles, workers compare MIPS, derived expression and native DLL
separately on restored memory, checking returns and required side effects. Cover
branches, conditional selections and delay slots; never omit a failing case.
No embedded game body or reconstructed return oracle belongs in a manifest.
Scratch is isolated synthetic memory, not guaranteed free game heap space.

## Supervision and persistent recovery

The Windows Job supervisor owns worker/descendants, assigns before resume, and
records APIs/PID/errors, individual/global deadlines and cleanup. Timeouts never
pass; a containing deadline does not prove an independent compiler deadline.
Terminate only owned processes; no elevation or sandbox bypass.

The pipeline sorts explicit manifests, checks Windows-safe identities and writes
atomic checkpoints plus per-function/aggregate results. pal_auto validates plan
semantics, input snapshots and attempt history before resume. Only
--resume PLAN --retry-infrastructure permits one further infrastructure/interrupted
attempt after confirmed cleanup. Equivalence, coverage, static rejection,
compilation failures and timeouts are not automatically retried. Structural
errors stop the authorized operation; keep every failed attempt.

## Evidence and limits

Batch-001's 40 cases and batch-002's 80 cases have retained dynamic reports.
Repeated runs do not add unique historical coverage. The 37-batch lifter summary
is a different evidence level, not complete semantic proof. Relevant existing
tests include test_generate_batches.py, test_run_pal_pipeline.py, test_pal_auto.py
and test_windows_job.py; select affected tests rather than a new framework.

Use exclusive ignored build/batches, build/pipeline and build/auto outputs. Never
modify previous runs, source manifests, ELF/maps or user saves to obtain a pass.
The main checkout may lag the published code; read the handoff before testing.
