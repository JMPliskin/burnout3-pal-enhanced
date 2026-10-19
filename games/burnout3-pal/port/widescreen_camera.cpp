// Camera-only PAL adaptation of the USA v0.4.0 SetViewWindow hook.
// HUD, 2D packets and the Vulkan presentation remain unchanged.
#include <cmath>
#include <cstring>

#include "ps2host/host.h"
#include "ps2host/run.h"

// PAL MIPS contract: a0 = RwCamera*, a1 = view-window vec2.
// Call the existing translation directly: dispatching this address recurses.
void F_001E2100(lifted::Context&);

namespace {
struct RestoreWindowX {
    u32 address, bits;
    ~RestoreWindowX() { generated::Write32(address, bits); }
};

void SetViewWindow(lifted::Context& c) {
    const float wider = ps2host::PictureAspect() / (4.0f / 3.0f);
    const u32 window = u32(c.r[5]);
    const u32 x_bits = generated::ReadU32(window);
    const u32 y_bits = generated::ReadU32(window + 4);
    float x, y;
    std::memcpy(&x, &x_bits, 4);
    std::memcpy(&y, &y_bits, 4);
    // As USA: only a positive 4:3 window, keeping the vertical view intact.
    // Explicit finite guards leave malformed/offscreen windows to the original.
    if (!std::isfinite(wider) || !std::isfinite(x) || !std::isfinite(y) ||
        std::fabs(wider - 1.0f) < 0.001f || !(x > 0.0f) ||
        std::fabs(y / x - 0.75f) > 0.002f) {
        F_001E2100(c);
        return;
    }
    const float wide = x * wider;
    if (!std::isfinite(wide)) { F_001E2100(c); return; }
    u32 wide_bits;
    std::memcpy(&wide_bits, &wide, 4);
    RestoreWindowX restore{window, x_bits};
    generated::Write32(window, wide_bits);
    F_001E2100(c);
}
} // namespace

void RegisterPalWidescreenCamera() {
    ps2host::Override(0x001E2100u, SetViewWindow,
        "PAL RwCameraSetViewWindow: horizontal widescreen, original vertical view");
}
