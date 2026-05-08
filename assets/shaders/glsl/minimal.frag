#version 450

// REQ-DIAG-MIN: minimal fragment shader for the resize-flicker isolation
// demo. Just outputs the interpolated vertex color, no lighting, no
// texturing, no UBO. Pairs with minimal.vert.

layout(location = 0) in vec3 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(inColor, 1.0);
}
