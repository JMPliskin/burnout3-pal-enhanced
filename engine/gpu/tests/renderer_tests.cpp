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
    ps2gpu::GsDrawState state;
    state.fbp = 0x10;
    state.fbw = 1;
    state.width = 64;
    state.height = 64;
    state.scissor_x1 = 63;
    state.scissor_y1 = 63;
    state.gouraud = false;

    // A flat right triangle covering the top-left half of 0..32.
    const ps2gpu::GsVertex flat[3] = {{0, 0, 0, 0, 0, 0, 0x80, 0, 0, 1},
                                      {32, 0, 0, 0, 0, 0, 0x80, 0, 0, 1},
                                      {0, 32, 0, 0x40, 0x80, 0xFF, 0x80, 0, 0, 1}};
    renderer->DrawTriangles(state, flat, 3);

    // Scissored away entirely.
    ps2gpu::GsDrawState clipped = state;
    clipped.scissor_x0 = 60;
    clipped.scissor_y0 = 60;
    const ps2gpu::GsVertex hidden[3] = {{0, 40, 0, 0xFF, 0, 0, 0x80, 0, 0, 1},
                                        {20, 40, 0, 0xFF, 0, 0, 0x80, 0, 0, 1},
                                        {0, 60, 0, 0xFF, 0, 0, 0x80, 0, 0, 1}};
    renderer->DrawTriangles(clipped, hidden, 3);

    std::vector<std::uint32_t> pixels;
    std::uint32_t w = 0, h = 0;
    Check(renderer->ReadTarget(0x10, 1, 0, &pixels, &w, &h) && w == 64 && h == 64, "the target reads back");
    if (pixels.size() == 64 * 64) {
        Check(pixels[5 * 64 + 5] == Rgba(0x40, 0x80, 0xFF, 0x80), "flat shading takes the last vertex's colour");
        Check(pixels[40 * 64 + 40] == 0, "outside the triangle stays as local memory was (zero)");
        Check(pixels[45 * 64 + 5] == 0, "a scissored triangle draws nothing");
        Check(pixels[0] == Rgba(0x40, 0x80, 0xFF, 0x80), "the GS samples pixel (0,0) at its corner");
    }
    if (g_failures) std::printf("gpu renderer tests: %d failed\n", g_failures);
    else std::printf("gpu renderer tests: all passed (%s)\n", gpu->Describe().c_str());
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
