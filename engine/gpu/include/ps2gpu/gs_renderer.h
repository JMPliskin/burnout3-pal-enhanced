/*
 * The GS on the GPU (engine/docs/GPU.md, stage two): primitives the host's GS
 * model has assembled, drawn with Vulkan into render targets that stand for
 * regions of the GS's local memory.
 *
 * Drawn so far: triangles - flat or Gouraud, scissored - textured or not, with
 * the GS's four texture functions, its alpha test, its blending and its depth
 * test, into 32-bit targets at the native resolution, which read back for
 * testing against the software GS (engine/host/src/gs_draw.cpp). Textures arrive decoded: the host
 * reads them out of local memory (swizzle, CLUT, TEXA) into RGBA and names
 * them with a key of its choosing. Depth is a 32-bit float buffer holding
 * Z / 2^32. The destination alpha test, fog and FBMSK are next.
 *
 * Colours are in the GS's terms throughout: 0-255 per channel, with 0x80 as
 * 1.0 where the GS multiplies (vertex colour against texture, alpha as a blend
 * factor).
 *
 * Gpu is the device without a window (tests, off-screen work); a Display's
 * device can stand in for it later.
 */

#ifndef PS2GPU_GS_RENDERER_H
#define PS2GPU_GS_RENDERER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ps2gpu {

/* A Vulkan instance and device with one graphics queue (Vulkan 1.3: dynamic
   rendering). Opaque; created headless. */
class Gpu {
public:
    virtual ~Gpu() = default;
    virtual std::string Describe() const = 0;
};
std::unique_ptr<Gpu> CreateGpu(std::string* error);

/* A vertex as the GS has it after XYOFFSET: position in pixels (the GS samples
   at whole pixel coordinates), colour 0-255, and the texture coordinate as
   S/Q, T/Q over the texture's size (with UV, the host passes u / 16 / width,
   v / 16 / height and q = 1). */
struct GsVertex {
    float x, y;
    std::uint32_t z;
    std::uint8_t r, g, b, a;
    float s, t, q;
    std::uint8_t fog = 0xFF;            /* F: 255 keeps the colour, 0 is all FOGCOL */
};

/* TEX0.TFX: how the texel and the vertex colour combine. */
enum class TextureFunction : std::uint32_t { Modulate = 0, Decal = 1, Highlight = 2, Highlight2 = 3 };

/* ALPHA's operands: Cv = (A - B) * C / 128 + D. */
enum class BlendColour : std::uint32_t { Source = 0, Dest = 1, Zero = 2 };
enum class BlendAlpha : std::uint32_t { Source = 0, Dest = 1, Fix = 2 };

/* The state a batch of triangles is drawn with. */
struct GsDrawState {
    std::uint32_t fbp = 0, fbw = 1, psm = 0;            /* FRAME: the target */
    std::uint32_t fbmsk = 0;                             /* FRAME.FBMSK: bits not written */
    std::uint32_t width = 640, height = 448;            /* how much of it to make */
    std::int32_t scissor_x0 = 0, scissor_y0 = 0, scissor_x1 = 639, scissor_y1 = 447;   /* inclusive */
    bool gouraud = true;                                 /* else the last vertex's colour */

    /* Texturing (PRIM.TME): a texture uploaded under this key. */
    bool textured = false;
    std::uint64_t texture = 0;
    TextureFunction tfx = TextureFunction::Modulate;
    bool tcc = true;                                     /* TEX0.TCC: the texel's alpha counts */
    bool bilinear = false;                               /* TEX1: the four texels around */
    /* CLAMP: WMS/WMT 0 REPEAT, 1 CLAMP, 2 REGION_CLAMP (to MIN..MAX), 3
       REGION_REPEAT (MIN is a mask, MAX ORed in). */
    std::uint32_t wms = 0, wmt = 0, minu = 0, maxu = 0, minv = 0, maxv = 0;
    std::uint32_t tw = 0, th = 0;                        /* TEX0's size (0: the texture's own) */
    /* ...or a target sampled as the texture (render to texture): TEX0's TBP is
       the target's FBP * 32 and TBW its FBW. 24- and 16-bit texture formats
       read it as the GS would - TEXA's alpha, 16-bit colour to 5 bits. */
    bool texture_target = false;
    std::uint32_t tex_fbp = 0, tex_fbw = 0, tex_target_psm = 0;
    std::uint32_t tex_format = 0;                        /* 0 32-bit, 1 24-bit, 2 16-bit */
    std::uint32_t ta0 = 0, ta1 = 0x80;                   /* TEXA */
    bool aem = false;

    /* The alpha test (TEST.ATE, ATST, AREF, AFAIL). What a failing pixel
       writes: AFAIL 0 KEEP nothing, 1 FB_ONLY its colour (not depth), 2
       ZB_ONLY its depth (not colour), 3 RGB_ONLY its colour without alpha
       (not depth) - drawn as a second pass over the failing pixels. */
    bool alpha_test = false;
    std::uint32_t atst = 1, aref = 0, afail = 0;         /* ATST 1 is ALWAYS */

    /* The destination alpha test (TEST.DATE, DATM): a pixel is drawn only
       where the target's alpha bit (its alpha's top bit) equals DATM. */
    bool date = false;
    std::uint32_t datm = 0;

    /* Blending (PRIM.ABE) with ALPHA's operands and FIX. */
    bool blend = false;
    BlendColour a = BlendColour::Source, b = BlendColour::Dest, d = BlendColour::Dest;
    BlendAlpha c = BlendAlpha::Source;
    std::uint32_t fix = 0x80;
    bool pabe = false;                                   /* PABE: blend only where As >= 0x80 */

    /* Fog (PRIM.FGE): C = (F * C + (255 - F) * FOGCOL) / 256, colour only. */
    bool fog = false;
    std::uint32_t fogcol = 0;                            /* FOGCOL: R, G, B in bytes 0-2 */

    /* Depth (ZBUF, TEST.ZTE/ZTST): a depth buffer named by ZBP, as large as
       the target. ZTST 0 NEVER, 1 ALWAYS, 2 GEQUAL, 3 GREATER - the GS keeps
       the larger Z. Without `depth` nothing is tested or written. */
    bool depth = false;
    std::uint32_t zbp = 0, ztst = 1;
    bool depth_write = true;                             /* !ZBUF.ZMSK */
};

/* A texture in GS local memory, and how its texels expand: TEX0's TBP0 (in
   blocks), TBW, PSM and size, CLAMP's modes and region, TEXA, CSA, and the
   CLUT as the GS model holds it (256 RGBA entries; palettized formats). */
struct GsTextureDecode {
    std::uint32_t tbp = 0, tbw = 1, psm = 0, tw = 1, th = 1;
    std::uint32_t wms = 0, wmt = 0, minu = 0, maxu = 0, minv = 0, maxv = 0;
    std::uint32_t texa_lo = 0;      // TA0, and AEM at bit 15
    std::uint32_t ta1 = 0, csa = 0;
    const std::uint32_t* clut = nullptr;
};

class GsRenderer {
public:
    virtual ~GsRenderer() = default;
    /* A texture, RGBA in memory (alpha as the GS has it, 0x80 is 1.0), rows
       top-down. Replaces what the key named. */
    virtual void UploadTexture(std::uint64_t key, const std::uint32_t* pixels, std::uint32_t width,
                               std::uint32_t height) = 0;
    /* A depth buffer's contents, one Z per pixel, rows top-down: what local
       memory holds there (the GS's Z, unsigned). */
    virtual void UploadDepth(std::uint32_t zbp, std::uint32_t width, std::uint32_t height,
                             const std::uint32_t* z) = 0;
    /* A target's contents from local memory, RGBA rows top-down (made, at
       this size, if there was none). */
    virtual void UploadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, const std::uint32_t* pixels,
                              std::uint32_t width, std::uint32_t height) = 0;
    /* Forgets a texture (after what is recorded has used it). */
    virtual void ReleaseTexture(std::uint64_t key) = 0;
    /* Whether a target exists for (FBP, FBW, PSM) - what a draw can sample. */
    virtual bool HasTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm) const = 0;
    /* Triangles, three vertices each, in GS order. */
    virtual void DrawTriangles(const GsDrawState& state, const GsVertex* vertices, std::size_t count) = 0;
    /* Runs what has been drawn. */
    virtual void Flush() = 0;
    /* GS local memory on the GPU: 4 MB, as the GS lays it out, which targets
       and depth buffers are swizzled into and out of without the CPU. Blocks
       are 256 bytes; [begin, end) in blocks. Nothing waits but MemoryRead. */
    /* The CPU's copy of blocks, into the GPU's. */
    virtual void MemoryWrite(std::uint32_t begin, std::uint32_t end, const std::uint8_t* bytes) = 0;
    /* The GPU's copy of blocks, into `bytes` (runs what is recorded, waits). */
    virtual void MemoryRead(std::uint32_t begin, std::uint32_t end, std::uint8_t* bytes) = 0;
    /* A target's first `rows` rows, `cols` wide, into GPU memory at FBP as
       its PSM lays them out (CT32, CT24, CT16, CT16S); false if there is none. */
    virtual bool TargetToMemory(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::uint32_t cols,
                                std::uint32_t rows) = 0;
    /* A target, made at this size if there is none, loaded from GPU memory. */
    virtual void TargetFromMemory(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::uint32_t width,
                                  std::uint32_t height) = 0;
    /* The same for the depth buffer at ZBP of this size, laid out by ZPSM
       (Z32, Z24, Z16, Z16S) FBW pages wide. */
    virtual bool DepthToMemory(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, std::uint32_t fbw,
                               std::uint32_t zpsm, std::uint32_t cols, std::uint32_t rows) = 0;
    virtual void DepthFromMemory(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, std::uint32_t fbw,
                                 std::uint32_t zpsm) = 0;
    /* A texture decoded from GPU memory, as the host's software GS decodes
       one (GsTextureDecode); replaces what the key named. */
    virtual void TextureFromMemory(std::uint64_t key, const GsTextureDecode& decode) = 0;
    /* A depth buffer's Z (the GS's, unsigned), rows top-down; false if there
       is none of that size. */
    virtual bool ReadDepth(std::uint32_t zbp, std::uint32_t width, std::uint32_t height,
                           std::vector<std::uint32_t>* z) = 0;
    /* A texture's texels, RGBA, rows top-down; false if there is none. */
    virtual bool ReadTexture(std::uint64_t key, std::vector<std::uint32_t>* pixels, std::uint32_t* width,
                             std::uint32_t* height) = 0;
    /* A target's pixels, RGBA in memory, rows top-down; false if there is none. */
    virtual bool ReadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                            std::vector<std::uint32_t>* pixels, std::uint32_t* width,
                            std::uint32_t* height) = 0;
};
std::unique_ptr<GsRenderer> CreateGsRenderer(Gpu& gpu, std::string* error);

}  // namespace ps2gpu

#endif  /* PS2GPU_GS_RENDERER_H */
