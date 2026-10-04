/*
 * The GS on the GPU (engine/docs/GPU.md, stage two): primitives the host's GS
 * model has assembled, drawn with Vulkan into render targets that stand for
 * regions of the GS's local memory.
 *
 * Drawn so far: triangles - flat or Gouraud, scissored - textured or not, with
 * the GS's four texture functions, its alpha test and its blending, into 32-bit
 * targets at the native resolution, which read back for testing against the
 * software GS (engine/host/src/gs_draw.cpp). Textures arrive decoded: the host
 * reads them out of local memory (swizzle, CLUT, TEXA) into RGBA and names
 * them with a key of its choosing. Depth and the destination alpha test are
 * next.
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
};

/* TEX0.TFX: how the texel and the vertex colour combine. */
enum class TextureFunction : std::uint32_t { Modulate = 0, Decal = 1, Highlight = 2, Highlight2 = 3 };

/* ALPHA's operands: Cv = (A - B) * C / 128 + D. */
enum class BlendColour : std::uint32_t { Source = 0, Dest = 1, Zero = 2 };
enum class BlendAlpha : std::uint32_t { Source = 0, Dest = 1, Fix = 2 };

/* The state a batch of triangles is drawn with. */
struct GsDrawState {
    std::uint32_t fbp = 0, fbw = 1, psm = 0;            /* FRAME: the target */
    std::uint32_t width = 640, height = 448;            /* how much of it to make */
    std::int32_t scissor_x0 = 0, scissor_y0 = 0, scissor_x1 = 639, scissor_y1 = 447;   /* inclusive */
    bool gouraud = true;                                 /* else the last vertex's colour */

    /* Texturing (PRIM.TME): a texture uploaded under this key. */
    bool textured = false;
    std::uint64_t texture = 0;
    TextureFunction tfx = TextureFunction::Modulate;
    bool tcc = true;                                     /* TEX0.TCC: the texel's alpha counts */
    bool bilinear = false;                               /* TEX1.MMAG */
    bool repeat_s = true, repeat_t = true;               /* CLAMP.WMS/WMT: repeat, else clamp */

    /* The alpha test (TEST.ATE, ATST, AREF, AFAIL). What a failing pixel
       writes: AFAIL 0 KEEP nothing, 1 FB_ONLY its colour (not depth), 2
       ZB_ONLY no colour, 3 RGB_ONLY its colour without alpha (taken as
       FB_ONLY for now). */
    bool alpha_test = false;
    std::uint32_t atst = 1, aref = 0, afail = 0;         /* ATST 1 is ALWAYS */

    /* Blending (PRIM.ABE) with ALPHA's operands and FIX. */
    bool blend = false;
    BlendColour a = BlendColour::Source, b = BlendColour::Dest, d = BlendColour::Dest;
    BlendAlpha c = BlendAlpha::Source;
    std::uint32_t fix = 0x80;
};

class GsRenderer {
public:
    virtual ~GsRenderer() = default;
    /* A texture, RGBA in memory (alpha as the GS has it, 0x80 is 1.0), rows
       top-down. Replaces what the key named. */
    virtual void UploadTexture(std::uint64_t key, const std::uint32_t* pixels, std::uint32_t width,
                               std::uint32_t height) = 0;
    /* Triangles, three vertices each, in GS order. */
    virtual void DrawTriangles(const GsDrawState& state, const GsVertex* vertices, std::size_t count) = 0;
    /* Runs what has been drawn. */
    virtual void Flush() = 0;
    /* A target's pixels, RGBA in memory, rows top-down; false if there is none. */
    virtual bool ReadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                            std::vector<std::uint32_t>* pixels, std::uint32_t* width,
                            std::uint32_t* height) = 0;
};
std::unique_ptr<GsRenderer> CreateGsRenderer(Gpu& gpu, std::string* error);

}  // namespace ps2gpu

#endif  /* PS2GPU_GS_RENDERER_H */
