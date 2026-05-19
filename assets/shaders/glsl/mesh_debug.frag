#version 450

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform MeshOverlayUBO {
    vec4 color;
} overlay;

void main() {
    outColor = overlay.color;
}
