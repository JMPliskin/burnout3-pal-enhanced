Burnout 3 PAL Enhanced 0.1.0-alpha - local player test

Experimental Windows x64 player. Requires Windows 10 version 1903 or newer,
an AVX2-capable CPU and a Vulkan-capable GPU/driver. Audio/controller support
comes from the shared host. No separate Python, BAT or MSVC runtime is needed.

Extract the ZIP to a normal local folder. Double-click Burnout3-PAL-Enhanced.exe.
Choose your own Burnout 3 PAL ISO, boot executable SLES_525.84. NTSC-U is rejected.
Cancel exits cleanly. The disc path is remembered; if missing, select it again.
No disc, extracted executable, modules or game assets are included.

Player data: %LOCALAPPDATA%/Burnout3-PAL-Enhanced/
  pc_settings.ini: disc path and settings
  memcard/: persistent saves
  logs/run-*/: exclusive error log and the existing PAL relocation audit
Do not delete this directory when replacing the EXE. Existing development cards
are not automatically overwritten or imported. To import your own profile,
close the game and copy it into memcard only after making a private backup.

First-run defaults: Vulkan GPU, audio, 2x, 16:9, Frame Pacing On, V-Sync/AA Off.
The original PAL language and 50/60 Hz screens are preserved. F9 opens shared
settings. Advanced host CLI options remain available when arguments are supplied.

GPU Thread defaults On. This build retains diagnostic instrumentation even with
logging absent. Earlier GPU Thread freezes remain unexplained; this is not a
certified public Release. User testing passed ISO selection, path persistence,
renamed-disc recovery, direct EXE startup and a complete race with GPU Thread On.
A clean installation and memory-card location without pre-existing profiles
still require validation before public distribution. An older profile loaded
in development testing; its provenance has not been independently confirmed.

Manual acceptance: choose ISO, load/create profile, complete a race, close and
reopen. Move/rename your ISO temporarily without changing its contents, then
reselect it; verify settings and profile survive. Test cancellation and a wrong
region image. Never run two instances against the same writable card.

Based on burnout3-pc by Daniel Gomes Vieira and ps2-port-engine. See LICENSE and
THIRD-PARTY-NOTICES.txt. Unofficial; not affiliated with EA or Criterion Games.
