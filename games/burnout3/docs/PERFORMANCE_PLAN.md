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
