/*
 * The GPU GS renderer (engine/gpu, engine/docs/GPU.md) driven by the host's GS
 * model, in one of two modes.
 *
 * --gpu-shadow: beside the software GS, and checked against it. Every
 * primitive the software GS draws is also handed to ps2gpu with the state it was
 * drawn with. Textures are decoded from the software GS's local memory (with
 * --gpu-shadow-rtt, a texture that is a GPU target is sampled from the target
 * instead); depth buffers start each frame as local memory has them. At the end
 * of each frame every target drawn into is read back and compared with local
 * memory, and the agreement is reported.
 *
 * --gpu: instead of the software GS. The GS model still takes every GIF packet,
 * register and image transfer, but draws nothing (Gs::rasterize false); the GPU
 * draws, and its images stand for regions of local memory. The two are kept in
 * step: each GPU image (a target or depth buffer, with the blocks it covers)
 * knows whether the GPU has drawn what memory lacks (gpu_newer) or memory has
 * changed under it (stale). Before local memory is read or written other than
 * by drawing - a transfer, a local copy, a CLUT load, a texture decoded, the
 * picture shown - the GPU images over it are written back into it; after it is
 * written, they are stale, and reload from memory before the GPU next uses
 * them. A target drawn under another width or format than one it overlaps
 * (Burnout 3's reflection buffers), or a depth buffer that is drawn into as
 * colour, is reconciled the same way, through memory. An image stands for the
 * rows drawn into so far, not all the scissor allows: Burnout 3's depth buffer
 * starts where its frame buffer's 448 rows end, and a 512-row reach would have
 * each depth write make the frame stale. A texture that is a target is sampled
 * from the target itself. Other textures are decoded into a cache keyed by
 * their registers, TEXA and their CLUT, dropped when memory under them is
 * written or the GPU draws over them.
 *
 * Known difference: the GPU's blender rounds where the GS truncates, so a
 * blended pixel can be 1 brighter (invisible; in shadow runs, Burnout 3's frame
 * copy doubles it into the 16-bit display buffer).
 *
 * Without PS2HOST_VULKAN the options report that they are unavailable.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ps2host/gs.h"
#include "ps2host/profile.h"
#include "ps2host/run.h"

#if PS2HOST_VULKAN
#include "ps2gpu/gs_renderer.h"
#endif

namespace ps2host {

#if PS2HOST_VULKAN

ps2gpu::Gpu* WindowGpu();       // window.cpp: the window's device, when there is one

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

bool Is16(u32 psm) { return psm == gs::PSMCT16 || psm == gs::PSMCT16S || psm == gs::PSMZ16 || psm == gs::PSMZ16S; }

/* The blocks a buffer's first `rows` rows take: whole rows of pages (32 blocks
   each), FBW pages wide. */
std::pair<u32, u32> Blocks(u32 fbp, u32 fbw, u32 psm, u32 rows) {
    const u32 page_rows = Is16(psm) ? 64 : 32;
    const u32 begin = fbp * 32;
    return {begin, begin + std::max(1u, (rows + page_rows - 1) / page_rows) * std::max(1u, fbw) * 32};
}

u32 To16(u32 c) {
    return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10) | ((c >> 31) << 15);
}

u32 From16(u32 c) {
    return ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19) | ((c & 0x8000) ? 0x80000000u : 0);
}

struct Extent {
    u32 x1 = 0, y1 = 0;             // the furthest pixel drawn, exclusive
};

/* A GPU image standing for a region of local memory (--gpu). */
struct Owned {
    u32 fbp = 0, fbw = 1, psm = 0;  // FBP (ZBP) in 2048-word units, FBW, PSM
    bool depth = false;
    u32 width = 0, height = 0;      // the image
    // The blocks it stands for: the rows used so far (drawn into, or sampled),
    // of all it could hold; of those, the ones the GPU has drawn into.
    u32 begin = 0, end = 0, full_end = 0;
    u32 drawn_end = 0;
    u32 x1 = 0, y1 = 0;             // how much of it the GPU has drawn
    bool gpu_newer = false;         // drawn on the GPU, not yet in local memory
    bool stale = true;              // local memory changed since the image was loaded
    bool stale_beyond = false;      // ...in rows past `end`: stale when it grows into them
    int why = 0;                    // what made it stale: 0 new, 1 memory written, 2 a draw over it, 3 another's write-back
    u64 reloads[4] = {};            // reloads, by why
    u64 drawn_serial = 0;           // when the GPU last drew into it (Bridge::serial)
};

/* A texture decoded into the renderer (--gpu). */
struct Cached {
    u64 renderer_key = 0;
    u32 begin = 0, end = 0;
    bool valid = false;
    u64 decoded_serial = 0;         // Bridge::serial when decoded
    std::list<u64>::iterator lru;
};

struct Bridge {
    std::unique_ptr<ps2gpu::Gpu> gpu;
    std::unique_ptr<ps2gpu::GsRenderer> renderer;
    bool primary = false;
    // --gpu-shadow-rtt: a texture that is a GPU target is sampled from it, as
    // the GPU will have to on its own.
    bool targets_as_textures = false;

    // Shadow: the texture last decoded, by what decides its contents, and the
    // memory drawn into since.
    std::tuple<u64, u64, u64, u64> texture_state{~0ull, 0, 0, 0};
    u64 texture_key = 0;
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

    // Primary.
    std::map<std::tuple<u32, u32, u32, bool>, Owned> owned;
    std::unordered_map<u64, Cached> textures;
    std::list<u64> texture_lru;     // most recent first
    u64 next_texture = 1;
    u64 serial = 0;                 // counts GPU draws, so a texture knows if one went over it
    u64 write_backs = 0, reloads = 0, decodes = 0;
};

Bridge* g_bridge = nullptr;

// ------------------------------------------------------------------ --gpu

/* Writes a GPU image back into local memory. */
void WriteBack(Bridge& b, Owned& o) {
    Timed timed(Section::GpuSync);
    o.gpu_newer = false;
    std::vector<u32> pixels;
    u32 w = o.width, h = o.height;
    if (o.depth ? !b.renderer->ReadDepth(o.fbp, w, h, &pixels)
                : !b.renderer->ReadTarget(o.fbp, o.fbw, o.psm, &pixels, &w, &h))
        return;
    ++b.write_backs;
    gs::Memory& memory = gs::TheGs().memory;
    const u32 x1 = std::min(o.x1, w), y1 = std::min(o.y1, h);
    for (u32 y = 0; y < y1; ++y)
        for (u32 x = 0; x < x1; ++x) {
            const u32 c = pixels[std::size_t(y) * w + x];
            memory.Write(o.fbp * 32, o.fbw, x, y, o.psm, !o.depth && Is16(o.psm) ? To16(c) : c);
        }
}

/* Before local memory [begin, end) is read or written: what the GPU drew there
   goes back into it; written, the GPU images there must reload. */
void Sync(Bridge& b, u32 begin, u32 end, bool write, const Owned* except = nullptr) {
    for (auto& [key, o] : b.owned) {
        if (&o == except) continue;
        if (o.gpu_newer && o.begin < end && begin < o.drawn_end) WriteBack(b, o);
        if (!write || end <= o.begin || o.full_end <= begin) continue;
        if (begin < o.end) {
            if (!o.stale) { o.stale = true; o.why = 1; }
        } else {
            o.stale_beyond = true;
        }
    }
    if (write)
        for (auto& [key, t] : b.textures)
            if (t.begin < end && begin < t.end) t.valid = false;
}

/* A target or depth buffer about to be drawn into or sampled: up to date with
   memory and whatever else the GPU drew over its blocks. */
Owned& Use(Bridge& b, u32 fbp, u32 fbw, u32 psm, bool depth, u32 width, u32 height, u32 rows) {
    Owned& o = b.owned[std::make_tuple(fbp, fbw, psm, depth)];
    if (!o.width) {
        o.fbp = fbp;
        o.fbw = fbw;
        o.psm = psm;
        o.depth = depth;
        o.width = width;
        o.height = height;
        std::tie(o.begin, o.end) = Blocks(fbp, fbw, psm, rows);
        o.full_end = Blocks(fbp, fbw, psm, height).second;
        o.drawn_end = o.begin;
    } else {
        const u32 end = Blocks(fbp, fbw, psm, rows).second;
        if (end > o.end) {
            o.end = end;
            if (o.stale_beyond && !o.stale) { o.stale = true; o.why = 1; }
        }
    }
    // Another image's drawing over these blocks reaches this one through memory.
    for (auto& [key, other] : b.owned)
        if (&other != &o && other.gpu_newer && other.begin < o.end && o.begin < other.drawn_end) {
            WriteBack(b, other);
            if (!o.stale) { o.stale = true; o.why = 3; }
        }
    if (o.stale) {
        Timed timed(Section::GpuSync);
        ++b.reloads;
        const gs::Gs& g = gs::TheGs();
        std::vector<u32> pixels(std::size_t(o.width) * o.height);
        for (u32 y = 0; y < o.height; ++y)
            for (u32 x = 0; x < o.width; ++x) {
                const u32 raw = g.memory.Read(o.fbp * 32, o.fbw, x, y, o.psm);
                pixels[std::size_t(y) * o.width + x] =
                    depth ? raw : Is16(o.psm) ? From16(raw) : o.psm == gs::PSMCT24 ? (raw & 0xFFFFFF) : raw;
            }
        if (depth) b.renderer->UploadDepth(o.fbp, o.width, o.height, pixels.data());
        else b.renderer->UploadTarget(o.fbp, o.fbw, o.psm, pixels.data(), o.width, o.height);
        ++o.reloads[o.why];
        o.stale = o.stale_beyond = false;
    }
    return o;
}

/* After the GPU drew into `o`, rows up to `rows`: memory and the other images
   over those blocks are behind. */
void Drew(Bridge& b, Owned& o, u32 x1, u32 y1) {
    o.gpu_newer = true;
    o.x1 = std::max(o.x1, std::min(x1, o.width));
    o.y1 = std::max(o.y1, std::min(y1, o.height));
    o.drawn_end = std::max(o.drawn_end, Blocks(o.fbp, o.fbw, o.psm, o.y1).second);
    o.end = std::max(o.end, o.drawn_end);
    o.drawn_serial = ++b.serial;    // textures over it decoded before this are out of date
    for (auto& [key, other] : b.owned) {
        if (&other == &o || o.drawn_end <= other.begin || other.full_end <= o.begin) continue;
        if (o.begin < other.end) {
            if (!other.stale) { other.stale = true; other.why = 2; }
        } else {
            other.stale_beyond = true;
        }
    }
}

/* The CLUT a palettized texture is read through, as a hash. */
u64 ClutHash(u32 psm) {
    if (psm != gs::PSMT8 && psm != gs::PSMT4 && psm != gs::PSMT8H && psm != gs::PSMT4HL && psm != gs::PSMT4HH)
        return 0;
    const auto& clut = gs::TheGs().Clut();
    u64 h = 1469598103934665603ull;
    for (u32 c : clut) h = (h ^ c) * 1099511628211ull;
    return h;
}

/* The renderer's key for the texture the current primitive samples, decoded if
   need be (--gpu). */
u64 CachedTexture(Bridge& b, const gs::Context& c, u64 texa, u32 tw, u32 th) {
    const u32 tbp = Bits(c.tex0, 0, 14), tbw = Bits(c.tex0, 14, 6), psm = Bits(c.tex0, 20, 6);
    // What decides the decoded texels: where and how it is stored (TBP0, TBW,
    // PSM, TW, TH), how a region mode wraps it (CLAMP), TEXA, the CLUT's
    // contents (wherever it was loaded from).
    u64 key = c.tex0 & 0x3FFFFFFFFull;
    if (Bits(c.clamp, 0, 2) >= 2 || Bits(c.clamp, 2, 2) >= 2) key ^= c.clamp * 0xC2B2AE3D27D4EB4Full;
    key ^= texa * 0x165667B19E3779F9ull;
    key ^= ClutHash(psm) * 0x27D4EB2F165667C5ull;
    Cached& t = b.textures[key];
    if (!t.renderer_key) {
        t.renderer_key = b.next_texture++;
        b.texture_lru.push_front(key);
        t.lru = b.texture_lru.begin();
        t.begin = tbp;
        t.end = tbp + std::max(1u, tbw * 64 * th * PixelBits(psm) / 8 / 256 + 32);
    } else {
        b.texture_lru.splice(b.texture_lru.begin(), b.texture_lru, t.lru);
    }
    // The GPU drawing over its blocks since it was decoded makes it out of date
    // as surely as memory written there.
    for (const auto& [okey, o] : b.owned)
        if (o.drawn_serial > t.decoded_serial && o.begin < t.end && t.begin < o.drawn_end) t.valid = false;
    if (!t.valid) {
        Timed timed(Section::GpuSync);
        Sync(b, t.begin, t.end, false);
        std::vector<u32> pixels;
        u32 w = 0, h = 0;
        gs::TheGs().DecodeTexture(&pixels, &w, &h);
        b.renderer->UploadTexture(t.renderer_key, pixels.data(), w, h);
        t.valid = true;
        t.decoded_serial = b.serial;
        ++b.decodes;
    }
    const u64 renderer_key = t.renderer_key;
    // Keep the cache bounded: the least recently used go.
    while (b.textures.size() > 2048) {
        const u64 old = b.texture_lru.back();
        b.texture_lru.pop_back();
        auto found = b.textures.find(old);
        if (found != b.textures.end()) {
            b.renderer->ReleaseTexture(found->second.renderer_key);
            b.textures.erase(found);
        }
    }
    (void)tw;
    return renderer_key;
}

// ------------------------------------------------------------------ both

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

/* A primitive as triangles: sprites as two (the second vertex's colour), lines
   and points as one pixel wide quads. */
std::size_t Triangles(const gs::Gs::DrawCall& call, bool fst, u32 tw, u32 th, ps2gpu::GsVertex* v, bool* gouraud) {
    const u32 type = u32(call.prim & 7);
    auto quad = [&](ps2gpu::GsVertex a, ps2gpu::GsVertex b) {
        ps2gpu::GsVertex ab = b, ba = b;
        ab.x = b.x; ab.y = a.y; ab.t = a.t;
        ba.x = a.x; ba.y = b.y; ba.s = a.s;
        a.r = b.r; a.g = b.g; a.b = b.b; a.a = b.a; a.fog = b.fog;
        v[0] = a; v[1] = ab; v[2] = ba; v[3] = ab; v[4] = b; v[5] = ba;
        *gouraud = true;
        return std::size_t(6);
    };
    if (type == 6 && call.count >= 2)
        return quad(Convert(call.vertices[0], fst, tw, th), Convert(call.vertices[1], fst, tw, th));
    // Lines and points as the software GS draws them: a pixel per step along
    // the major axis, at the line's position rounded, both ends included. A
    // pixel n is sampled at n, so the band [p - 0.5, p + 0.5) across the line
    // holds exactly the rounded pixel, and the ends reach half a pixel past.
    if (type == 0 && call.count >= 1) {             // a point: its pixel
        ps2gpu::GsVertex a = Convert(call.vertices[call.count - 1], fst, tw, th), b = a;
        a.x -= 0.5f;
        a.y -= 0.5f;
        b.x += 0.5f;
        b.y += 0.5f;
        return quad(a, b);
    }
    if ((type == 1 || type == 2) && call.count >= 2) {   // a line: a band one pixel across
        ps2gpu::GsVertex a = Convert(call.vertices[0], fst, tw, th), b = Convert(call.vertices[1], fst, tw, th);
        const bool flat = std::fabs(b.x - a.x) >= std::fabs(b.y - a.y);
        float& a_major = flat ? a.x : a.y;
        float& b_major = flat ? b.x : b.y;
        const float reach = b_major >= a_major ? 0.5f : -0.5f;
        a_major -= reach;
        b_major += reach;
        ps2gpu::GsVertex a0 = a, a1 = a, b0 = b, b1 = b;
        if (flat) { a0.y -= 0.5f; b0.y -= 0.5f; a1.y += 0.5f; b1.y += 0.5f; }
        else { a0.x -= 0.5f; b0.x -= 0.5f; a1.x += 0.5f; b1.x += 0.5f; }
        v[0] = a0; v[1] = b0; v[2] = a1; v[3] = b0; v[4] = b1; v[5] = a1;
        return 6;
    }
    if (call.count >= 3) {
        for (int i = 0; i < 3; ++i) v[i] = Convert(call.vertices[i], fst, tw, th);
        return 3;
    }
    return 0;
}

void OnDraw(const gs::Gs::DrawCall& call) {
    Timed timed(Section::GpuBridge);
    Bridge& s = *g_bridge;
    const u32 type = u32(call.prim & 7);
    if (type == 7) return;
    if (!s.primary && type < 3) return;                 // the shadow compares what it has always drawn
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
    // The primitive as triangles, and the pixels it can reach.
    ps2gpu::GsVertex v[6];
    bool gouraud = state.gouraud;
    const std::size_t n = Triangles(call, fst, tw, th, v, &gouraud);
    state.gouraud = gouraud;
    if (!n) return;
    u32 x1 = 0, y1 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        // Exclusive: the GS draws the pixels whose centres lie before the far edge.
        x1 = std::max(x1, u32(std::clamp(std::ceil(v[i].x), 0.0f, float(state.width))));
        y1 = std::max(y1, u32(std::clamp(std::ceil(v[i].y), 0.0f, float(state.height))));
    }
    x1 = std::min(x1, u32(state.scissor_x1 + 1));
    y1 = std::min(y1, u32(state.scissor_y1 + 1));
    if (s.primary) {
        // Wholly outside the scissor (or the buffer): nothing to draw, and no
        // image to bring up to date for it. Burnout 3's garage sends thousands
        // a frame to FRAME 0, FBW 1, whose colour and Z alias the same page.
        float lo_x = v[0].x, lo_y = v[0].y, hi_x = v[0].x, hi_y = v[0].y;
        for (std::size_t i = 1; i < n; ++i) {
            lo_x = std::min(lo_x, v[i].x);
            lo_y = std::min(lo_y, v[i].y);
            hi_x = std::max(hi_x, v[i].x);
            hi_y = std::max(hi_y, v[i].y);
        }
        const float left = float(std::max(state.scissor_x0, 0)), top = float(std::max(state.scissor_y0, 0));
        const float right = float(std::min(state.scissor_x1, int(state.width) - 1));
        const float bottom = float(std::min(state.scissor_y1, int(state.height) - 1));
        if (std::floor(hi_x) < left || std::ceil(lo_x) > right || std::floor(hi_y) < top || std::ceil(lo_y) > bottom)
            return;
    }
    const u32 rows = std::max(1u, y1);
    Owned* target = nullptr;
    if (s.primary) target = &Use(s, state.fbp, state.fbw, state.psm, false, state.width, state.height, rows);
    if (state.textured) {
        const u32 tex_psm = Bits(c.tex0, 20, 6), tbp_blocks = Bits(c.tex0, 0, 14);
        const u32 tex_format = tex_psm == gs::PSMCT32 ? 0 : tex_psm == gs::PSMCT24 ? 1
                             : (tex_psm == gs::PSMCT16 || tex_psm == gs::PSMCT16S) ? 2 : 3;
        state.tw = tw;
        state.th = th;
        // A texture that is a target the GPU has drawn is sampled from it
        // (render to texture).
        if ((s.primary || s.targets_as_textures) && tex_format < 3 && tbp_blocks % 32 == 0) {
            const u32 candidates[2] = {tex_format == 2 ? tex_psm : gs::PSMCT32, tex_format == 2 ? tex_psm : gs::PSMCT24};
            const u32 tex_fbp = tbp_blocks / 32, tex_fbw = Bits(c.tex0, 14, 6);
            for (u32 psm : candidates) {
                const bool known = s.primary ? s.owned.count(std::make_tuple(tex_fbp, tex_fbw, psm, false)) != 0
                                             : s.renderer->HasTarget(tex_fbp, tex_fbw, psm);
                if (!known) continue;
                if (s.primary) {
                    Owned& source = s.owned[std::make_tuple(tex_fbp, tex_fbw, psm, false)];
                    Use(s, tex_fbp, tex_fbw, psm, false, source.width, source.height, 1);   // sampling grows nothing
                }
                state.texture_target = true;
                state.tex_fbp = tex_fbp;
                state.tex_fbw = tex_fbw;
                state.tex_target_psm = psm;
                break;
            }
            state.tex_format = tex_format;
            state.ta0 = Bits(call.texa, 0, 8);
            state.ta1 = Bits(call.texa, 32, 8);
            state.aem = Bits(call.texa, 15, 1) != 0;
        }
        if (!state.texture_target) {
            if (s.primary) {
                state.texture = CachedTexture(s, c, call.texa, tw, th);
            } else {
                // Decoded again when the texture's registers change, local memory
                // is uploaded into, or a draw since went into the texture's blocks.
                const auto key = std::make_tuple(c.tex0, c.clamp, call.texa, gs::TheGs().uploads());
                const u32 tbp = Bits(c.tex0, 0, 14);
                const u32 tex_blocks = std::max(1u, Bits(c.tex0, 14, 6) * 64 * th * PixelBits(tex_psm) / 8 / 256);
                if (key != s.texture_state || s.drawn_into(tbp, tex_blocks)) {
                    std::vector<u32> pixels;
                    u32 w = 0, h = 0;
                    gs::TheGs().DecodeTexture(&pixels, &w, &h);
                    // Named by the registers that place it: decoding it again
                    // replaces it rather than adding another.
                    s.texture_key = c.tex0 & 0x1FFFFFFFFFFFFFFFull;
                    s.renderer->UploadTexture(s.texture_key, pixels.data(), w, h);
                    s.texture_state = key;
                    s.since_decode.clear();
                }
                state.texture = s.texture_key;
            }
        }
        state.tfx = ps2gpu::TextureFunction(Bits(c.tex0, 35, 2));
        state.tcc = Bits(c.tex0, 34, 1) != 0;
        const int k = int(std::int32_t(Bits(c.tex1, 32, 12) << 20) >> 20);
        const u32 mmin = Bits(c.tex1, 6, 3);
        state.bilinear = k <= 0 ? Bits(c.tex1, 5, 1) != 0 : (mmin == 1 || mmin == 4 || mmin == 5);
        state.wms = Bits(c.clamp, 0, 2);
        state.wmt = Bits(c.clamp, 2, 2);
        state.minu = Bits(c.clamp, 4, 10);
        state.maxu = Bits(c.clamp, 14, 10);
        state.minv = Bits(c.clamp, 24, 10);
        state.maxv = Bits(c.clamp, 34, 10);
    }
    state.alpha_test = Bits(c.test, 0, 1) != 0;
    state.atst = Bits(c.test, 1, 3);
    state.aref = Bits(c.test, 4, 8);
    state.afail = Bits(c.test, 12, 2);
    state.date = Bits(c.test, 14, 1) != 0 && state.psm != gs::PSMCT24;
    state.datm = Bits(c.test, 15, 1);
    state.blend = (call.prim >> 6) & 1;
    state.a = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 0, 2)));
    state.b = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 2, 2)));
    state.c = ps2gpu::BlendAlpha(std::min(2u, Bits(c.alpha, 4, 2)));
    state.d = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 6, 2)));
    state.fix = Bits(c.alpha, 32, 8);
    state.pabe = call.pabe;
    state.fog = (call.prim >> 5) & 1;
    state.fogcol = u32(call.fogcol & 0xFFFFFF);
    // Depth: ZTE with ZBUF.
    const u32 zpsm = 0x30 | Bits(c.zbuf, 24, 4);
    const u32 zmax = zpsm == gs::PSMZ32 ? 0xFFFFFFFFu : zpsm == gs::PSMZ24 ? 0xFFFFFFu : 0xFFFFu;
    state.depth = Bits(c.test, 16, 1) != 0;
    state.ztst = Bits(c.test, 17, 2);
    state.zbp = Bits(c.zbuf, 0, 9);
    state.depth_write = Bits(c.zbuf, 32, 1) == 0;
    Owned* depth = nullptr;
    if (state.depth) {
        if (s.primary) {
            depth = &Use(s, state.zbp, state.fbw, zpsm, true, state.width, state.height, rows);
        } else if (s.depth_seeded.insert(state.zbp).second) {
            // Each depth buffer starts the frame as local memory has it, so the
            // GPU tests against what the software GS drew.
            std::vector<u32> z(std::size_t(state.width) * state.height);
            const gs::Gs& g = gs::TheGs();
            for (u32 y = 0; y < state.height; ++y)
                for (u32 x = 0; x < state.width; ++x)
                    z[std::size_t(y) * state.width + x] = g.memory.Read(state.zbp * 32, state.fbw, x, y, zpsm);
            s.renderer->UploadDepth(state.zbp, state.width, state.height, z.data());
        }
    }

    for (std::size_t i = 0; i < n; ++i) v[i].z = std::min(v[i].z, zmax);
    {
        Timed drawing(Section::GpuRender);
        s.renderer->DrawTriangles(state, v, n);
    }
    if (s.primary) {
        // FBMSK over every bit the format stores: the colour is not written.
        const u32 stored = state.psm == gs::PSMCT24 ? 0xFFFFFFu : Is16(state.psm) ? 0x80F8F8F8u : 0xFFFFFFFFu;
        if ((state.fbmsk & stored) != stored) Drew(s, *target, x1, y1);
        if (depth && state.depth_write) Drew(s, *depth, x1, y1);
        return;
    }
    Extent& e = s.drawn[std::make_tuple(state.fbp, state.fbw, state.psm)];
    e.x1 = std::max(e.x1, x1);
    e.y1 = std::max(e.y1, y1);
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

bool StartGpuRenderer(std::string* error, GpuMode mode, bool show) {
    static Bridge bridge;
    // On the window's device when there is a window: one Vulkan device for both.
    ps2gpu::Gpu* device = WindowGpu();
    if (!device) {
        bridge.gpu = ps2gpu::CreateGpu(error);
        if (!bridge.gpu) return false;
        device = bridge.gpu.get();
    }
    bridge.renderer = ps2gpu::CreateGsRenderer(*device, error);
    if (!bridge.renderer) return false;
    g_bridge = &bridge;
    bridge.primary = mode == GpuMode::Primary;
    bridge.targets_as_textures = mode == GpuMode::ShadowRtt;
    gs::Gs& g = gs::TheGs();
    g.on_draw = OnDraw;
    if (bridge.primary) {
        g.rasterize = false;
        g.on_memory = [](u32 begin, u32 end, bool write) { Sync(*g_bridge, begin, end, write); };
    }
    if (show || bridge.primary) {
        // The window shows the GPU's picture: the displayed buffer read back
        // from its target (the software picture where the GPU has none).
        SetPictureSource([](std::vector<u32>* pixels, u32* width, u32* height) {
            u32 fbp = 0, fbw = 0, psm = 0;
            if (!g_bridge || !gs::TheGs().Displayed(&fbp, &fbw, &psm)) return false;
            if (g_bridge->primary) {
                auto found = g_bridge->owned.find(std::make_tuple(fbp, fbw, psm, false));
                if (found == g_bridge->owned.end() || found->second.stale) return false;
            }
            std::vector<u32> rgba;
            u32 w = 0, h = 0;
            if (!g_bridge->renderer->ReadTarget(fbp, fbw, psm, &rgba, &w, &h)) return false;
            const u32 shown_w = std::min(w, 640u), shown_h = std::min(h, 448u);
            pixels->resize(std::size_t(shown_w) * shown_h);
            for (u32 y = 0; y < shown_h; ++y)
                for (u32 x = 0; x < shown_w; ++x) {
                    const u32 c = rgba[std::size_t(y) * w + x];
                    (*pixels)[std::size_t(y) * shown_w + x] = (c & 0xFF00FF00u) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
                }
            *width = shown_w;
            *height = shown_h;
            return true;
        });
    }
    std::fprintf(stderr, "gpu: drawing on %s %s\n", device->Describe().c_str(),
                 bridge.primary ? "instead of the software GS" : "beside the software GS");
    return true;
}

void GpuShadowFrame(u64 frame, const std::string& dump_dir) {
    if (!g_bridge) return;
    Bridge& s = *g_bridge;
    if (s.primary) {
        if (frame % 100 == 0)
            std::fprintf(stderr, "[gpu] frame %llu: %llu draws, %llu write-backs, %llu reloads, %llu decodes, %zu images, %zu textures\n",
                         static_cast<unsigned long long>(frame), static_cast<unsigned long long>(s.serial),
                         static_cast<unsigned long long>(s.write_backs), static_cast<unsigned long long>(s.reloads),
                         static_cast<unsigned long long>(s.decodes), s.owned.size(), s.textures.size());
        if (std::getenv("PS2HOST_GPU_LOG"))
            for (const auto& [key, o] : s.owned)
                std::fprintf(stderr, "[gpu]   %s %X/%u/%02X blocks %X-%X drawn to %X, %ux%u; reloads new %llu memory %llu draw %llu write-back %llu\n",
                             o.depth ? "Z" : "C", unsigned(o.fbp), unsigned(o.fbw), unsigned(o.psm), unsigned(o.begin),
                             unsigned(o.end), unsigned(o.drawn_end), unsigned(o.x1), unsigned(o.y1),
                             static_cast<unsigned long long>(o.reloads[0]), static_cast<unsigned long long>(o.reloads[1]),
                             static_cast<unsigned long long>(o.reloads[2]), static_cast<unsigned long long>(o.reloads[3]));
        return;
    }
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
    if (!g_bridge) return "";
    char text[192];
    if (g_bridge->primary) {
        std::snprintf(text, sizeof text, "gpu: %llu write-backs to local memory, %llu reloads from it, %llu textures decoded",
                      static_cast<unsigned long long>(g_bridge->write_backs),
                      static_cast<unsigned long long>(g_bridge->reloads),
                      static_cast<unsigned long long>(g_bridge->decodes));
        return text;
    }
    if (!g_bridge->frames) return "";
    std::snprintf(text, sizeof text, "gpu-shadow: %llu frames compared, %.1f%% of pixels agree on average",
                  static_cast<unsigned long long>(g_bridge->frames), 100.0 * g_bridge->agreement_sum / double(g_bridge->frames));
    return text;
}

#else

bool StartGpuRenderer(std::string* error, GpuMode, bool) {
    if (error) *error = "built without the GPU renderer (PS2HOST_VULKAN)";
    return false;
}
void GpuShadowFrame(gs::u64, const std::string&) {}
std::string GpuShadowSummary() { return ""; }

#endif

}  // namespace ps2host
