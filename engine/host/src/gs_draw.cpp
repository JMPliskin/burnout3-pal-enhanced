/*
 * GS drawing (gs.h): primitives from the vertex queue, rasterized in software,
 * each pixel through the GS's pipeline (GS User's Manual, chapter 5):
 *
 *   scissor -> texture (TEX0/CLAMP/TEXA/CLUT; TFX modulate/decal/highlight)
 *   -> fog -> alpha test (TEST) -> depth test (ZBUF) -> blending (ALPHA,
 *   PABE, COLCLAMP) -> frame mask (FBMSK) -> FRAME and ZBUF writes.
 *
 * Stage one samples textures at the nearest texel and does not dither; the
 * destination alpha test, anti-aliasing and mipmapping are not modelled.
 */

#include <algorithm>
#include <cstdio>
#include <cmath>

#include "ps2host/gs.h"
#include "ps2host/profile.h"

namespace ps2host {
namespace gs {

namespace {

u32 Bits(u64 value, int low, int count) { return u32((value >> low) & ((1ull << count) - 1)); }

u32 Rgba(u32 r, u32 g, u32 b, u32 a) { return r | g << 8 | b << 16 | a << 24; }

u32 From16(u32 c, u64 texa) {
    const u32 r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
    const u32 a = (c & 0x8000) ? Bits(texa, 32, 8) : ((Bits(texa, 15, 1) && !(c & 0x7FFF)) ? 0 : Bits(texa, 0, 8));
    return Rgba(r, g, b, a);
}

u32 To16(u32 c) {
    return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10) | ((c >> 31) << 15);
}

int Wrap(int i, u32 size, u32 mode, u32 min, u32 max) {
    switch (mode) {
        case 0: return i & int(size - 1);                                   // REPEAT
        case 1: return std::clamp(i, 0, int(size) - 1);                     // CLAMP
        case 2: return std::clamp(i, int(min), int(max));                   // REGION_CLAMP
        default: return (i & int(min)) | int(max);                         // REGION_REPEAT
    }
}

}  // namespace

// ---------------------------------------------------------------- primitives

void Gs::Kick(bool draw) {
    const u32 type = Prim() & 7;
    auto shift = [&](int keep_first) {
        if (keep_first) { queue_[1] = queue_[2]; queued_ = 2; }
        else { queue_[0] = queue_[1]; queue_[1] = queue_[2]; queued_ = 2; }
    };
    switch (type) {
        case 0:                                         // point
            if (draw) Draw();
            queued_ = 0;
            break;
        case 1: case 6:                                 // line, sprite
            if (queued_ < 2) return;
            if (draw) Draw();
            queued_ = 0;
            break;
        case 2:                                         // line strip
            if (queued_ < 2) return;
            if (draw) Draw();
            queue_[0] = queue_[1];
            queued_ = 1;
            break;
        case 3:                                         // triangle
            if (queued_ < 3) return;
            if (draw) Draw();
            queued_ = 0;
            break;
        case 4:                                         // triangle strip
            if (queued_ < 3) return;
            if (draw) Draw();
            shift(0);
            break;
        case 5:                                         // triangle fan
            if (queued_ < 3) return;
            if (draw) Draw();
            shift(1);
            break;
        default:
            queued_ = 0;
            break;
    }
}

void Gs::DecodeTexture(std::vector<u32>* pixels, u32* width, u32* height) const {
    const u64 tex0 = Ctx().tex0;
    const u32 tw = 1u << std::min(Bits(tex0, 26, 4), 10u), th = 1u << std::min(Bits(tex0, 30, 4), 10u);
    pixels->resize(std::size_t(tw) * th);
    for (u32 v = 0; v < th; ++v)
        for (u32 u = 0; u < tw; ++u) (*pixels)[std::size_t(v) * tw + u] = TexelAt(int(u), int(v));
    *width = tw;
    *height = th;
}

bool Gs::Displayed(u32* fbp, u32* fbw, u32* psm) const {
    int circuit = Bits(pmode_, 1, 1) ? 1 : Bits(pmode_, 0, 1) ? 0 : -1;
    if (circuit < 0) {
        if (!dispfb_[1] && !dispfb_[0]) return false;
        circuit = dispfb_[1] ? 1 : 0;
    }
    const u64 fb = dispfb_[circuit];
    *fbp = Bits(fb, 0, 9);
    *fbw = Bits(fb, 9, 6);
    *psm = Bits(fb, 15, 5);
    return *fbw != 0;
}

u32 Gs::FramePixel(u32 fbp, u32 fbw, u32 psm, u32 x, u32 y) const {
    const u32 c = memory.Read(fbp * 32, fbw, x, y, psm);
    if (psm == PSMCT16 || psm == PSMCT16S) return From16(c, 0x80ull << 32);
    if (psm == PSMCT24) return (c & 0xFFFFFF) | 0x80000000u;
    return c;
}

void Gs::Draw() {
    Timed timed(Section::GsDraw);
    ++primitives_;
    if (on_draw) on_draw(DrawCall{Prim(), &Ctx(), texa_, queue_, queued_});
    if (trace) {
        const Context& c = Ctx();
        const Vertex& v = queue_[0];
        const Vertex& w = queue_[queued_ - 1];
        std::fprintf(stderr, "[gs] prim %llX tex0 tbp %X tbw %u psm %02X %ux%u cbp %X cpsm %u csm %u csa %u cld %u"
                     " | (%.1f,%.1f)-(%.1f,%.1f) uv (%u,%u)-(%u,%u) st (%.3f,%.3f)-(%.3f,%.3f) q %.3f-%.3f"
                     " rgba %02X%02X%02X%02X from %08X\n",
                     (unsigned long long)Prim(), Bits(c.tex0, 0, 14), Bits(c.tex0, 14, 6), Bits(c.tex0, 20, 6),
                     1u << Bits(c.tex0, 26, 4), 1u << Bits(c.tex0, 30, 4), Bits(c.tex0, 37, 14),
                     Bits(c.tex0, 51, 4), Bits(c.tex0, 55, 1), Bits(c.tex0, 56, 5), Bits(c.tex0, 61, 3),
                     v.x, v.y, w.x, w.y, v.u, v.v, w.u, w.v, v.s, v.t, w.s, w.t, v.q, w.q, w.r, w.g, w.b, w.a, rgba_source_);
        if (queued_ == 3)
            std::fprintf(stderr, "[gs]   middle vertex (%.1f,%.1f)\n", queue_[1].x, queue_[1].y);
        std::fprintf(stderr, "[gs]   frame fbp %X fbw %u psm %02X fbmsk %08X; zbuf zbp %X psm %02X zmsk %u; test %llX alpha %llX\n",
                     Bits(c.frame, 0, 9), Bits(c.frame, 16, 6), Bits(c.frame, 24, 6), Bits(c.frame, 32, 32),
                     Bits(c.zbuf, 0, 9), Bits(c.zbuf, 24, 4), Bits(c.zbuf, 32, 1),
                     (unsigned long long)c.test, (unsigned long long)c.alpha);
    }
    switch (Prim() & 7) {
        case 0: DrawLine(queue_[queued_ - 1], queue_[queued_ - 1]); break;
        case 1: case 2: DrawLine(queue_[0], queue_[1]); break;
        case 3: case 4: case 5: DrawTriangle(queue_[0], queue_[1], queue_[2]); break;
        case 6: DrawSprite(queue_[0], queue_[1]); break;
        default: break;
    }
}

namespace {

// Texture coordinates of a vertex in texels: UV directly (1/16 texel), or
// S/Q, T/Q scaled by the texture's size.
void Coords(const Vertex& v, bool fst, u32 tw, u32 th, float* u, float* t) {
    if (fst) { *u = float(v.u) / 16.0f; *t = float(v.v) / 16.0f; }
    else {
        const float q = v.q != 0.0f ? v.q : 1.0f;
        *u = v.s / q * float(tw);
        *t = v.t / q * float(th);
    }
}

}  // namespace

void Gs::DrawSprite(const Vertex& a, const Vertex& b) {
    const u64 prim = Prim();
    const bool tme = Bits(prim, 4, 1), fst = Bits(prim, 8, 1);
    const Context& c = Ctx();
    const u32 tw = 1u << std::min(Bits(c.tex0, 26, 4), 10u), th = 1u << std::min(Bits(c.tex0, 30, 4), 10u);
    float ua, va, ub, vb;
    Coords(a, fst, tw, th, &ua, &va);
    Coords(b, fst, tw, th, &ub, &vb);
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    const bool flip_x = a.x > b.x, flip_y = a.y > b.y;
    const int ix0 = int(std::ceil(x0)), ix1 = int(std::ceil(x1));
    const int iy0 = int(std::ceil(y0)), iy1 = int(std::ceil(y1));
    const float du = x1 > x0 ? (ub - ua) / (b.x - a.x) : 0, dv = y1 > y0 ? (vb - va) / (b.y - a.y) : 0;
    (void)flip_x;
    (void)flip_y;
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float u = ua + (float(x) - a.x) * du, v = va + (float(y) - a.y) * dv;
            Plot(x, y, b.z, b.r, b.g, b.b, b.a, 0, 0, 1, u, v, tme);
        }
}

void Gs::DrawTriangle(const Vertex& a, const Vertex& b, const Vertex& c) {
    const u64 prim = Prim();
    const bool iip = Bits(prim, 3, 1), tme = Bits(prim, 4, 1), fst = Bits(prim, 8, 1);
    const Context& ctx = Ctx();
    const u32 tw = 1u << std::min(Bits(ctx.tex0, 26, 4), 10u), th = 1u << std::min(Bits(ctx.tex0, 30, 4), 10u);
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0.0f) return;
    const u64 sc = ctx.scissor;
    const int minx = std::max(int(std::floor(std::min({a.x, b.x, c.x}))), int(Bits(sc, 0, 11)));
    const int maxx = std::min(int(std::ceil(std::max({a.x, b.x, c.x}))), int(Bits(sc, 16, 11)));
    const int miny = std::max(int(std::floor(std::min({a.y, b.y, c.y}))), int(Bits(sc, 32, 11)));
    const int maxy = std::min(int(std::ceil(std::max({a.y, b.y, c.y}))), int(Bits(sc, 48, 11)));
    float ua = 0, va = 0, ub = 0, vb = 0, uc = 0, vc = 0;
    if (fst) { Coords(a, true, tw, th, &ua, &va); Coords(b, true, tw, th, &ub, &vb); Coords(c, true, tw, th, &uc, &vc); }
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            const float px = float(x), py = float(y);
            float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const Vertex& last = c;
            u8 r = last.r, g = last.g, bl = last.b, al = last.a;
            if (iip) {
                r = u8(w0 * a.r + w1 * b.r + w2 * c.r);
                g = u8(w0 * a.g + w1 * b.g + w2 * c.g);
                bl = u8(w0 * a.b + w1 * b.b + w2 * c.b);
                al = u8(w0 * a.a + w1 * b.a + w2 * c.a);
            }
            // Depth in double, from the first vertex: 24 and 32-bit depths do not
            // survive float, and a flat 0xFFFFFF came out 0xFFFFFE, failing GEQUAL.
            const u32 z = u32(std::clamp(double(a.z) + double(w1) * (double(b.z) - double(a.z)) +
                                         double(w2) * (double(c.z) - double(a.z)), 0.0, 4294967295.0));
            if (fst) {
                Plot(x, y, z, r, g, bl, al, 0, 0, 1, w0 * ua + w1 * ub + w2 * uc, w0 * va + w1 * vb + w2 * vc, tme);
            } else {
                const float s = w0 * a.s + w1 * b.s + w2 * c.s, t = w0 * a.t + w1 * b.t + w2 * c.t;
                const float q = w0 * a.q + w1 * b.q + w2 * c.q;
                const float qq = q != 0.0f ? q : 1.0f;
                Plot(x, y, z, r, g, bl, al, s, t, q, s / qq * float(tw), t / qq * float(th), tme);
            }
        }
}

void Gs::DrawLine(const Vertex& a, const Vertex& b) {
    const u64 prim = Prim();
    const bool iip = Bits(prim, 3, 1), tme = Bits(prim, 4, 1), fst = Bits(prim, 8, 1);
    const Context& ctx = Ctx();
    const u32 tw = 1u << std::min(Bits(ctx.tex0, 26, 4), 10u), th = 1u << std::min(Bits(ctx.tex0, 30, 4), 10u);
    const int steps = std::max(1, int(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y))));
    float ua, va, ub, vb;
    Coords(a, fst, tw, th, &ua, &va);
    Coords(b, fst, tw, th, &ub, &vb);
    for (int i = 0; i <= steps; ++i) {
        const float f = float(i) / float(steps);
        const auto mix = [&](float p, float q) { return p + (q - p) * f; };
        const u8 r = iip ? u8(mix(a.r, b.r)) : b.r, g = iip ? u8(mix(a.g, b.g)) : b.g;
        const u8 bl = iip ? u8(mix(a.b, b.b)) : b.b, al = iip ? u8(mix(a.a, b.a)) : b.a;
        Plot(int(std::lround(mix(a.x, b.x))), int(std::lround(mix(a.y, b.y))), u32(double(a.z) + (double(b.z) - double(a.z)) * double(f)),
             r, g, bl, al, 0, 0, 1, mix(ua, ub), mix(va, vb), tme);
    }
}

// ---------------------------------------------------------------- pixels

// TEX1: MMAG (bit 5) filters magnification, MMIN (6-8) minification, chosen by
// the level of detail - K (bits 32-43, 4 fractional bits) here; LOD from Q is
// not modelled yet. Linear filtering samples the four texels around
// (u - 0.5, v - 0.5), as the GS does.
u32 Gs::Texel(float fu, float fv) const {
    const u64 tex1 = Ctx().tex1;
    const int k = int(s32(Bits(tex1, 32, 12) << 20) >> 20);
    const u32 mmin = Bits(tex1, 6, 3);
    const bool linear = k <= 0 ? Bits(tex1, 5, 1) != 0 : (mmin == 1 || mmin == 4 || mmin == 5);
    if (!linear) return TexelAt(int(std::floor(fu)), int(std::floor(fv)));
    const float x = fu - 0.5f, y = fv - 0.5f;
    const int u0 = int(std::floor(x)), v0 = int(std::floor(y));
    const float a = x - float(u0), b = y - float(v0);
    const u32 t00 = TexelAt(u0, v0), t10 = TexelAt(u0 + 1, v0);
    const u32 t01 = TexelAt(u0, v0 + 1), t11 = TexelAt(u0 + 1, v0 + 1);
    u32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float c00 = float((t00 >> shift) & 0xFF), c10 = float((t10 >> shift) & 0xFF);
        const float c01 = float((t01 >> shift) & 0xFF), c11 = float((t11 >> shift) & 0xFF);
        const float top = c00 + (c10 - c00) * a, bottom = c01 + (c11 - c01) * a;
        out |= u32(std::lround(top + (bottom - top) * b)) << shift;
    }
    return out;
}

u32 Gs::TexelAt(int iu, int iv) const {
    const Context& c = Ctx();
    const u64 tex0 = c.tex0, clamp = c.clamp;
    const u32 tbp = Bits(tex0, 0, 14), tbw = Bits(tex0, 14, 6), psm = Bits(tex0, 20, 6);
    const u32 tw = 1u << std::min(Bits(tex0, 26, 4), 10u), th = 1u << std::min(Bits(tex0, 30, 4), 10u);
    const int u = Wrap(iu, tw, Bits(clamp, 0, 2), Bits(clamp, 4, 10), Bits(clamp, 14, 10));
    const int v = Wrap(iv, th, Bits(clamp, 2, 2), Bits(clamp, 24, 10), Bits(clamp, 34, 10));
    const u32 raw = memory.Read(tbp, tbw, u32(u) & 2047, u32(v) & 2047, psm);
    switch (psm) {
        case PSMCT32: return raw;
        case PSMCT24: {
            const u32 rgb = raw & 0xFFFFFF;
            const u32 a = (Bits(texa_, 15, 1) && !rgb) ? 0 : Bits(texa_, 0, 8);
            return rgb | a << 24;
        }
        case PSMCT16: case PSMCT16S: return From16(raw, texa_);
        case PSMT8: case PSMT8H: return clut_[raw & 0xFF];
        case PSMT4: case PSMT4HL: case PSMT4HH: return clut_[(Bits(tex0, 56, 5) * 16 + (raw & 15)) & 0xFF];
        default: return raw;
    }
}

void Gs::Plot(int x, int y, u32 z, u8 r, u8 g, u8 b, u8 a, float s, float t, float q,
              float u, float v, bool textured) {
    (void)s; (void)t; (void)q;
    const Context& c = Ctx();
    const u64 prim = Prim();
    // Scissor.
    if (x < int(Bits(c.scissor, 0, 11)) || x > int(Bits(c.scissor, 16, 11)) ||
        y < int(Bits(c.scissor, 32, 11)) || y > int(Bits(c.scissor, 48, 11)) || x < 0 || y < 0)
        return;
    u32 cr = r, cg = g, cb = b, ca = a;
    if (textured) {
        const u32 texel = Texel(u, v);
        const u32 tr = texel & 0xFF, tg = (texel >> 8) & 0xFF, tb = (texel >> 16) & 0xFF, ta = texel >> 24;
        const bool tcc = Bits(c.tex0, 34, 1);
        switch (Bits(c.tex0, 35, 2)) {
            case 0:                                    // MODULATE
                cr = std::min(255u, tr * r >> 7); cg = std::min(255u, tg * g >> 7); cb = std::min(255u, tb * b >> 7);
                ca = tcc ? std::min(255u, ta * a >> 7) : a;
                break;
            case 1:                                    // DECAL
                cr = tr; cg = tg; cb = tb; ca = tcc ? ta : a;
                break;
            case 2:                                    // HIGHLIGHT
                cr = std::min(255u, (tr * r >> 7) + a); cg = std::min(255u, (tg * g >> 7) + a);
                cb = std::min(255u, (tb * b >> 7) + a); ca = tcc ? std::min(255u, ta + a) : a;
                break;
            default:                                   // HIGHLIGHT2
                cr = std::min(255u, (tr * r >> 7) + a); cg = std::min(255u, (tg * g >> 7) + a);
                cb = std::min(255u, (tb * b >> 7) + a); ca = tcc ? ta : a;
                break;
        }
    }
    // Alpha test.
    bool write_rgb = true, write_alpha = true, write_z = true;
    const u64 test = c.test;
    if (Bits(test, 0, 1)) {
        const u32 ref = Bits(test, 4, 8);
        bool pass = true;
        switch (Bits(test, 1, 3)) {
            case 0: pass = false; break;
            case 1: pass = true; break;
            case 2: pass = ca < ref; break;
            case 3: pass = ca <= ref; break;
            case 4: pass = ca == ref; break;
            case 5: pass = ca >= ref; break;
            case 6: pass = ca > ref; break;
            default: pass = ca != ref; break;
        }
        if (!pass) {
            switch (Bits(test, 12, 2)) {
                case 0: return;                                         // KEEP
                case 1: write_z = false; break;                         // FB_ONLY
                case 2: write_rgb = write_alpha = false; break;         // ZB_ONLY
                default: write_alpha = false; write_z = false; break;   // RGB_ONLY
            }
        }
    }
    // Destination alpha test: the frame's alpha bit (31 for 32-bit, 15 for 16-bit
    // formats; 24-bit frames have none) must equal DATM - a stencil games draw
    // into alpha first.
    if (Bits(test, 14, 1)) {
        const u32 fpsm = Bits(c.frame, 24, 6);
        if (fpsm != PSMCT24) {
            const u32 here = memory.Read(Bits(c.frame, 0, 9) * 32, Bits(c.frame, 16, 6), u32(x), u32(y), fpsm);
            const u32 bit = (fpsm == PSMCT16 || fpsm == PSMCT16S) ? (here >> 15) & 1 : here >> 31;
            if (bit != Bits(test, 15, 1)) return;
        }
    }
    // Depth test.
    const u64 zbuf = c.zbuf;
    const u32 zbp = Bits(zbuf, 0, 9) * 32, zpsm = Bits(zbuf, 24, 4) | 0x30;
    const u32 fbw = Bits(c.frame, 16, 6);
    if (Bits(test, 16, 1)) {
        const u32 old = memory.Read(zbp, fbw, u32(x), u32(y), zpsm);
        bool pass = true;
        switch (Bits(test, 17, 2)) {
            case 0: pass = false; break;
            case 1: pass = true; break;
            case 2: pass = z >= old; break;
            default: pass = z > old; break;
        }
        if (!pass) return;
    } else {
        write_z = false;
    }
    if (Bits(zbuf, 32, 1)) write_z = false;
    // Blending.
    const u32 fbp = Bits(c.frame, 0, 9) * 32, fpsm = Bits(c.frame, 24, 6);
    u32 dest = memory.Read(fbp, fbw, u32(x), u32(y), fpsm);
    if (fpsm == PSMCT16 || fpsm == PSMCT16S) dest = From16(dest, 0x80ull << 32);
    if (fpsm == PSMCT24) dest |= 0x80u << 24;
    const bool blend = Bits(prim, 6, 1) && !(Bits(pabe_, 0, 1) && !(ca & 0x80));
    if (blend) {
        const u64 al = c.alpha;
        const u32 dr = dest & 0xFF, dg = (dest >> 8) & 0xFF, db = (dest >> 16) & 0xFF, da = dest >> 24;
        const u32 sel_c = Bits(al, 4, 2);
        const int factor = sel_c == 0 ? int(ca) : sel_c == 1 ? int(da) : int(Bits(al, 32, 8));
        auto pick = [&](u32 sel, u32 src, u32 dst) { return sel == 0 ? int(src) : sel == 1 ? int(dst) : 0; };
        auto mix = [&](u32 src, u32 dst) {
            int value = ((pick(Bits(al, 0, 2), src, dst) - pick(Bits(al, 2, 2), src, dst)) * factor >> 7) +
                        pick(Bits(al, 6, 2), src, dst);
            return Bits(colclamp_, 0, 1) ? u32(std::clamp(value, 0, 255)) : u32(value) & 0xFF;
        };
        cr = mix(cr, dr);
        cg = mix(cg, dg);
        cb = mix(cb, db);
    }
    if (Bits(c.fba, 0, 1)) ca |= 0x80;
    u32 out = Rgba(cr, cg, cb, ca);
    u32 mask = u32(c.frame >> 32);
    if (!write_rgb) mask |= 0x00FFFFFF;
    if (!write_alpha) mask |= 0xFF000000;
    out = (out & ~mask) | (dest & mask);
    if (mask != 0xFFFFFFFF) {
        if (fpsm == PSMCT16 || fpsm == PSMCT16S) memory.Write(fbp, fbw, u32(x), u32(y), fpsm, To16(out));
        else memory.Write(fbp, fbw, u32(x), u32(y), fpsm, out);
        ++pixels_;
    }
    if (write_z) memory.Write(zbp, fbw, u32(x), u32(y), zpsm, z);
}

}  // namespace gs
}  // namespace ps2host
