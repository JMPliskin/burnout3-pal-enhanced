Burnout 3: Takedown - PC port, version {version}
=================================================

An unofficial, fan-made native Windows port of Burnout 3: Takedown (PS2,
NTSC-U). The game's PS2 program was translated to C++ and compiled for the
PC; it runs on your computer directly, not in an emulator.

This download contains NO game content. You need your own copy of the
game: an ISO disc image you made from your own Burnout 3: Takedown disc.
Nothing of the game - no textures, sounds, music, movies or data - is
included, and none is downloaded: everything comes from your disc image.

Not affiliated with, endorsed by or sponsored by Electronic Arts or
Criterion Games. "Burnout" is a trademark of Electronic Arts Inc.

This is an ALPHA: expect bugs.


What you need
-------------

* Windows 10 or 11, 64-bit
* A processor with AVX2 (Intel Haswell / AMD Ryzen or newer)
* A graphics card with a Vulkan 1.3 driver (NVIDIA GTX 900 series, AMD
  RX 400 series, Intel UHD 600 series or newer, with current drivers)
* Your own disc image of Burnout 3: Takedown, the North American (NTSC-U)
  release: SLUS-21050. Other regions are not supported yet.


Getting started
---------------

1. Unzip this folder somewhere you can write to (your Documents or Games
   folder - not Program Files): the game keeps its saves and settings
   beside burnout3.exe.
2. Run burnout3.exe.
3. The first time, it asks for your disc image: choose your .iso. It
   checks that it is the right disc and remembers it.

The game plays in a window or fullscreen (Alt+Enter switches). In
fullscreen on a 144 Hz screen it switches to 120 Hz, so that the game's 60
frames a second show evenly.


Controls
--------

A game controller (Xbox, PlayStation, most others SDL knows) works as a
DualShock 2. Rumble is supported.

Keyboard:
  Arrow keys        D-pad            Enter       Start
  X                 Cross            Backspace   Select
  C                 Circle           Q / E       L1 / R1
  Z                 Square           1 / 3       L2 / R2
  V                 Triangle

Mouse, in the menus: point at an item to highlight it, click to choose it,
right-click to go back, wheel to move up and down. The buttons along the
bottom of the screen can be clicked too.


PC settings
-----------

In the main menu, PC SETTINGS (where ONLINE used to be - EA's servers are
long gone) sets: fullscreen, vertical sync, frame pacing, the internal
resolution (original, twice as sharp, or as sharp as your screen - up to
4x), the speed blur, and the aspect ratio (4:3, 16:9 or your window's).
QUIT GAME is in the main menu too.


Files beside burnout3.exe
-------------------------

  pc_settings.ini   your settings, and where your disc image is
  memcard\          your memory card: the game's saves
  burnout3.log      what the last run did (useful in a bug report)

To choose a different disc image, delete the "disc=" line from
pc_settings.ini.


Known issues
------------

* Only the NTSC-U release (SLUS-21050) runs.
* Online play is not available.
* A heavy moment in a race can still show a picture a frame late.
* Some menus (the name keyboard, the map, the car selection) take the
  mouse's wheel, right button and bottom-bar buttons, but not pointing yet.


Licences
--------

The port uses SDL 3, volk, glslang and the Vulkan headers: their licences
are in THIRD-PARTY-NOTICES.txt.
