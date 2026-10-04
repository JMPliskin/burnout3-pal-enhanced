# The GPU renderer: plan

The port draws with a software GS today (`engine/host/src/gs*.cpp`). It is exact
enough to be the reference, and slow and native-resolution only. The GPU renderer
replaces it for play and for the enhancements (higher resolution, widescreen,
higher frame rates), and is checked against it.

## Decisions

* **Vulkan**, behind the `ps2gpu` library (`engine/gpu`). One backend reaches
  Windows, Linux and Android natively and macOS through MoltenVK. It gives what
  GS emulation needs and portable wrappers do not: blending the fixed-function
  units cannot express (done in the fragment shader with framebuffer fetch or
  feedback loops), the destination alpha test, sampling from what was just
  drawn, and 16- and 24-bit colour and depth.
* **SDL3** for the window, input, events and later audio, on every platform.
  The Win32/GDI window stays as the fallback (`window.cpp`).
* Dependencies come from their releases by CMake FetchContent (Vulkan-Headers,
  volk, SDL3; glslang next), so nothing is installed and nothing of theirs is
  committed.
* The software GS stays: it is the reference the GPU renderer is tested against,
  frame by frame, and the fallback without a GPU.

## Stage one (done): presentation

`ps2gpu::Display` / `CreateVulkanDisplay`: an SDL3 window and a Vulkan swapchain.
Each frame the software GS's picture is copied to a staging buffer and an image
and blitted, 4:3 and letterboxed, to the swapchain image. MAILBOX present mode
where available, so presenting never blocks the emulation thread. Built into the
host with `-DPS2HOST_VULKAN=ON`; `--window` then uses it, and the keyboard is
read through SDL. `ps2gpu_smoke` checks a device, a swapchain and 120 frames.

## Stage two: the GS on the GPU

The GS draws into its 4 MB local memory, which games also read back as textures
(render-to-texture, the frame copies Burnout 3 does every frame from 0x8C0 to
the CT16S display buffer), upload into and copy within. The design follows what
proved itself in PCSX2's hardware renderers, made smaller:

1. **Draw batching.** `Gs::Draw` hands primitives to a renderer interface instead
   of rasterising: vertices in screen space (after XYOFFSET), with the state
   that drew them - PRIM, TEX0/TEX1/CLAMP, ALPHA, TEST, FBA, FRAME, ZBUF,
   SCISSOR, DTHE, COLCLAMP, PABE. A batch runs while the state is unchanged.
2. **Render targets.** A FRAME (FBP, FBW, PSM) is a Vulkan colour image at N x
   the native resolution; a ZBUF a depth image. They are looked up by base
   pointer and format, and created on first use.
3. **Textures.** A TEX0 is looked up in a texture cache keyed by (TBP, TBW, PSM,
   size, CLUT). A miss decodes the region from local memory (the swizzle and the
   CLUT, as `gs_memory.cpp` does) into an RGBA8 image; uploads (`Transfer`) and
   local copies invalidate the pages they touch. A TEX0 that is a render target
   samples the target's image directly - no round trip through local memory.
4. **Shaders.** One vertex shader; one fragment shader specialised (specialisation
   constants) by texture function, alpha test, destination alpha test, fog and
   blending mode. Blending modes the fixed-function blender expresses use it;
   the others read the target in the shader (VK_EXT_rasterization_order_
   attachment_access where present, else a copy). Shaders are GLSL in
   `engine/gpu/shaders`, compiled to SPIR-V at build time by glslang (fetched
   like the others) and embedded.
5. **Read-back.** Where the EE reads local memory (an IMAGE transfer from the
   GS, rare) or a texture covers a target in another format, the target is
   written back to local memory first.
6. **Display.** DISPFB/DISPLAY pick the target that is shown; it is blitted to
   the swapchain as stage one does. PMODE's two circuits and their blending come
   later.

## Testing

* Per frame, both renderers draw the same GIF stream; the GPU frame read back at
  1x is compared with the software frame (`--frames` writes both). Differences
  above a threshold fail; the first differing primitive is found by bisecting
  the stream.
* Burnout 3's frontend - the LOADING screen, menus, the title - are the first
  test scenes, then VU1's 3D.

## Progress

* `GsRenderer` draws triangles - flat or Gouraud, scissored - into R8G8B8A8
  targets keyed by (FBP, FBW, PSM), and reads them back.
* Textures arrive decoded (`UploadTexture`, RGBA with the GS's alpha, under
  a key the host chooses); the fragment shader applies the four texture
  functions with TCC, sampling nearest or bilinear, repeating or clamped.
* The alpha test (ATST, AREF; AFAIL KEEP).
* Blending: `Cv = (A - B) * C + D` maps onto the fixed-function blender as a
  source and destination factor and an operation; C (source alpha or FIX
  over 128) is the fragment's second output by dual-source blending, so the
  alpha written stays the source alpha. One pipeline per ALPHA setting, made
  on first use. Coefficients the blender cannot reach (1 + C, factors over
  1.0, the destination alpha over 128) are approximated.
* The alpha test's AFAIL: FB_ONLY and RGB_ONLY write colour (without depth
  on the GPU yet they are the same), KEEP and ZB_ONLY discard.
* Depth: a D32_SFLOAT buffer per ZBP (and one per target for draws that test
  nothing, since dynamic rendering keeps one attachment layout), holding
  Z / 2^32 - exact for Z24 and Z16, rounded past 24 bits for Z32. ZTST NEVER,
  ALWAYS, GEQUAL, GREATER and ZMSK are dynamic state (Vulkan 1.3), not
  pipelines. `UploadDepth` loads a buffer from local memory's Z; the shadow
  mode does so for each buffer at its first use in a frame.
* AFAIL exactly: a batch whose alpha test can fail draws twice - the passing
  pixels with everything, then the failing ones with what AFAIL keeps
  (FB_ONLY colour, ZB_ONLY depth, RGB_ONLY colour without alpha), the shader
  told which pixels each pass keeps.
* FBMSK as the colour write mask, per byte; fog (FGE, FOGCOL) in the shader.
* The destination alpha test: depth buffers are D32_SFLOAT_S8_UINT; before a
  DATE batch a setup pass (shaders/date.*) samples the target and sets the
  stencil where its alpha bit equals DATM, and the batch draws with a stencil
  EQUAL test.
* Not yet checked against the Vulkan validation layers (not installed on the
  machine this was written on); the results are what the tests check.
* `ps2gpu_tests` checks each of these by reading pixels back.
* **The host drives it**: `--gpu-shadow` (engine/host/src/gpu_bridge.cpp)
  hands every primitive the software GS draws to the renderer as well - its
  FRAME, SCISSOR, TEX0/TEX1/CLAMP, TEST and ALPHA, sprites as two triangles -
  with textures decoded from the software GS's local memory (decoded again
  when their registers change, an upload or local copy happens, or a draw
  goes into their blocks). Each frame, every target drawn into is read back
  and compared with local memory. Burnout 3's frontend, boot to title screen
  and menus, 3,855 frames: 100.0% of pixels agree (within 2 per channel, 8 for
  16-bit formats). The game's frame copy - sprites textured from the frame
  at 0x8C0 into the CT16S display buffer, drawn with ATST NEVER and AFAIL
  FB_ONLY - is what found AFAIL.

## Order of work

1. ~~The renderer interface in the host, with the software GS as one backend~~
   (`Gs::on_draw`, `--gpu-shadow`).
2. ~~Vulkan targets, the batcher and a flat-shaded pipeline~~.
3. ~~Textures~~ (decoded by the host); the texture cache and CLUTs on the GPU
   come with step 6.
4. ~~Blending, alpha and depth tests, fog, FBMSK~~; the destination alpha test
   ~~(DATE, through a stencil pass: the target's alpha bit copied into
   stencil, then the draw tested against it)~~, ~~PABE (a second pass)~~.
5. Upscaling, widescreen (a wider projection where the game's matrices allow).
   With these comes a settings front end: internal resolution and the other
   display options set there are saved (a settings file beside the
   executable) and loaded on every run, command-line options overriding them
   - settings must persist across runs.
6. ~~**The GPU as the renderer**~~ (`--gpu`, engine/host/src/gpu_bridge.cpp):
   the GS model still takes every packet, register and transfer but draws
   nothing (`Gs::rasterize` false); the GPU draws, and its images stand for
   regions of local memory. Each image (a target or a depth buffer) knows the
   blocks it covers - the rows drawn so far, not all the scissor allows - and
   whether the GPU has drawn what memory lacks or memory changed under it.
   * *read-backs*: before the GS model reads or writes local memory other
     than by drawing (an image transfer either way, a local copy, a CLUT
     load, the picture shown) it calls `Gs::on_memory` with the blocks, and
     the GPU images over them are written back (`ReadTarget`, `ReadDepth`,
     stored in the GS's swizzled layout);
   * *uploads*: memory written under an image makes it stale; it reloads
     from memory (`UploadTarget`, `UploadDepth`) before the GPU next uses it;
   * *aliasing*: an image drawn over another's blocks under another width or
     format (Burnout 3's reflection buffers at 0x16B-0x1A7, its depth buffer
     at 0xD2 reused for 64x32 to 320x224 scratch targets) is written back and
     the other reloaded - once a frame, not once a draw;
   * *targets as textures*: a TEX0 whose TBP and format match a target's
     samples the target's image;
   * *other textures*: decoded by the host into a cache keyed by
     TBP/TBW/PSM/TW/TH, CLAMP's region modes, TEXA and the CLUT's contents,
     invalid once memory under them is written or the GPU draws over them,
     the least recently used of 2,048 released;
   * *lines and points*: one-pixel bands that cover what the software GS
     plots (the position rounded, both ends);
   * primitives wholly outside the scissor are dropped before any of this -
     the garage sends thousands a frame to FRAME 0/FBW 1, whose colour and Z
     alias one page.
   Checked by running the same scripted input (`--virtual-time`) under both
   renderers and comparing the pictures with `engine/tools/framediff.py`:
   `car_select.args`, 40 pictures from boot to the 3D garage, 99.85% of
   pixels agree on average (within 8 per channel); the garage 99.96%, the
   menus 99.9-100%. The run takes 5m15s against the software GS's 9m07s.
   Left: a text-entry screen's highlight boxes (99.42%), the globe's light
   beams (99.25%).
   Local memory stays the meeting point: the shadow mode's comparison, run on
   a few frames, checks each step.
