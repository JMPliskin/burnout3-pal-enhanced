# Performance and frame pacing: the plan

Goal (2026-10-10): better than a PS2 emulator on the same machine, and **no
dropped frame in gameplay** - every one of the race's 60 pictures a second
shown on time.

## How it is measured

* **The profiler** - `PS2HOST_PROFILE=file` (`ps2host/timeline.h`): every
  thread's work (the EE, the VIF1 worker, the GS thread, the present thread)
  and every picture's way to the screen, as a timeline (Chrome's trace
  format: ui.perfetto.dev), and a summary at the end: the present intervals'
  percentiles, the frames dropped or late, and for each late one what was
  busy before it. `engine/tools/timeline.py` lists every thread's events
  around a moment (`at`) or the longest spans of each kind (`longest`).
* **The benchmark** - the race script (`port/runs/race.args`) under
  `--virtual-time`, exactly to the race, then `--real-time-from 18000`: the
  race played in real time, windowed, at a fixed 3x resolution (1920 x 1344,
  heavier than 1080p), sound on, the driving extended to frame 21400 and
  profiled from 18600 - 2,800 frames, 47 seconds of racing.
* **Dropped** means any of: a picture dropped before it was shown, a present
  interval past 1.5 frames (the last picture shown twice), a vertical blank
  the EE was too busy for (the game itself loses a step).

## Where it stands (the baseline, 2026-10-10)

The race at 3x on an RTX 4070 Laptop: 2,801 pictures in 2,801 blanks, none
dropped, none lost - but **3 shown late** (37.4, 29.5, 25.3 ms), and the
interval's 99th percentile 20.5 ms (jitter 1-2 ms around 16.7).

| thread | busy | what |
| --- | --- | --- |
| EE | ~38% | the game (translated code), the IOP, the SPU2 |
| VIF1 worker | 50-73% | VU1's vertex programs (compiled), VIF1 |
| GS thread | 40-72% | the GS model, the GPU bridge (draws, textures, memory) |
| GPU | 30-46% | drawing at 3x |

The late frames: the GS thread stopped 23 ms in "GPU and memory in step"
(the CPU waiting for the GPU to copy memory back) at the worst; VU1 ~33 ms
of work in the same moments (a crash's debris and cars on screen).

## Done so far

* **The CPU no longer waits for the GPU when a transfer writes part of a
  block the GPU drew last.** The race's CLUT uploads (8 x 3 and 16 x 4
  pixels, into blocks the narrow 64 x 512 targets' write-backs left the
  GPU's) brought each block back first - 228 times in the race, 10-14 ms
  each, the GPU's whole queue waited for. Now the transfer's pixels are
  patched into the GPU's memory once it is done (a compute shader, eight
  masked words a dispatch), the blocks staying the GPU's: none brought back.
* **The benchmark's pacing.** After `--real-time-from` the host's clock runs
  on from the virtual one; FrameDue compared it with the blanks' steady
  clock and paced nothing - most of the "late" frames measured first were
  that. FrameDue keeps to the steady clock now (play was not affected).

With both: the race at 3x, **2,800 pictures, none late**, interval median
16.69 ms, 99% 18.1, longest 21.9, jitter 0.45 ms; a frame's way from its
blank to the screen 1.5 ms median, 10.5 at most. Per frame, the GS thread
10.2 ms median (15.2 at the 99th percentile), VIF1's worker 10.5 (15.8):
the headroom that is left.

The player's screen (2560 x 1440 at 144 Hz): "display" resolution is 3x,
as measured; fullscreen goes to 120 Hz (EvenMode), so 60 pictures a second
each show for two refreshes. In a window the desktop's 144 Hz can't show
60 evenly - a window shows a picture for two refreshes, then three.

Then (8471128, 429353d, 409a126), each found as a picture shown late:

* the blanks' timer and the present thread woke late with every core busy
  (a blank 13 ms late): the highest priority, high-resolution waitable
  timers; Windows 11 dropping the timer resolution for a hidden window
  (a 200 us sleep took 15 ms): the process asks to keep it;
* the present thread waited for a swapchain image at the picture's time:
  acquired ahead now;
* FrameDue: a 6 ms floor, and two seconds' history (heavy moments come
  together);
* image memory sub-allocated from 64 MB blocks, the texture cache 4,096:
  a new stretch of track no longer goes to the kernel for each texture.

Now, at 3x: the race 0-1 frames late in 2,800 (99% 17.1 ms, 99.9% 18.7-19.2),
the menus 0 late in 4,400 (two runs). The one left: the race's frame ~20840,
the GS thread's own work there (texture uploads and their decoding, ~25 ms
in one frame).

## The plan, in order of what drops frames first

1. **No CPU wait for the GPU in a frame.** Find every download in the race
   (the profiler names them: a texture, a read, a write) and remove its
   cause: textures read from targets decoded on the GPU (they are, where
   the GPU holds the blocks - find the ones that are not), CPU reads of GPU
   memory answered later or not at all, the CLUT loads' syncs.
2. **Spikes of GPU bookkeeping.** New textures and images in a crash (first
   use: decode, upload, pipeline creation) - spread out, cached, or made
   ahead (pipelines at boot from a list, as the VU programs are compiled).
3. **VU1 headroom.** The worker is at 50-73% with spikes past a frame:
   flag liveness per block (not per program) so the exact mode's flag work
   drops where nothing reads it; the hottest programs' code (program 26 a
   third of the time); split the frame's VU1 work over two workers.
4. **The GS thread's headroom.** The GS model per primitive (the bridge's
   state compare, vertex conversion) - the sampler (`--sample-gs`) says
   where.
5. **Pacing.** The present thread wakes by sleep and yield: a waitable
   timer (high resolution) and the swapchain's present mode (mailbox or
   FIFO by the vsync setting) - the jitter's 1-2 ms down; frames presented
   at the vertical blank's due time, not when ready.
6. **Elsewhere in gameplay.** The same measure on the other modes - crash
   mode, road rage, the car select, the race's start (the loading screen's
   end) - and their spikes.
7. **Against PCSX2.** The same race in PCSX2 at the same resolution on the
   same machine: its frame rate and its frame times (its own on-screen
   statistics), beside the port's.

Each step: the benchmark before and after (the summary's late frames and
percentiles), the regressions (car_select, race_training, race) as
recorded.

## Low-power machines (2026-10-16/17)

Goal: smooth on low-end PCs - few cores, slow cores, weak GPUs.

**How it is measured.** The same race benchmark, at the PS2's own
resolution (what a weak machine runs), the process held to a few cores from
its start with `PS2HOST_CORES` (a count, or a mask: on the i7-13620H used,
logical cores 0-11 are six hyperthreaded pairs, so `PS2HOST_CORES=4` is two
physical cores with hyperthreading - a low-end i3 - and `0x5` two physical
cores without - a Celeron or Pentium Silver). A process held to fewer cores
only after it started is not the same: the GPU driver's thread pools were
sized for sixteen, and every result measured so was wrong. Optionally a
"burner" takes a share of each core at a higher priority (a slower CPU;
imprecise with hyperthreading). Per run: frames dropped, blanks lost, late
presents, the interval's percentiles, and the process's CPU (cores used).

**Where it stood.** Two physical cores: 163 frames dropped, 2,390 blanks
lost, the game stopping for up to 4.8 seconds at a time.

**What fixed it.**

| change | two cores | sixteen |
| --- | --- | --- |
| before | 163 dropped, 2,390 lost, longest 4,774 ms, 1.52 cores | 0 late |
| the vertical blank timer no longer spins on a timer interrupt it marked due and the EE has not taken yet (it ran at time-critical priority, holding a core the EE needed) | 0-2 dropped, 0-3 lost, 41-44 late, longest 67-113 ms, 1.40 cores | 0 late, 1.44 cores |
| spinning to a deadline and the game threads above normal only with 4+ logical cores (with 2: 1,118 dropped) | (included above) | unchanged |
| VU1 fast mode by default (exact pictures; the scripted race 102.8 -> 98.3 s, VU1 29.2% -> 25.6%) | | |
| SPU2: a voice heard nowhere skips its interpolation (WAV byte-identical; SPU2 5.5% -> 5.3%) | | |

Two physical cores with hyperthreading (`PS2HOST_CORES=4`): 0 dropped, 5
late in 2,800 (11 without the spin). With a burner taking about half of
every core as well: 0 dropped, 2-3 late.

What is left on two cores: in heavy moments (crashes, debris) VU1, the GS
thread and the EE together need more than two cores for a few frames - the
41 late presents. Per frame on the fast CPU, at the PS2's resolution: VIF1's
worker ~49% busy (89% of that VU1's compiled code, spread over many blocks),
the GS thread ~44% (25% drawing, ~25% Vulkan recording and the driver), the
EE ~37% (a third of it the SPU2). Measured and found not to matter: the GS
thread's spin before it sleeps (1.46 vs 1.44 cores), the speed blur (2-3%
of the GPU), the 1024 x 1024 depth texture decoded each frame (no change).

The GPU at the PS2's resolution: about 5.5 ms a frame on an RTX 4070 laptop
(draws 55%, swizzles 20%, decodes 15%): ~1,870 draws in ~216 render passes,
~410 barriers, ~200 dispatches a frame - each texture decode goes through a
staging buffer and a copy between full barriers. An integrated GPU will feel
that first; the next lever there is fewer passes and barriers (decodes
written straight into their images, batched).
