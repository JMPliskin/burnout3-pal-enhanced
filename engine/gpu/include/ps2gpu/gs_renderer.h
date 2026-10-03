/*
 * The GS on the GPU (engine/docs/GPU.md, stage two): primitives the host's GS
 * model has assembled, drawn with Vulkan into render targets that stand for
 * regions of the GS's local memory.
 *
 * This first step draws triangles - flat or Gouraud, scissored - into 32-bit
 * targets at the native resolution, and reads them back, which is how it is
 * tested against the software GS. Textures, blending, the alpha, depth and
 * destination alpha tests follow in that order.
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
   at whole pixel coordinates), colour 0-255 (0x80 is 1.0 for modulation). */
struct GsVertex {
    float x, y;
    std::uint32_t z;
    std::uint8_t r, g, b, a;
    float s, t, q;
};

/* The state a batch of triangles is drawn with. */
struct GsDrawState {
    std::uint32_t fbp = 0, fbw = 1, psm = 0;            /* FRAME: the target */
    std::uint32_t width = 640, height = 448;            /* how much of it to make */
    std::int32_t scissor_x0 = 0, scissor_y0 = 0, scissor_x1 = 639, scissor_y1 = 447;   /* inclusive */
    bool gouraud = true;                                 /* else the last vertex's colour */
};

class GsRenderer {
public:
    virtual ~GsRenderer() = default;
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
