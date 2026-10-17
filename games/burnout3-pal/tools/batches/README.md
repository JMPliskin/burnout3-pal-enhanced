# PAL batch runner MVP

Run from the repository root with Python 3.12:

```powershell
py -3.12 games/burnout3-pal/tools/run_batch.py --manifest games/burnout3-pal/tools/batches/batch-001.json --validate-only
```

Manifest paths are relative to the shell; game and output paths are relative to the script. Validation writes only a new ignored `build/batches/<id>/validate-<UUID>/results.json`. Existing runs are never overwritten.

`schema_version: 1` requires an explicit selection, unique addresses and cases, exact counts, limits, memory contracts and coverage requirements. u64 arguments use exactly sixteen hexadecimal digits. Omitted arg0..arg7 default to zero in the planned executor. Objects use scratch-relative offsets, power-of-two alignment, byte fill and nonoverlapping little-endian fields. Argument references identify an object and offset. Read contracts specify object, offset, width and sign; writes are forbidden in this MVP. Pointer provenance is not dynamically proved by validation.

The batch-001 manifest contains the exact 40 input pairs recovered from the successful report identified by its path and SHA-256. It embeds no return oracle, executable data or reconstructed function implementation. The report is local provenance and is not required on another checkout.

Explicit validation and exploration are separate: `explore` is recognized but rejected as unimplemented. Validation does not certify dynamic coverage, successful derivation, interpreter support or equivalence. Counts of eight or more satisfy the current collect harness minimum; no cases may be silently omitted in future execution.

Dynamic execution is intentionally unavailable. Without `--validate-only`, the runner exits nonzero with an explicit error, and does not import derive or compile anything. Planned execution uses one worker per function, scoped adapters restored at worker exit, collect(max_passes=1), strict emit count/signature checks and cppcheck.build, plus independent MIPS/symbolic/DLL runs on identical restored memory. Supervisor timeouts must cover workers and compiler descendants. These worker and timeout mechanisms are not implemented yet; limits are only validated and reported.

Future results must distinguish rejection, mismatch, coverage failure, timeout, compilation error and structural error. No automatic repair or dropping failed functions. Return checking with the current DLL harness is s32; full 64-bit returns and HI/LO contracts are not supported.
