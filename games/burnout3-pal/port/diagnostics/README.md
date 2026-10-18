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
and a complete race with GPU Thread On. Clean-install and memory-card provenance
validation remain mandatory before any public binary Release.

The patch uses zero context to avoid whitespace-only context lines in the public
source diff. Apply with `git -C engine apply --unidiff-zero
../games/burnout3-pal/port/diagnostics/gpu-trace-d7e6e023.patch` after the check.
