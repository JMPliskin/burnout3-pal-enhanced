# Working instructions: Burnout 3 PAL Enhanced

## Read before making changes

- Read PAL_DEVELOPMENT.md, docs/SESSION_HANDOFF.md, docs/pal/ROADMAP.md,
  games/burnout3-pal/AGENTS.md and games/burnout3-pal/DEVELOPMENT_POLICY.md.
  Consult existing NTSC-U and engine solutions first; reuse AutoTest.
- Check branch, HEAD and git status. The main branch is pal-enhanced. If the
  branch differs unexpectedly, report it and ask for direction; do not switch
  or discard work automatically. Explicitly authorized isolated branches are expected.
- Inspect real files/reports and flag stale documentation. A correctly named
  local branch can still lag the published baseline; see the handoff.
- Use English for technical documentation, code comments, internal instructions
  and commit messages. Use concise Spanish for user reports and explanations.

## Context efficiency

- Use the handoff as an evidence index; read the relevant sections/functions and
  concise summaries rather than repeatedly loading complete histories or raw logs.
- At a session boundary, record the exact baseline, dirty-work context, new verified
  findings, evidence locations and the next unresolved contract in existing documents.
  Do not reconstruct missing details from memory or create parallel project histories.
- Distinguish status checked in the current task from reused status. Respect explicit
  tool prohibitions; if Git queries are excluded, report the last verified Git state.

## Scope and protection

- Work in small reviewable steps with explicitly selected functions and cases.
- Preserve the working Spanish PAL setup, original language/video selections
  and accepted functionality. Use the original PAL ELF as the authority for PAL
  game logic. USA code or equal addresses do not establish regional identity.
- Protect engine/, games/burnout3/ (NTSC-U), game.json, ELF and existing databases;
  do not modify them without explicit authorization for those paths.
- Keep experiments isolated from stable functionality. Never overwrite earlier
  runs; create exclusive build directories and retain partial reports/diagnostics.
- Do not run the full game, port.py, derivation or compilation beyond authorized scope.
- Preserve unrelated user changes and untracked files. Never reset a dirty
  checkout to obtain a newer baseline or silently reopen a completed investigation.

## Content and Git

- Never version ISOs, ELF/executables, DLLs, binaries, BIOS, commercial game assets,
  generated databases, translated/reconstructed game C++, generated reports or
  personal data/secrets. Own scripts may contain addresses and synthetic test
  data, but no embedded game bodies or reconstructed return oracles.
- build/ and extracted/ are ignored. Check exclusions and the index; never use
  git add -f for artifacts. Use portable script/config-relative paths; absolute
  machine paths belong only in ignored local diagnostics.
- Do not stage, commit, push or publish without authorization for that action.
  Stage only authorized paths and review the staged diff. Local commit permission
  never implies push permission. Past release authorization is not future permission.
- Authorized commits need a descriptive English title and meaningful body:
  what changed, why, and how it was tested or which evidence was reused.
- Publish engine objects to an accessible fork before publishing dependent PAL
  gitlinks. Preserve upstream remotes and old tags; verify exact release tag targets.

## Tests and evidence

- Schema checks/static inspection are not dynamic equivalence. Never invent
  original cases, outcomes, coverage or timings; recover evidence or state it is missing.
- Keep tests independent and traceable: input/tool hashes, exact cases, u64
  arguments, initial memory/contracts, per-function and aggregate reports.
- Compare MIPS, derived expression and native DLL separately where applicable;
  require every case, return and memory effect without silent omissions.
- Distinguish the harness s32 return from u64 argument images. Do not claim full
  register, HI/LO or 64-bit-return equivalence without a specific check.
- Cover branches, conditional selections and delay slots; record unproved coverage.
- Disable automatic fault mapping in final verification. Scratch is synthetic
  isolated-test memory, not guaranteed free memory during full gameplay.
- Failures are not success. Stop on structural errors; continue individual failures
  only when the authorized procedure permits it. No automatic engine/generated-code repair.

## Windows isolation

- Give every worker a private Job before resuming it; no elevation or sandbox bypass.
- Terminate only owned processes/Jobs; verify cleanup and retain PID, API and
  GetLastError evidence. Individual/global timeouts must prevent passed status.
- An independent compilation timeout remains unproved until implementation and
  test evidence establish it. Supervisor tests use owned synthetic processes,
  without game execution or compilation unless specifically authorized.

## Incremental Validation and Evidence Reuse

Apply this policy automatically to every Burnout 3 PAL Enhanced task. Before testing, briefly identify changed files and functions, affected interfaces and contracts, direct dependencies, and concrete regression risks. Test what changed and what those changes can affect; do not audit the whole project by default. A small edit to a critical function may still require broader targeted tests.

- Reuse applicable prior evidence when the relevant components and dependencies are unchanged. Report separately: **previously validated and unaffected**, **validated in the current change**, **pending manual validation**, and **not covered**. Prior tests do not directly validate a newly built executable.
- Match validation to risk: localized changes need incremental compilation and specific tests; complete features need their own tests and direct dependencies; shared components need targeted tests of affected consumers; major integrations need the supplied integration regression; releases need general regression and manual acceptance.
- Reopen a completed Vulkan, GS, VU, CPU, audio, or repository audit only for new evidence, a reproducible regression, a relevant dependency change, or a technical contradiction that affects implementation. Once acceptance criteria are met, finish the task without speculative diagnostic phases.
- Prefer existing engine tools and evidence, including `framediff.py`, `gsmem.py`, `timeline.py`, GS Replay, profiling flags, GS captures, and existing synthetic tests. Add instrumentation only when these cannot answer the specific question.
- Request manual tests only when they provide new evidence. Group brief manual acceptance at the end of a feature or meaningful integration; do not request full races or broad matrices for unrelated small changes.
- Keep each task report concise: what changed, tests run, prior results reused, tests omitted with reasons, and remaining work.
- Preserve stable releases, main branches, reference executables, and previous sessions during experiments. Do not regenerate EE/VU without demonstrated need, update upstream or submodules incidentally, or commit, push, merge, or rebase without authorization.
