/*
 * --gpu-shadow: the GPU GS renderer (engine/gpu, engine/docs/GPU.md) drawing
 * beside the software GS, and checked against it.
 *
 * Every primitive the software GS draws is also handed to ps2gpu - triangles as
 * they are, sprites as two triangles - with the state it was drawn with:
 * FRAME, SCISSOR, TEX0/TEX1/CLAMP, TEST, ALPHA. Textures are decoded from the
 * software GS's local memory, so the GPU samples what the reference has; what
 * it draws goes into its own targets. At the end of each frame every target
 * drawn into is read back and compared with the same region of local memory,
 * and the agreement is reported - the measure of how far the GPU renderer is
 * from replacing the software one. Depth is not yet on the GPU, so 3D scenes
 * differ where surfaces overlap.
 *
 * Without PS2HOST_VULKAN the option reports that it is unavailable.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "ps2host/gs.h"
#include "ps2host/run.h"

#if PS2HOST_VULKAN
#include "ps2gpu/gs_renderer.h"
#endif

namespace ps2host {

#if PS2HOST_VULKAN

namespace {

using gs::u32;
using gs::u64;

u32 Bits(u64 value, int low, int count) { return u32((value >> low) & ((1ull << count) - 1)); }

/* Bits a pixel of a format takes in local memory (the 8H/4HL/4HH formats live
   in 32-bit pixels). */
u32 PixelBits(u32 psm) {
    switch (psm) {
        case gs::PSMCT16: case gs::PSMCT16S: case gs::PSMZ16: case gs::PSMZ16S: return 16;
        case gs::PSMT8: return 8;
        case gs::PSMT4: return 4;
        default: return 32;
    }
}

struct Extent {
    u32 x1 = 0, y1 = 0;             // the furthest pixel drawn, exclusive
};

struct Shadow {
    std::unique_ptr<ps2gpu::Gpu> gpu;
    std::unique_ptr<ps2gpu::GsRenderer> renderer;
    // The texture last decoded, by what decides its contents.
    std::tuple<u64, u64, u64, u64> texture_state{~0ull, 0, 0, 0};
    u64 texture_key = 0;
    // Local memory drawn into since the texture was decoded, as block ranges.
    std::map<u32, u32> since_decode;  // start -> blocks
    bool drawn_into(u32 bp, u32 blocks) const {
        for (const auto& [start, count] : since_decode)
            if (start < bp + blocks && bp < start + count) return true;
        return false;
    }
    std::map<std::tuple<u32, u32, u32>, Extent> drawn;  // this frame's targets
    u64 frames = 0;
    double agreement_sum = 0;
    std::string dump_dir;           // a dump asked for, made at the next frame with drawing
    std::set<u32> depth_seeded;     // depth buffers loaded from local memory this frame
};

Shadow* g_shadow = nullptr;

ps2gpu::GsVertex Convert(const gs::Vertex& v, bool fst, u32 tw, u32 th) {
    ps2gpu::GsVertex out{};
    out.x = v.x;
    out.y = v.y;
    out.z = v.z;                    // clamped to the depth format by the caller
    out.r = v.r;
    out.g = v.g;
    out.b = v.b;
    out.a = v.a;
    out.fog = v.fog;
    if (fst) {
        out.s = float(v.u) / 16.0f / float(tw);
        out.t = float(v.v) / 16.0f / float(th);
        out.q = 1.0f;
    } else {
        out.s = v.s;
        out.t = v.t;
        out.q = v.q;
    }
    return out;
}

void OnDraw(const gs::Gs::DrawCall& call) {
    Shadow& s = *g_shadow;
    const u32 type = u32(call.prim & 7);
    if (type < 3) return;                               // points and lines: not yet
    const gs::Context& c = *call.context;
    ps2gpu::GsDrawState state;
    state.fbp = Bits(c.frame, 0, 9);
    state.fbw = std::max(1u, Bits(c.frame, 16, 6));
    state.psm = Bits(c.frame, 24, 6);
    state.fbmsk = u32(c.frame >> 32);
    state.width = std::min(2048u, state.fbw * 64);
    state.height = 512;
    state.scissor_x0 = int(Bits(c.scissor, 0, 11));
    state.scissor_x1 = int(Bits(c.scissor, 16, 11));
    state.scissor_y0 = int(Bits(c.scissor, 32, 11));
    state.scissor_y1 = int(Bits(c.scissor, 48, 11));
    state.gouraud = (call.prim >> 3) & 1;
    state.textured = (call.prim >> 4) & 1;
    const bool fst = (call.prim >> 8) & 1;
    const u32 tw = 1u << std::min(Bits(c.tex0, 26, 4), 10u), th = 1u << std::min(Bits(c.tex0, 30, 4), 10u);
    if (state.textured) {
        // Decoded again when the texture's registers change, local memory is
        // uploaded into, or a draw since went into the texture's blocks.
        const auto key = std::make_tuple(c.tex0, c.clamp, call.texa, gs::TheGs().uploads());
        const u32 tbp = Bits(c.tex0, 0, 14);
        const u32 tex_blocks = std::max(1u, Bits(c.tex0, 14, 6) * 64 * th * PixelBits(Bits(c.tex0, 20, 6)) / 8 / 256);
        if (key != s.texture_state || s.drawn_into(tbp, tex_blocks)) {
            std::vector<u32> pixels;
            u32 w = 0, h = 0;
            gs::TheGs().DecodeTexture(&pixels, &w, &h);
            // Named by the registers that place it (TBP, TBW, PSM, size, CBP...):
            // decoding it again replaces it rather than adding another.
            s.texture_key = c.tex0 & 0x1FFFFFFFFFFFFFFFull;
            s.renderer->UploadTexture(s.texture_key, pixels.data(), w, h);
            s.texture_state = key;
            s.since_decode.clear();
        }
        state.texture = s.texture_key;
        state.tfx = ps2gpu::TextureFunction(Bits(c.tex0, 35, 2));
        state.tcc = Bits(c.tex0, 34, 1) != 0;
        const int k = int(std::int32_t(Bits(c.tex1, 32, 12) << 20) >> 20);
        const u32 mmin = Bits(c.tex1, 6, 3);
        state.bilinear = k <= 0 ? Bits(c.tex1, 5, 1) != 0 : (mmin == 1 || mmin == 4 || mmin == 5);
        state.repeat_s = Bits(c.clamp, 0, 2) == 0;
        state.repeat_t = Bits(c.clamp, 2, 2) == 0;
    }
    state.alpha_test = Bits(c.test, 0, 1) != 0;
    state.atst = Bits(c.test, 1, 3);
    state.aref = Bits(c.test, 4, 8);
    state.afail = Bits(c.test, 12, 2);
    state.blend = (call.prim >> 6) & 1;
    state.a = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 0, 2)));
    state.b = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 2, 2)));
    state.c = ps2gpu::BlendAlpha(std::min(2u, Bits(c.alpha, 4, 2)));
    state.d = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 6, 2)));
    state.fix = Bits(c.alpha, 32, 8);
    state.fog = (call.prim >> 5) & 1;
    state.fogcol = u32(call.fogcol & 0xFFFFFF);
    // Depth: ZTE with ZBUF. Each depth buffer starts the frame as local memory
    // has it, so the GPU tests against what the software GS drew.
    const u32 zpsm = 0x30 | Bits(c.zbuf, 24, 4);
    const u32 zmax = zpsm == gs::PSMZ32 ? 0xFFFFFFFFu : zpsm == gs::PSMZ24 ? 0xFFFFFFu : 0xFFFFu;
    state.depth = Bits(c.test, 16, 1) != 0;
    state.ztst = Bits(c.test, 17, 2);
    state.zbp = Bits(c.zbuf, 0, 9);
    state.depth_write = Bits(c.zbuf, 32, 1) == 0;
    if (state.depth && s.depth_seeded.insert(state.zbp).second) {
        std::vector<u32> z(std::size_t(state.width) * state.height);
        const gs::Gs& g = gs::TheGs();
        for (u32 y = 0; y < state.height; ++y)
            for (u32 x = 0; x < state.width; ++x)
                z[std::size_t(y) * state.width + x] = g.memory.Read(state.zbp * 32, state.fbw, x, y, zpsm);
        s.renderer->UploadDepth(state.zbp, state.width, state.height, z.data());
    }

    ps2gpu::GsVertex v[6];
    std::size_t n = 0;
    if (type == 6 && call.count >= 2) {                 // a sprite: two triangles, the second vertex's colour
        ps2gpu::GsVertex a = Convert(call.vertices[0], fst, tw, th);
        ps2gpu::GsVertex b = Convert(call.vertices[1], fst, tw, th);
        ps2gpu::GsVertex ab = b, ba = b;
        ab.x = b.x; ab.y = a.y; ab.t = a.t;
        ba.x = a.x; ba.y = b.y; ba.s = a.s;
        a.r = b.r; a.g = b.g; a.b = b.b; a.a = b.a; a.fog = b.fog;
        v[0] = a; v[1] = ab; v[2] = ba; v[3] = ab; v[4] = b; v[5] = ba;
        n = 6;
        state.gouraud = true;
    } else if (call.count >= 3) {
        for (int i = 0; i < 3; ++i) v[i] = Convert(call.vertices[i], fst, tw, th);
        n = 3;
    }
    if (!n) return;
    for (std::size_t i = 0; i < n; ++i) v[i].z = std::min(v[i].z, zmax);
    s.renderer->DrawTriangles(state, v, n);
    Extent& e = s.drawn[std::make_tuple(state.fbp, state.fbw, state.psm)];
    for (std::size_t i = 0; i < n; ++i) {
        e.x1 = std::max(e.x1, u32(std::clamp(std::ceil(v[i].x), 0.0f, float(state.width))));
        e.y1 = std::max(e.y1, u32(std::clamp(std::ceil(v[i].y), 0.0f, float(state.height))));
    }
    e.x1 = std::min(e.x1, u32(state.scissor_x1 + 1));
    e.y1 = std::min(e.y1, u32(state.scissor_y1 + 1));
    u32& blocks = s.since_decode[state.fbp * 32];
    blocks = std::max(blocks, std::max(1u, state.fbw * 64 * e.y1 * PixelBits(state.psm) / 8 / 256));
}

void WriteBmp(const std::string& path, const std::vector<u32>& rgba, u32 stride, u32 w, u32 h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const u32 data = w * h * 4, file = 54 + data;
    unsigned char header[54] = {'B', 'M'};
    auto put32 = [&](int at, u32 v) { for (int i = 0; i < 4; ++i) header[at + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, file);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, u32(-std::int32_t(h)));                   // top-down
    header[26] = 1;
    header[28] = 32;
    put32(34, data);
    std::fwrite(header, 1, 54, f);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u32 c = rgba[std::size_t(y) * stride + x];
            const unsigned char px[4] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c, 0xFF};
            std::fwrite(px, 1, 4, f);
        }
    std::fclose(f);
}

}  // namespace

bool StartGpuShadow(std::string* error) {
    static Shadow shadow;
    shadow.gpu = ps2gpu::CreateGpu(error);
    if (!shadow.gpu) return false;
    shadow.renderer = ps2gpu::CreateGsRenderer(*shadow.gpu, error);
    if (!shadow.renderer) return false;
    g_shadow = &shadow;
    gs::TheGs().on_draw = OnDraw;
    std::fprintf(stderr, "gpu-shadow: drawing on %s beside the software GS\n", shadow.gpu->Describe().c_str());
    return true;
}

void GpuShadowFrame(u64 frame, const std::string& dump_dir) {
    if (!g_shadow) return;
    Shadow& s = *g_shadow;
    if (!dump_dir.empty()) s.dump_dir = dump_dir;
    s.renderer->Flush();
    const gs::Gs& g = gs::TheGs();
    u32 shown_fbp = 0, shown_fbw = 0, shown_psm = 0;
    g.Displayed(&shown_fbp, &shown_fbw, &shown_psm);
    std::string line;
    u64 total = 0, close = 0;
    for (const auto& [key, extent] : s.drawn) {
        const auto [fbp, fbw, psm] = key;
        std::vector<u32> pixels;
        u32 w = 0, h = 0;
        if (!s.renderer->ReadTarget(fbp, fbw, psm, &pixels, &w, &h)) continue;
        const u32 x1 = std::min(extent.x1, w), y1 = std::min(extent.y1, h);
        const bool sixteen = psm == gs::PSMCT16 || psm == gs::PSMCT16S;
        const int slack = sixteen ? 8 : 2;
        u64 n = 0, ok = 0;
        for (u32 y = 0; y < y1; ++y)
            for (u32 x = 0; x < x1; ++x) {
                const u32 want = g.FramePixel(fbp, fbw, psm, x, y), got = pixels[std::size_t(y) * w + x];
                bool same = true;
                for (int shift = 0; shift < (psm == gs::PSMCT32 ? 32 : 24); shift += 8) {   // alpha too where it is stored
                    const int a = int((want >> shift) & 0xFF), b = int((got >> shift) & 0xFF);
                    if (std::abs(a - b) > slack) same = false;
                }
                ++n;
                ok += same;
            }
        total += n;
        close += ok;
        char text[96];
        std::snprintf(text, sizeof text, " %X/%u/%02X %ux%u %.1f%%", unsigned(fbp), unsigned(fbw), unsigned(psm),
                      unsigned(x1), unsigned(y1), n ? 100.0 * double(ok) / double(n) : 100.0);
        line += text;
        if (!s.dump_dir.empty() && x1 && y1) {
            char name[96];
            std::snprintf(name, sizeof name, "/gpu_%06llu_%X.bmp", static_cast<unsigned long long>(frame),
                          unsigned(fbp));
            WriteBmp(s.dump_dir + name, pixels, w, x1, y1);
            // The software GS's, beside it.
            std::vector<u32> reference(std::size_t(x1) * y1);
            for (u32 y = 0; y < y1; ++y)
                for (u32 x = 0; x < x1; ++x) reference[std::size_t(y) * x1 + x] = g.FramePixel(fbp, fbw, psm, x, y);
            std::snprintf(name, sizeof name, "/soft_%06llu_%X.bmp", static_cast<unsigned long long>(frame),
                          unsigned(fbp));
            WriteBmp(s.dump_dir + name, reference, x1, x1, y1);
        }
    }
    if (total) {
        ++s.frames;
        s.agreement_sum += double(close) / double(total);
        std::fprintf(stderr, "[gpu-shadow] frame %llu:%s (shown %X)\n", static_cast<unsigned long long>(frame),
                     line.c_str(), unsigned(shown_fbp));
    }
    if (!s.drawn.empty()) s.dump_dir.clear();
    s.drawn.clear();
    s.depth_seeded.clear();
}

std::string GpuShadowSummary() {
    if (!g_shadow || !g_shadow->frames) return "";
    char text[128];
    std::snprintf(text, sizeof text, "gpu-shadow: %llu frames compared, %.1f%% of pixels agree on average",
                  static_cast<unsigned long long>(g_shadow->frames), 100.0 * g_shadow->agreement_sum / double(g_shadow->frames));
    return text;
}

#else

bool StartGpuShadow(std::string* error) {
    if (error) *error = "built without the GPU renderer (PS2HOST_VULKAN)";
    return false;
}
void GpuShadowFrame(gs::u64, const std::string&) {}
std::string GpuShadowSummary() { return ""; }

#endif

}  // namespace ps2host
