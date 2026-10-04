/*
 * The GPU GS renderer's tests: draw into a target, read it back, check pixels
 * as the software GS (engine/host/src/gs_draw.cpp) would have them.
 */

#include <cstdio>
#include <cstdlib>

#include "ps2gpu/gs_renderer.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

std::uint32_t Rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return std::uint32_t(r) | std::uint32_t(g) << 8 | std::uint32_t(b) << 16 | std::uint32_t(a) << 24;
}

/* Each channel within `slack` of the expected value: blending on the GPU
   rounds where the GS truncates. */
bool Near(std::uint32_t got, std::uint32_t want, int slack = 1) {
    for (int shift = 0; shift < 32; shift += 8) {
        const int g = int((got >> shift) & 0xFF), w = int((want >> shift) & 0xFF);
        if (g - w > slack || w - g > slack) return false;
    }
    return true;
}

ps2gpu::GsDrawState Target(std::uint32_t fbp) {
    ps2gpu::GsDrawState state;
    state.fbp = fbp;
    state.fbw = 1;
    state.width = 64;
    state.height = 64;
    state.scissor_x1 = 63;
    state.scissor_y1 = 63;
    return state;
}

/* A rectangle as two triangles, texture coordinates 0..1 across it. */
void Rect(ps2gpu::GsRenderer& r, const ps2gpu::GsDrawState& state, float x0, float y0, float x1, float y1,
          std::uint32_t rgba) {
    const std::uint8_t cr = rgba & 0xFF, cg = (rgba >> 8) & 0xFF, cb = (rgba >> 16) & 0xFF, ca = rgba >> 24;
    const ps2gpu::GsVertex v[6] = {
        {x0, y0, 0, cr, cg, cb, ca, 0, 0, 1}, {x1, y0, 0, cr, cg, cb, ca, 1, 0, 1},
        {x0, y1, 0, cr, cg, cb, ca, 0, 1, 1}, {x1, y0, 0, cr, cg, cb, ca, 1, 0, 1},
        {x1, y1, 0, cr, cg, cb, ca, 1, 1, 1}, {x0, y1, 0, cr, cg, cb, ca, 0, 1, 1}};
    r.DrawTriangles(state, v, 6);
}

std::uint32_t Pixel(ps2gpu::GsRenderer& r, std::uint32_t fbp, int x, int y) {
    std::vector<std::uint32_t> pixels;
    std::uint32_t w = 0, h = 0;
    if (!r.ReadTarget(fbp, 1, 0, &pixels, &w, &h) || pixels.size() != std::size_t(w) * h) return 0xDEADBEEF;
    return pixels[std::size_t(y) * w + x];
}

void Untextured(ps2gpu::GsRenderer& renderer) {
    ps2gpu::GsDrawState state = Target(0x10);
    state.gouraud = false;
    // A flat right triangle covering the top-left half of 0..32.
    const ps2gpu::GsVertex flat[3] = {{0, 0, 0, 0, 0, 0, 0x80, 0, 0, 1},
                                      {32, 0, 0, 0, 0, 0, 0x80, 0, 0, 1},
                                      {0, 32, 0, 0x40, 0x80, 0xFF, 0x80, 0, 0, 1}};
    renderer.DrawTriangles(state, flat, 3);
    // Scissored away entirely.
    ps2gpu::GsDrawState clipped = state;
    clipped.scissor_x0 = 60;
    clipped.scissor_y0 = 60;
    const ps2gpu::GsVertex hidden[3] = {{0, 40, 0, 0xFF, 0, 0, 0x80, 0, 0, 1},
                                        {20, 40, 0, 0xFF, 0, 0, 0x80, 0, 0, 1},
                                        {0, 60, 0, 0xFF, 0, 0, 0x80, 0, 0, 1}};
    renderer.DrawTriangles(clipped, hidden, 3);

    std::vector<std::uint32_t> pixels;
    std::uint32_t w = 0, h = 0;
    Check(renderer.ReadTarget(0x10, 1, 0, &pixels, &w, &h) && w == 64 && h == 64, "the target reads back");
    if (pixels.size() == 64 * 64) {
        Check(pixels[5 * 64 + 5] == Rgba(0x40, 0x80, 0xFF, 0x80), "flat shading takes the last vertex's colour");
        Check(pixels[40 * 64 + 40] == 0, "outside the triangle stays as local memory was (zero)");
        Check(pixels[45 * 64 + 5] == 0, "a scissored triangle draws nothing");
        Check(pixels[0] == Rgba(0x40, 0x80, 0xFF, 0x80), "the GS samples pixel (0,0) at its corner");
    }
}

void Textured(ps2gpu::GsRenderer& renderer) {
    // 2x2: red, green / blue, white at half alpha.
    const std::uint32_t texels[4] = {Rgba(0xFF, 0, 0, 0x80), Rgba(0, 0xFF, 0, 0x80), Rgba(0, 0, 0xFF, 0x80),
                                     Rgba(0xFF, 0xFF, 0xFF, 0x40)};
    renderer.UploadTexture(7, texels, 2, 2);
    ps2gpu::GsDrawState state = Target(0x20);
    state.textured = true;
    state.texture = 7;
    state.repeat_s = state.repeat_t = false;

    Rect(renderer, state, 0, 0, 32, 32, Rgba(0x80, 0x80, 0x80, 0x80));
    Check(Pixel(renderer, 0x20, 4, 4) == Rgba(0xFF, 0, 0, 0x80), "MODULATE by 0x80 is the texel");
    Check(Pixel(renderer, 0x20, 20, 4) == Rgba(0, 0xFF, 0, 0x80), "the texel to the right");
    Check(Pixel(renderer, 0x20, 20, 20) == Rgba(0xFF, 0xFF, 0xFF, 0x40), "the texel below and right");

    Rect(renderer, state, 32, 0, 64, 32, Rgba(0x40, 0x40, 0x40, 0x40));
    Check(Pixel(renderer, 0x20, 36, 4) == Rgba(0x7F, 0, 0, 0x40), "MODULATE by 0x40 halves, truncating");

    ps2gpu::GsDrawState decal = state;
    decal.tfx = ps2gpu::TextureFunction::Decal;
    Rect(renderer, decal, 0, 32, 32, 64, Rgba(0x10, 0x10, 0x10, 0x10));
    Check(Pixel(renderer, 0x20, 4, 36) == Rgba(0xFF, 0, 0, 0x80), "DECAL ignores the vertex colour");

    ps2gpu::GsDrawState no_tcc = decal;
    no_tcc.tcc = false;
    Rect(renderer, no_tcc, 32, 32, 64, 64, Rgba(0x10, 0x10, 0x10, 0x22));
    Check(Pixel(renderer, 0x20, 36, 36) == Rgba(0xFF, 0, 0, 0x22), "without TCC the alpha is the vertex's");
}

void Blended(ps2gpu::GsRenderer& renderer) {
    ps2gpu::GsDrawState state = Target(0x30);
    Rect(renderer, state, 0, 0, 64, 64, Rgba(0xFF, 0xFF, 0xFF, 0x80));

    // (Cs - Cd) * As + Cd: the usual transparency, As 0x40 is a quarter... of
    // 0x80 = 1.0, so half.
    ps2gpu::GsDrawState over = state;
    over.blend = true;
    over.a = ps2gpu::BlendColour::Source;
    over.b = ps2gpu::BlendColour::Dest;
    over.c = ps2gpu::BlendAlpha::Source;
    over.d = ps2gpu::BlendColour::Dest;
    Rect(renderer, over, 0, 0, 32, 32, Rgba(0, 0, 0xFF, 0x40));
    Check(Near(Pixel(renderer, 0x30, 4, 4), Rgba(0x80, 0x80, 0xFF, 0x40)),
          "(Cs - Cd) * As + Cd blends half way at As 0x40, and writes As");

    // (Cs - 0) * FIX + Cd: additive.
    Rect(renderer, state, 32, 0, 64, 32, Rgba(10, 20, 30, 0x80));
    ps2gpu::GsDrawState add = state;
    add.blend = true;
    add.a = ps2gpu::BlendColour::Source;
    add.b = ps2gpu::BlendColour::Zero;
    add.c = ps2gpu::BlendAlpha::Fix;
    add.d = ps2gpu::BlendColour::Dest;
    add.fix = 0x80;
    Rect(renderer, add, 32, 0, 64, 32, Rgba(5, 6, 7, 0x80));
    Check(Near(Pixel(renderer, 0x30, 36, 4), Rgba(15, 26, 37, 0x80)), "(Cs - 0) * FIX + Cd adds");

    // (Cd - Cs) * FIX + 0 with FIX 0x80: subtracts, clamped at zero.
    Rect(renderer, state, 0, 32, 32, 64, Rgba(100, 100, 100, 0x80));
    ps2gpu::GsDrawState sub = add;
    sub.a = ps2gpu::BlendColour::Dest;
    sub.b = ps2gpu::BlendColour::Source;
    sub.d = ps2gpu::BlendColour::Zero;
    Rect(renderer, sub, 0, 32, 32, 64, Rgba(30, 120, 0, 0x80));
    Check(Near(Pixel(renderer, 0x30, 4, 36), Rgba(70, 0, 100, 0x80)), "(Cd - Cs) * FIX subtracts, clamped");
}

void AlphaTested(ps2gpu::GsRenderer& renderer) {
    ps2gpu::GsDrawState state = Target(0x40);
    state.alpha_test = true;
    state.atst = 5;                                 // GEQUAL
    state.aref = 0x80;
    Rect(renderer, state, 0, 0, 32, 32, Rgba(0xFF, 0, 0, 0x40));
    Rect(renderer, state, 32, 0, 64, 32, Rgba(0, 0xFF, 0, 0x80));
    Check(Pixel(renderer, 0x40, 4, 4) == 0, "alpha 0x40 fails GEQUAL 0x80: nothing is written");
    Check(Pixel(renderer, 0x40, 36, 4) == Rgba(0, 0xFF, 0, 0x80), "alpha 0x80 passes GEQUAL 0x80");

    // NEVER with AFAIL FB_ONLY: the frame buffer is written all the same
    // (Burnout 3 copies its frame this way, leaving depth alone).
    ps2gpu::GsDrawState never = state;
    never.atst = 0;
    never.afail = 1;
    Rect(renderer, never, 0, 32, 32, 64, Rgba(0, 0, 0xFF, 0x80));
    Check(Pixel(renderer, 0x40, 4, 36) == Rgba(0, 0, 0xFF, 0x80), "a failed test with AFAIL FB_ONLY writes colour");
}

/* A rectangle at one depth. */
void DepthRect(ps2gpu::GsRenderer& r, const ps2gpu::GsDrawState& state, float x0, float y0, float x1, float y1,
               std::uint32_t z, std::uint32_t rgba) {
    const std::uint8_t cr = rgba & 0xFF, cg = (rgba >> 8) & 0xFF, cb = (rgba >> 16) & 0xFF, ca = rgba >> 24;
    const ps2gpu::GsVertex v[6] = {
        {x0, y0, z, cr, cg, cb, ca, 0, 0, 1}, {x1, y0, z, cr, cg, cb, ca, 1, 0, 1},
        {x0, y1, z, cr, cg, cb, ca, 0, 1, 1}, {x1, y0, z, cr, cg, cb, ca, 1, 0, 1},
        {x1, y1, z, cr, cg, cb, ca, 1, 1, 1}, {x0, y1, z, cr, cg, cb, ca, 0, 1, 1}};
    r.DrawTriangles(state, v, 6);
}

void DepthTested(ps2gpu::GsRenderer& renderer) {
    ps2gpu::GsDrawState state = Target(0x50);
    state.depth = true;
    state.zbp = 0x60;
    state.ztst = 2;                                 // GEQUAL: the larger Z wins
    // Near then far: the far one is hidden.
    DepthRect(renderer, state, 0, 0, 32, 32, 0x200000, Rgba(0xFF, 0, 0, 0x80));
    DepthRect(renderer, state, 0, 0, 32, 32, 0x100000, Rgba(0, 0xFF, 0, 0x80));
    Check(Pixel(renderer, 0x50, 4, 4) == Rgba(0xFF, 0, 0, 0x80), "GEQUAL keeps the nearer (larger) Z");
    // Far then near: the near one covers it.
    DepthRect(renderer, state, 32, 0, 64, 32, 0x100000, Rgba(0, 0xFF, 0, 0x80));
    DepthRect(renderer, state, 32, 0, 64, 32, 0x200000, Rgba(0xFF, 0, 0, 0x80));
    Check(Pixel(renderer, 0x50, 36, 4) == Rgba(0xFF, 0, 0, 0x80), "a nearer Z draws over a farther one");
    // Equal Z: GEQUAL passes, GREATER does not.
    DepthRect(renderer, state, 0, 32, 32, 64, 0x300000, Rgba(0, 0, 0xFF, 0x80));
    DepthRect(renderer, state, 0, 32, 32, 64, 0x300000, Rgba(0xFF, 0xFF, 0, 0x80));
    Check(Pixel(renderer, 0x50, 4, 36) == Rgba(0xFF, 0xFF, 0, 0x80), "GEQUAL passes an equal Z");
    ps2gpu::GsDrawState greater = state;
    greater.ztst = 3;
    DepthRect(renderer, greater, 0, 32, 32, 64, 0x300000, Rgba(0, 0xFF, 0xFF, 0x80));
    Check(Pixel(renderer, 0x50, 4, 36) == Rgba(0xFF, 0xFF, 0, 0x80), "GREATER fails an equal Z");
    // ZMSK: tested, not written.
    ps2gpu::GsDrawState masked = state;
    masked.depth_write = false;
    DepthRect(renderer, masked, 32, 32, 64, 64, 0x500000, Rgba(0xFF, 0, 0xFF, 0x80));
    DepthRect(renderer, state, 32, 32, 64, 64, 0x400000, Rgba(0, 0, 0xFF, 0x80));
    Check(Pixel(renderer, 0x50, 36, 36) == Rgba(0, 0, 0xFF, 0x80), "with ZMSK the Z is not written");
    // What local memory held: uploaded depth hides a farther draw.
    std::vector<std::uint32_t> z(64 * 64, 0x800000);
    renderer.UploadDepth(0x61, 64, 64, z.data());
    ps2gpu::GsDrawState uploaded = state;
    uploaded.fbp = 0x51;
    uploaded.zbp = 0x61;
    DepthRect(renderer, uploaded, 0, 0, 64, 64, 0x700000, Rgba(0xFF, 0, 0, 0x80));
    Check(Pixel(renderer, 0x51, 10, 10) == 0, "an uploaded depth buffer hides what is behind it");
}

void Masked(ps2gpu::GsRenderer& renderer) {
    ps2gpu::GsDrawState state = Target(0x78);
    Rect(renderer, state, 0, 0, 32, 32, Rgba(0x10, 0x20, 0x30, 0x40));
    ps2gpu::GsDrawState masked = state;
    masked.fbmsk = 0xFF000000u;                     // alpha kept, as Burnout 3 draws
    Rect(renderer, masked, 0, 0, 32, 32, Rgba(0xA0, 0xB0, 0xC0, 0x80));
    Check(Pixel(renderer, 0x78, 4, 4) == Rgba(0xA0, 0xB0, 0xC0, 0x40), "FBMSK 0xFF000000 keeps the frame's alpha");
    masked.fbmsk = 0x0000FF00u;
    Rect(renderer, masked, 0, 0, 32, 32, Rgba(1, 2, 3, 4));
    Check(Pixel(renderer, 0x78, 4, 4) == Rgba(1, 0xB0, 3, 4), "FBMSK 0x0000FF00 keeps green");
}

void Fogged(ps2gpu::GsRenderer& renderer) {
    ps2gpu::GsDrawState state = Target(0x70);
    state.fog = true;
    state.fogcol = 0x0000FF;                        // red
    ps2gpu::GsVertex v[6];
    const float corners[6][2] = {{0, 0}, {32, 0}, {0, 32}, {32, 0}, {32, 32}, {0, 32}};
    for (int i = 0; i < 6; ++i) v[i] = {corners[i][0], corners[i][1], 0, 0xFF, 0xFF, 0xFF, 0x80, 0, 0, 1, 0x80};
    renderer.DrawTriangles(state, v, 6);
    Check(Pixel(renderer, 0x70, 4, 4) == Rgba(0xFE, 0x7F, 0x7F, 0x80),
          "fog F 0x80: (128 * C + 127 * FOGCOL) >> 8, as the software GS");
}

}  // namespace

int main() {
    std::string error;
    auto gpu = ps2gpu::CreateGpu(&error);
    if (!gpu) {
        std::printf("no GPU: %s (skipped)\n", error.c_str());
        return 0;
    }
    auto renderer = ps2gpu::CreateGsRenderer(*gpu, &error);
    if (!renderer) {
        std::printf("FAIL: renderer: %s\n", error.c_str());
        return 1;
    }
    Untextured(*renderer);
    Textured(*renderer);
    Blended(*renderer);
    AlphaTested(*renderer);
    DepthTested(*renderer);
    Fogged(*renderer);
    Masked(*renderer);
    if (g_failures) std::printf("gpu renderer tests: %d failed\n", g_failures);
    else std::printf("gpu renderer tests: all passed (%s)\n", gpu->Describe().c_str());
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
