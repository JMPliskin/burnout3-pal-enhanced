# Publication checklist

This repository publishes development source, synthetic tests, configuration,
regional adaptation evidence and documentation. It does not publish the game
or a self-contained playable package. See the root README for status and inputs.

## Audit checkpoint: 2026-10-18

- Integration HEAD: `b1b8858e8d49b149b3bd3a4353dafd4f94d71e10`.
- USA parent: `5832c4a35a09b4e2fcb130a4e674ebfd5386cd2f`.
- Engine gitlink and checkout: `d7e6e023f261d9f95ad097fcd063ec95e40ca465`.
- Fork remote branch checked read-only: `pal-enhanced` at
  `13f42950282d6167707d6ae1bbf9943f02098248`; the integration merge is local.
- 127 tracked entries, including the engine gitlink. No staged changes.
- No currently tracked files match the exclusion rules. No index removal is
  proposed; the USA icon, source assets, CSV metadata and synthetic fixtures
  are not equivalent to disc assets or generated game translations.

The new public sources `sync_native.py` and `test_sync_native.py` must be included
along with `UPSTREAM_INTEGRATION.md` and this checklist. Ignore rules must not
hide these files, CMakeLists.txt, batch JSON manifests or synthetic fixtures.

## Historical checks and their limits

All 2,068 blobs reachable from the integration HEAD in the main repository were
checked for executable/ELF/archive signatures, suspicious filenames, large blobs,
common credential patterns and private development paths. No confirmed forbidden
payload or credential was identified by these checks. This is not a legal audit
or proof that an arbitrary embedded byte array cannot contain proprietary data.

Large historical objects include USA `config/functions.csv` (function metadata,
not instruction bodies) and an old engine `iop_cpu_vectors.inc` synthetic test
fixture generated from the interpreter. The latter predates the conversion of
engine to a submodule. An old release skill contains the illustrative path
`C:\Users\...`, not an actual user's path. These inherited objects are already
reachable from the USA history; no history rewrite or removal is proposed.

The current engine is a separate public repository referenced by a gitlink;
this audit does not certify every historical object in that separate repository.
No binaries from the private filesystem are included by publishing a gitlink.

## Local files that must remain private

- Disc images, extracted ELF/IRX and original game resources.
- Analysis databases, translated EE sources and captured/generated VU programs.
- Build trees, executables, libraries, compiler outputs and dependency caches.
- Memory cards, profiles, settings snapshots, RAM, dumps, screenshots and logs.
- Private backups and workspace contents.

Existing build/extracted/source/memory-card directory rules cover the actual
local artifacts. Additional binary/cache and conventional private-output rules
protect accidental output outside build trees. Arbitrary `.bin`, `.json` and
image extensions are deliberately not globally ignored: synthetic fixtures and
public manifests must remain reviewable. New output locations require checking
their exclusions before use. `.gitignore` never removes an already tracked file.

## Before a separately authorized commit or push

1. Review modified and untracked files, including source provenance and secrets.
2. Run `git ls-files -ci --exclude-standard`; investigate any output.
3. Use `git check-ignore -v` for private artifacts and verify public tools remain
   visible with `git ls-files --others --exclude-standard`.
4. Review the explicit prospective file list and `git diff --check`.
5. Only after authorization, stage explicit reviewed paths and review the index.
   Commit authorization never grants permission to push.

Do not describe the integrated engine as fully validated: frame 2400 remains
black in the experimental startup capture. Preserve the stable branch and all
local recovery artifacts while that issue is investigated.
