#version 450
// Untextured, unblended: the vertex colour as the GS writes it.

layout(location = 0) in vec4 shade;
layout(location = 0) out vec4 colour;

void main() {
    colour = shade;
}
