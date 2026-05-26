#version 450

layout(location = 0) in vec3 vDirection;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube SkyboxMap;
layout(set = 0, binding = 2) uniform PrefilterUBO {
    float roughness;
    float sourceMipCount;
    float sampleCount;
    float padding;
} prefilter;

void main() {
    vec3 direction = normalize(vDirection);
    float lod = clamp(prefilter.roughness, 0.0, 1.0) *
                max(prefilter.sourceMipCount - 1.0, 0.0);
    outColor = vec4(textureLod(SkyboxMap, direction, lod).rgb, 1.0);
}
