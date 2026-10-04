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
* `ps2gpu_tests` checks each of these by reading pixels back.

## Order of work

1. The renderer interface in the host, with the software GS as one backend.
2. Vulkan targets, the batcher and a flat-shaded pipeline: the frontend's 2D.
3. Textures and the cache; CLUTs.
4. Blending, alpha and depth tests, the destination alpha test.
5. Upscaling, widescreen (a wider projection where the game's matrices allow).
