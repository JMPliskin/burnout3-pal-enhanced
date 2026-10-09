#version 450
// The GS's per-pixel work, in its own terms (0-255, with 0x80 as 1.0 where it
// multiplies): the texture function, then the alpha test. Blending is the
// fixed-function blender's; the GS's factor - source alpha or FIX over 128 -
// goes out as the second output (dual-source blending), since the alpha
// written is the source alpha as it is. Compiled a second time with
// SINGLE_OUTPUT for devices without dual-source blending, and a third with
// SHADER_BLEND: the GS's blend done here, from a copy of the target - for a
// factor the blender cannot have (FIX over 0x80: more than 1.0, which a
// fixed-point target clamps to 1).
#ifdef SHADER_BLEND
#define SINGLE_OUTPUT
layout(set = 2, binding = 0) uniform sampler2D destination;
#endif

layout(location = 0) in vec4 shade;
layout(location = 1) noperspective in vec3 coordinate;
layout(location = 2) noperspective in float fog;
layout(location = 3) flat in vec4 range;   // a sprite's S, T at its first and last pixels (gs.vert)

layout(set = 0, binding = 0) uniform sampler2D texture_image;
// The GPU's CLUT (decode.comp's), 256 entries as the GS expands them: a target
// read as PSMT8H (tex_format 4) indexes it by its alpha byte.
layout(std430, set = 1, binding = 0) readonly buffer Clut { uint clut[256]; };

layout(push_constant) uniform Params {
    layout(offset = 16) uint textured;
    uint tfx;           // 0 MODULATE, 1 DECAL, 2 HIGHLIGHT, 3 HIGHLIGHT2
    uint tcc;           // the texel's alpha counts
    uint atst;          // 0 NEVER, 1 ALWAYS, 2 LESS, 3 LEQUAL, 4 EQUAL, 5 GEQUAL, 6 GREATER, 7 NOTEQUAL
    uint aref;
    uint fix_factor;    // blend with FIX rather than the source alpha
    uint fix;
    uint afail;         // which pixels this pass draws: 0 passing, 1 failing, 2 all
    uint fge;           // fog
    uint fogcol;        // FOGCOL, R G B in bytes 0-2
    uint pabe;          // which pixels this pass draws: 0 all, 1 As >= 0x80, 2 As < 0x80
    uint bilinear;      // the four texels around, else the nearest
    uint wms, wmt;      // CLAMP: 0 REPEAT, 1 CLAMP, 2 REGION_CLAMP, 3 REGION_REPEAT
    uint minu, maxu, minv, maxv;
    uint tw, th;        // TEX0's size (0: the image's own)
    uint tex_format;    // a target read as 0 32-bit, 1 24-bit, 2 16-bit texels, 4 PSMT8H through the CLUT
    uint ta0, ta1, aem; // TEXA
    uint logic;         // FBMSK masking part of a channel: 1 the AND pass, 2 the OR pass, 0 neither
    uint keep;          // ...the bits FBMSK keeps, RGBA bytes as the target holds them
    uint scale;         // a target drawn larger (the internal resolution): its pixels to a GS texel
    uint blend_ops;     // bits 0-7, SHADER_BLEND: ALPHA's A, B, C, D, two bits each; 8-15: Snap's scale
} p;

// A texel coordinate wrapped as CLAMP says, as the software GS's Wrap. A
// target drawn larger (Fine) is addressed in its own pixels: its size and a
// region's bounds scaled; REGION_REPEAT's masks do not scale (Texel takes
// those in GS texels).
int Wrap(int i, int size, uint mode, uint lo, uint hi, int k) {
    if (mode == 0u) return k == 1 ? (i & (size - 1)) : ((i % size) + size) % size;
    if (mode == 1u) return clamp(i, 0, size - 1);
    if (mode == 2u) return clamp(i, int(lo) * k, int(hi) * k + k - 1);
    return (i & int(lo)) | int(hi);
}

// Whether the texture is a target drawn larger, addressed in its own pixels.
bool Fine() { return p.scale > 1u && p.wms != 3u && p.wmt != 3u; }

ivec2 TextureSize() {
    return p.tw != 0u ? ivec2(int(p.tw), int(p.th)) : textureSize(texture_image, 0);
}

vec4 Texel(int u, int v) {
    const int k = Fine() ? int(p.scale) : 1;
    ivec2 size = TextureSize() * k;
    ivec2 at = ivec2(Wrap(u, size.x, p.wms, p.minu, p.maxu, k), Wrap(v, size.y, p.wmt, p.minv, p.maxv, k));
    if (k == 1) at &= 2047;
    // A target drawn larger with REGION_REPEAT: the GS texel, and the middle
    // of its square.
    if (k == 1 && p.scale > 1u) at = at * int(p.scale) + int(p.scale / 2u);
    // A target may be smaller than TEX0 says: what lies beyond is not drawn here.
    at = min(at, textureSize(texture_image, 0) - 1);
    vec4 t = floor(texelFetch(texture_image, at, 0) * 255.0 + 0.5);
    if (p.tex_format == 4u) {                       // PSMT8H: the alpha byte indexes the CLUT
        const uint c = clut[uint(t.a) & 255u];
        return vec4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float(c >> 24));
    }
    bool black = t.r + t.g + t.b == 0.0;
    if (p.tex_format == 1u) {                       // 24-bit: TEXA's alpha
        t.a = (p.aem != 0u && black) ? 0.0 : float(p.ta0);
    } else if (p.tex_format == 2u) {                // 16-bit: 5 bits a channel, alpha by its bit
        bool bit = t.a >= 128.0;
        t.rgb = floor(t.rgb / 8.0) * 8.0;
        black = t.r + t.g + t.b == 0.0;
        t.a = bit ? float(p.ta1) : ((p.aem != 0u && black) ? 0.0 : float(p.ta0));
    }
    return t;
}

// The texel at (u, v) in texels: the nearest, or the four around (u - 0.5,
// v - 0.5) mixed and rounded per channel.
vec4 Sample(vec2 uv) {
    if (Fine()) uv *= float(p.scale);               // in the target's own pixels
    if (p.bilinear == 0u) return Texel(int(floor(uv.x)), int(floor(uv.y)));
    vec2 xy = uv - 0.5;
    ivec2 i0 = ivec2(floor(xy));
    vec2 f = xy - vec2(i0);
    // In the software GS's order of operations, so the two round alike.
    vec4 c00 = Texel(i0.x, i0.y), c10 = Texel(i0.x + 1, i0.y);
    vec4 c01 = Texel(i0.x, i0.y + 1), c11 = Texel(i0.x + 1, i0.y + 1);
    vec4 top = c00 + (c10 - c00) * f.x, bottom = c01 + (c11 - c01) * f.x;
    return floor(top + (bottom - top) * f.y + 0.5);
}

#ifdef SINGLE_OUTPUT
layout(location = 0) out vec4 colour;
#else
layout(location = 0, index = 0) out vec4 colour;
layout(location = 0, index = 1) out vec4 factor;
#endif

void main() {
    vec4 v = round(shade * 255.0);
    vec4 c = v;
    if (p.textured != 0u) {
        vec2 uv = coordinate.xy / coordinate.z * vec2(TextureSize());
        // A sprite on a target drawn larger: sampled as finely as the target
        // is drawn, within the coordinates the GS reads at its first and last
        // pixels (gs_renderer.cpp's DrawTriangles: further, where it meets
        // another), and - REPEAT, the sprite within one copy of the texture -
        // not wrapping round to its far side (a tile's edge: seams).
        const uint snap = (p.blend_ops >> 8) & 255u;
        if (snap > 1u) {
            const vec2 size = vec2(TextureSize());
            const vec2 lo = min(range.xy, range.zw), hi = max(range.xy, range.zw);
            uv = clamp(uv, lo * size, hi * size);
            const float inset = p.bilinear != 0u ? 0.5 : 0.001;
            if (p.wms == 0u && lo.x >= 0.0 && hi.x <= 1.0) uv.x = clamp(uv.x, inset, size.x - inset);
            if (p.wmt == 0u && lo.y >= 0.0 && hi.y <= 1.0) uv.y = clamp(uv.y, inset, size.y - inset);
        }
        vec4 t = Sample(uv);
        vec3 product = floor(t.rgb * v.rgb / 128.0);
        if (p.tfx == 0u) {
            c.rgb = product;
            c.a = p.tcc != 0u ? floor(t.a * v.a / 128.0) : v.a;
        } else if (p.tfx == 1u) {
            c.rgb = t.rgb;
            c.a = p.tcc != 0u ? t.a : v.a;
        } else if (p.tfx == 2u) {
            c.rgb = product + v.a;
            c.a = p.tcc != 0u ? t.a + v.a : v.a;
        } else {
            c.rgb = product + v.a;
            c.a = p.tcc != 0u ? t.a : v.a;
        }
        c = min(c, vec4(255.0));
    }

    if (p.fge != 0u) {
        // Towards FOGCOL as F falls, colour only.
        float f = round(fog * 255.0);
        vec3 fc = vec3(float(p.fogcol & 0xFFu), float((p.fogcol >> 8) & 0xFFu), float((p.fogcol >> 16) & 0xFFu));
        c.rgb = floor((f * c.rgb + (255.0 - f) * fc) / 256.0);
    }

    uint a = uint(c.a);
    bool pass = true;
    switch (p.atst) {
        case 0u: pass = false; break;
        case 2u: pass = a < p.aref; break;
        case 3u: pass = a <= p.aref; break;
        case 4u: pass = a == p.aref; break;
        case 5u: pass = a >= p.aref; break;
        case 6u: pass = a > p.aref; break;
        case 7u: pass = a != p.aref; break;
        default: break;
    }
    // The renderer draws AFAIL as passes: 0 keeps the passing pixels, 1 the
    // failing ones (with only what AFAIL writes), 2 all of them.
    if (p.afail == 0u && !pass) discard;
    if (p.afail == 1u && pass) discard;
    if (p.pabe == 1u && c.a < 128.0) discard;
    if (p.pabe == 2u && c.a >= 128.0) discard;

#ifdef SHADER_BLEND
    // Cv = ((A - B) * C >> 7) + D, clamped (COLCLAMP), as the software GS.
    vec4 dst = round(texelFetch(destination, ivec2(gl_FragCoord.xy), 0) * 255.0);
    uint ops = p.blend_ops & 255u;
    vec3 op_a = (ops & 3u) == 0u ? c.rgb : (ops & 3u) == 1u ? dst.rgb : vec3(0.0);
    vec3 op_b = ((ops >> 2) & 3u) == 0u ? c.rgb : ((ops >> 2) & 3u) == 1u ? dst.rgb : vec3(0.0);
    float op_c = ((ops >> 4) & 3u) == 0u ? c.a : ((ops >> 4) & 3u) == 1u ? dst.a : float(p.fix);
    vec3 op_d = ((ops >> 6) & 3u) == 0u ? c.rgb : ((ops >> 6) & 3u) == 1u ? dst.rgb : vec3(0.0);
    c.rgb = clamp(floor((op_a - op_b) * op_c / 128.0) + op_d, 0.0, 255.0);
#endif
    colour = c / 255.0;
    // A partly masked channel: the target ANDed with what FBMSK keeps, then
    // ORed with the rest of the source's bits (the pipelines' logic ops).
    if (p.logic == 1u) colour = unpackUnorm4x8(p.keep);
    if (p.logic == 2u) colour = unpackUnorm4x8(packUnorm4x8(c / 255.0) & ~p.keep);
#ifndef SINGLE_OUTPUT
    float k = p.fix_factor != 0u ? float(p.fix) : c.a;
    factor = vec4(k / 128.0);
#endif
}
