#version 450
// The GS's per-pixel work, in its own terms (0-255, with 0x80 as 1.0 where it
// multiplies): the texture function, then the alpha test. Blending is the
// fixed-function blender's; the GS's factor - source alpha or FIX over 128 -
// goes out as the second output (dual-source blending), since the alpha
// written is the source alpha as it is. Compiled a second time with
// SINGLE_OUTPUT for devices without dual-source blending.

layout(location = 0) in vec4 shade;
layout(location = 1) noperspective in vec3 coordinate;
layout(location = 2) noperspective in float fog;

layout(set = 0, binding = 0) uniform sampler2D texture_image;

layout(push_constant) uniform Params {
    layout(offset = 16) uint textured;
    uint tfx;           // 0 MODULATE, 1 DECAL, 2 HIGHLIGHT, 3 HIGHLIGHT2
    uint tcc;           // the texel's alpha counts
    uint atst;          // 0 NEVER, 1 ALWAYS, 2 LESS, 3 LEQUAL, 4 EQUAL, 5 GEQUAL, 6 GREATER, 7 NOTEQUAL
    uint aref;
    uint fix_factor;    // blend with FIX rather than the source alpha
    uint fix;
    uint afail;         // a failing pixel: 0 KEEP, 1 FB_ONLY, 2 ZB_ONLY, 3 RGB_ONLY
    uint fge;           // fog
    uint fogcol;        // FOGCOL, R G B in bytes 0-2
} p;

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
        vec4 t = round(texture(texture_image, coordinate.xy / coordinate.z) * 255.0);
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
    // Without depth on the GPU yet, FB_ONLY and RGB_ONLY write the colour.
    if (!pass && (p.afail == 0u || p.afail == 2u)) discard;

    colour = c / 255.0;
#ifndef SINGLE_OUTPUT
    float k = p.fix_factor != 0u ? float(p.fix) : c.a;
    factor = vec4(k / 128.0);
#endif
}
