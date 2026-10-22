# Publication checklist

## v0.2.0-alpha: source publication and binary gate

The Windows x64 candidate is prepared and tested locally. The maintainer has
confirmed that no documented authorization/legal review for redistribution of
the reconstructed original game program is currently available. The port/engine
MIT licenses do not supply those rights. Keep the EXE and ZIP local until that
issue is resolved. Manual uploading does not remove this requirement.

The maintainer authorized publication of the accepted engine source, PAL
`pal-enhanced` branch and exact `v0.2.0-alpha` tag on 22 October 2026. This does
not authorize creating a GitHub Release or uploading the EXE/ZIP. Binary release
publication is a separate manual step after the distribution-rights issue is
resolved; manually uploading does not bypass that requirement.
A local annotated PAL tag is prepared on the final source commit, including
the documentation-only publication corrections. The tested EXE/ZIP are unchanged.
The development repository already has an unrelated USA `v0.2.0-alpha` tag;
preserve it. The PAL tag is prepared in the isolated local publication repository
identified in the release dossier. Run the tag checks/push there, never publish
the development repository's same-named USA tag.
The previous release/tags and both `master` branches remain intact. Only the
authorized engine integration branch and PAL `pal-enhanced`/release tag are updated.

### Git dependencies before using GitHub Releases

For the authorized source publication (no binary assets):

1. Publish the engine branch containing
   `bfad7255f17f7789a8bbb0a59a6e3a003535c548` to JMPliskin/ps2-port-engine.
2. Verify that exact object can be fetched from the fork.
3. Check the candidate ancestry, gitlink and documentation-only final changes.
4. Recheck remote `pal-enhanced`. Advance it to the audited final source only when
   the update is a normal fast-forward; stop if the remote changed unexpectedly.
   The candidate already merges the accepted integration and the main commit
   observed during preparation. Never force-push or rebase published history.
5. Verify `git rev-parse v0.2.0-alpha^{commit}` equals the audited final game SHA,
   then publish that explicit local tag. Never tag an implicit default-branch HEAD.
6. Verify a fresh checkout can resolve `engine` through the JMPliskin .gitmodules URL.

Exact SHAs, destinations, package hashes and proposed commands are recorded in
the private release preparation dossier; verify them again before execution.

### Manual steps on GitHub

Once source refs are published, distribution rights are resolved and the
maintainer is authorized to publish the binary package:

1. Open JMPliskin/burnout3-pal-enhanced, then **Releases > Draft a new release**.
2. Select the already verified **v0.2.0-alpha** tag. Do not create it on master or
   another default target. Confirm its commit matches the audited candidate SHA.
3. Title: **Burnout 3 PAL Enhanced v0.2.0-alpha**.
4. Paste `RELEASE_NOTES_v0.2.0-alpha.md` from that tagged revision.
5. Attach `Burnout3-PAL-Enhanced-0.2.0-alpha-windows-x64.zip` and its `.zip.sha256`
   checksum file. Check their recorded SHA-256 values before uploading.
6. Enable **This is a pre-release**. Do not mark this alpha as a stable latest release.
7. Review tag/commit, notes, assets and rights clearance. Only then, with explicit
   publication authorization, choose **Publish release**.
8. Verify the public asset checksums/download links and README instructions.
   Keep v0.1.0-alpha and all its assets/tags unchanged.

The following audit is historical; it is not current binary distribution clearance.

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
