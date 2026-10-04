#version 450
// A GS vertex: position in pixels, colour 0-255 (normalised by the attribute
// format), texture coordinate S, T, Q. The GS samples a pixel at its whole
// coordinates and Vulkan at its centre, so positions move half a pixel.

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec3 stq;
layout(location = 3) in uint z;     // the GS's Z, unsigned; larger is nearer
layout(location = 4) in float fog;  // F, 0-1

layout(push_constant) uniform Target {
    vec2 size;          // the target, in pixels
} target;

layout(location = 0) out vec4 shade;
// The GS interpolates S, T and Q linearly across the screen and divides per
// pixel: no perspective correction of its own.
layout(location = 1) noperspective out vec3 coordinate;
layout(location = 2) noperspective out float fog_out;

void main() {
    vec2 ndc = (position + 0.5) / target.size * 2.0 - 1.0;
    gl_Position = vec4(ndc, float(z) / 4294967296.0, 1.0);
    shade = colour;
    coordinate = stq;
    fog_out = fog;
}
