#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    float nDotV = clamp(vUV.x, 0.0, 1.0);
    float roughness = clamp(vUV.y, 0.0, 1.0);
    float scale = 1.0 - 0.5 * roughness;
    float bias = 0.04 * (1.0 - nDotV) * (1.0 - roughness);
    outColor = vec4(scale, bias, 0.0, 1.0);
}
