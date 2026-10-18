# Reproducing the functional diagnostic engine

Base: ps2-port-engine d7e6e023f261d9f95ad097fcd063ec95e40ca465.
The adjacent patch preserves the exact four source files used by the private
functional GPU Thread experiment: bridge, queue, Vulkan presentation and a
bounded trace header. No renderer algorithm fix is claimed. The user completed
races with logging enabled and absent; the previous binary freeze is unexplained.

Apply only to the pinned clean engine, in an isolated checkout. Review with
`git -C engine apply --unidiff-zero --check ../games/burnout3-pal/port/diagnostics/gpu-trace-d7e6e023.patch`
before applying. Do not update the gitlink to pretend these uncommitted changes
are already upstream. The patch carries engine source, not extracted guest code.

PS2HOST_GPU_TRACE activates the bounded diagnostic buffer. Its absence disables
logging, but does not remove instrumentation from the compiled executable.

The player alpha recompiles its PAL entry point and six SDL units only to trim
private __FILE__ assertion paths; no SDL source or algorithm was modified.
Instrumented host/GPU libraries and same-revision EE/VU objects were reused.
A new link is not proof that the previous freeze cannot recur. User testing of
the player alpha passed native startup, ISO persistence, renamed-disc recovery
and a complete race with GPU Thread On. Subsequent clean-install/new-profile
save and recovery validation is also maintainer-confirmed.

The patch uses zero context to avoid whitespace-only context lines in the public
source diff. Apply with `git -C engine apply --unidiff-zero
../games/burnout3-pal/port/diagnostics/gpu-trace-d7e6e023.patch` after the check.

## Clean player reconstruction

Use a separate worktree at the player source revision and a separate engine
checkout at d7e6e023. Apply the public patch with --unidiff-zero as above; do not
reuse the diagnostic experiment's libraries or object files. CRLF/LF differences
in patch application do not change the C++ source tokens.

From an MSVC/Ninja-capable Windows environment:

```powershell
py -3.12 -B games/burnout3-pal/tools/test_sync_native.py -v
py -3.12 -B games/burnout3-pal/tools/sync_native.py --prepare PATH_TO_PRESERVED_PAL_CHECKOUT --build-root PATH_TO_PRIVATE_BUILDS
py -3.12 -B games/burnout3-pal/tools/sync_native.py --execute PATH_TO_NEW_WORK_JSON
```

The preparation checks historical input hashes and the original 97-program VU
manifest, uses preserved EE sources and dispatcher/worker completion, emits VU
for the pinned engine ABI, and references cached dependency sources. It requires
the preserved private build manifests described in RECOVERY.md; a source clone
alone is insufficient. No old object files are reused. Subsequent builds allow three compile workers;
reduce to two on excessive memory pressure or resource errors. The first clean
reconstruction started before this policy change and used two workers.
New manifests name Burnout3-PAL-Enhanced.exe and include synthetic startup tests;
old manifests retain their recorded executable and original test targets.
The build report records the output hash and host/renderer/startup test results.
A successful build does not prove bit-identical reproduction or resolve the
historical GPU Thread freeze. Manual race validation is now maintainer-confirmed.

First clean reconstruction completed in experimental/player-clean-build: fresh
Release /O2 CMake/Ninja build, static MSVC runtime, Vulkan, 9383 preserved EE
functions and 97 regenerated VU programs. No old objects/libraries reused.
Ten adapter tests, eleven synthetic startup tests and all host/renderer tests
passed. One bounded ISO startup with GPU Thread On and trace variable absent
reached 2200 VBLANK / 1898 distinct functions; exit 2 was the requested frame stop,
with successful process cleanup. No 3D/VU pairs or usable visual capture in that
probe. Subsequent maintainer testing confirmed correct racing with GPU Thread On,
an empty isolated installation, creation/save of a new profile, ISO persistence
and recovery of that profile after restarting, without observed errors.
The executable hash differs from the earlier selectively linked alpha; neither
bit-identical reproduction nor resolution of the historical freeze is claimed.
