Burnout 3: Takedown - PAL Enhanced 0.1.0-alpha
FUNCTIONALLY VALIDATED ALPHA - PUBLIC BINARY DISTRIBUTION PENDING CLEARANCE

Requirements
Windows 10 version 1903 or newer (64-bit), an AVX2 CPU and a Vulkan-capable GPU
with a current driver. Supply your own compatible European PS2 ISO, SLES_525.84.
No Python or BAT is needed to play an approved Windows package.

Getting started
Extract the complete package into a normal local folder and double-click
Burnout3-PAL-Enhanced.exe. Choose your PAL ISO when prompted. NTSC-U images and
invalid images are rejected. Cancel closes the player. The ISO path is remembered;
if you move or rename it, the player asks again. No game disc or assets included.

Choose your language and follow the original PAL 50/60 Hz selection screens.
Use your controller or keyboard; F9 opens the shared PC settings panel. Press
Start to request skipping a non-looping movie. Tested controller: Xbox One USB.
Keyboard defaults: arrow keys for direction, X for Cross, C for Circle, V for Triangle
(Back in PAL menus), and Enter for Start. Controller face-button labels follow
the PS2 layout: Xbox A = Cross, B = Circle, Y = Triangle. F9 opens settings.

Player data
%LOCALAPPDATA%/Burnout3-PAL-Enhanced/pc_settings.ini holds settings and ISO path.
%LOCALAPPDATA%/Burnout3-PAL-Enhanced/memcard/ holds persistent saves.
%LOCALAPPDATA%/Burnout3-PAL-Enhanced/logs/ holds per-run diagnostics.
An existing card may load an existing profile. Installing or replacing the EXE
does not clear this data. Back up saves before importing any files. Do not run
two game instances using the same writable card.

Known limitations
Early alpha: not all modes or hardware are tested. GPU diagnostic instrumentation
remains compiled in, even without logging. An older GPU Thread freeze is still
unexplained. The rebuilt player passed native tests and maintainer validation:
racing with GPU Thread On, isolated installation without old profiles, creating
and saving a new profile, ISO persistence and profile recovery after restarting.
Performance can vary, especially in 3D. No fixed frame-rate guarantee is made.

Credits and distribution
Based on the PC port and ps2-port-engine by Daniel Gomes Vieira and contributors.
PAL adaptation by JMPliskin, with programming, reverse-engineering, testing and
debugging assistance from OpenAI ChatGPT and Codex. The upstream project also
uses AI-assisted tools. Development decisions and gameplay validation are
directed and reviewed by the project maintainer. Retain LICENSE and
THIRD-PARTY-NOTICES.txt and all other dependency notices.
The port's MIT license does not license Burnout 3's original code or assets.
This EXE includes code translated from the game. Supplying an ISO at runtime
does not itself establish redistribution rights. No authorization from the
original game's rights holders is claimed. Do not redistribute the game ISO.
Unofficial; not affiliated with Electronic Arts or Criterion Games.
