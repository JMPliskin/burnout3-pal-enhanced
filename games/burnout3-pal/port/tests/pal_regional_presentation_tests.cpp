#include "../../../../engine/host/src/pal_regional_presentation.h"
#include <cstdio>
#include <initializer_list>

using namespace pal_regional_presentation;

static int failures = 0;
static void check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); ++failures; }
}

static Candidate race() {
    Candidate c;
    c.fbp = 0; c.fbw = 10; c.psm = 0x0a;
    c.width = 640; c.height = 1024; c.scale = 2; c.shown = 448;
    c.pmode = 0x66; c.smode2 = 1;
    c.dispfb2 = 0x51400; c.display2 = 0x1bf9ff0183227cull;
    c.initialized = c.footprint_known = true;
    for (unsigned p = 0; p < 70; ++p) c.visible.set(p);
    for (unsigned p = 70; p < 210; ++p) c.stale.set(p);
    return c;
}

int main() {
    for (unsigned scale : {2u, 3u, 4u}) {
        auto c = race(); c.scale = scale;
        check(Evaluate(c) == Reject::None, "regional race accepted");
        check(PhysicalExtentMatches(scale, 640u * scale, 1024u * scale),
              "physical target extent");
        c.stale.set(69);
        check(Evaluate(c) == Reject::Footprint, "visible stale rejected");
        c = race(); c.scale = scale; c.cpu_write = true;
        check(Evaluate(c) == Reject::CpuWrite, "CPU write rejected");
        c = race(); c.scale = scale; c.pending_patch = true;
        check(Evaluate(c) == Reject::PendingPatch, "pending patch rejected");
        c = race(); c.scale = scale; c.other_producer = true;
        check(Evaluate(c) == Reject::OtherProducer, "other producer rejected");
        c = race(); c.scale = scale; c.initialized = false;
        check(Evaluate(c) == Reject::Uninitialized, "uninitialized rejected");
    }
    auto c = race(); c.psm = 0;
    check(Evaluate(c) == Reject::Key, "other PSM rejected");
    c = race(); c.smode2 = 0;
    check(Evaluate(c) == Reject::Video, "other video mode rejected");
    c = race(); c.shown = 512;
    check(Evaluate(c) == Reject::Geometry, "selector geometry rejected");
    c = race(); c.scale = 1;
    check(Evaluate(c) == Reject::Geometry, "unvalidated scale rejected");
    c = race(); c.footprint_known = false;
    check(Evaluate(c) == Reject::Footprint, "unknown footprint rejected");
    c = race(); c.unknown = true;
    check(Evaluate(c) == Reject::Unknown, "unknown invalidation rejected");
    check(!PhysicalExtentMatches(4, 1280, 2048), "old scale rejected");
    check(!PhysicalExtentMatches(3, 1920, 896), "cropped target rejected");
    check(SelectorNeedsFullHeightFallback(0x66, 1, 0, 0, 0x51400,
          0x001ff9ff01848290ull), "PAL selector fallback");
    check(!SelectorNeedsFullHeightFallback(0x66, 1, 0, 0, 0x51400,
          0x001bf9ff0183227cull), "race stays eligible");
    std::printf("PAL regional predicate: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
