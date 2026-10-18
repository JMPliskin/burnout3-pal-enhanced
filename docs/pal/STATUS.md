\# Burnout 3 PAL Enhanced - Status



\## Target version



European PlayStation 2 PAL release:

SLES-52584



\## Current development status



Work in progress.



Latest development checkpoint: d47b132.



The checkpoint reports:

\- Playable native VU build.

\- Save functionality.

\- Movie skipping.



These features require further validation before release.



\## Initial progress



\- PAL-specific recompilation research.

\- PAL game directory and configuration.

\- Initial Spanish localization integration.

\- Original startup sequence investigation.

\- Identification of a missing dispatch stage.



\## Current blocker



Investigate PAL startup dispatch around

MIPS address 0x00242AC8.



The original European version displays

the 50/60 Hz selection screen after

language selection.



The PAL PC port has not yet completed

this startup sequence.



\## Validation principles



\- Reference original PAL MIPS behavior.

\- Separate game logic from rendering tests.

\- Do not require pixel-perfect matching

&#x20; with the modified USA renderer.

\- Automate reproducible tests locally.

\- Do not distribute generated game code.



\## Release status



No playable public PAL release.
