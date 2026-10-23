# Permanent policy for efficient PAL development

Read with the root/PAL AGENTS.md, PAL_DEVELOPMENT.md and docs/SESSION_HANDOFF.md.
Establish runnable state from real manifests/reports, not historical prose or
function counts. Root Incremental Validation and Evidence Reuse is permanent.

## Required order when a concrete blocker appears

1. AutoTest: read the relevant summary.json and identify the first reproducible blocker.
2. NTSC-U: use rg/git grep in games/burnout3/port, docs/FINDINGS.md, config and
   port/runs; consult PAL port/ADAPTATIONS.md to avoid repeating resolved work.
3. Engine: inspect only relevant host/runtime/tools and docs/PORTING.md. Reuse
   existing mechanisms before writing another translator, supervisor or runner.
4. Diagnosis: establish instructions, arguments, structures, calls, returns and
   observable effects. Equal addresses/sizes do not establish binary identity.
5. Correction: minimal and preferably PAL-specific. Separate required services
   from optional PC features; no invented instructions, no-ops or fabricated responses.
6. Validation: targeted unit tests, incremental compilation only when justified,
   then AutoTest QUICK when applicable. Preserve evidence and stop on integrity
   failure, regression or an unexpected out-of-scope failure.
7. Record only verified findings in ADAPTATIONS.md: PAL problem, existing USA
   solution or its absence, equivalences/differences, fix, tests, limitations and
   the single next blocker/action. Preserve dated history.

## Resources and local autonomy

Reuse AutoTest-PAL.bat and tools/autotest.py for self-contained authorized jobs.
Read summary JSON/relevant excerpts; avoid repeatedly polling CPU/RAM or full logs.
Initially allow at most two MSVC/Ninja jobs; the current adapter compiles serially.
Immutable object reuse does not imply a compatible CMake tree: verify hashes,
generator, arguments, ABI and paths first.

Do not repeat the historical 9,383 translations, 37 batches or 2,923 unverified
entries without explicit need/scope. These are origin counts, not permanent caps.
Prefer one bounded change, tests, at most one incremental build and one QUICK
unless a concrete technical reason justifies more. Documentation-only changes
need document checks, not a build or gameplay probe. Stop at the next major fix.
Do not use parallel agents for small tasks, external research when local code
suffices, or install dependencies without demonstrated need.

## Integrity

Preserve translations, EXEs, logs and reports in exclusive ignored build directories.
Never promote NOT VERIFIED because compilation/startup/gameplay succeeds. Execution
coverage is not equivalence or proof of reaching a menu. Retain process supervision,
timeouts, private TEMP where needed, and separate failure/cleanup evidence.
Do not modify NTSC-U, update the engine or change shared runtime without explicit
authorization. Do not alter extracted inputs or version their derivatives.
Do not stage, commit or push without explicit authorization. Efficiency removes
duplicate supervision/work, not the required quality of evidence.
