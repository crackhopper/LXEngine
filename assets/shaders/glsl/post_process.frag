#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 0, binding = 1) uniform PostProcessUBO {
    float exposure;
    int toneMappingMode;
    float gamma;
    float bloomIntensity;
} postProcess;

layout(set = 0, binding = 2) uniform sampler2D BloomColor;

vec3 reinhardToneMap(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 acesToneMap(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e),
                 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(SceneColor, vUV).rgb;
    hdr += texture(BloomColor, vUV).rgb * max(postProcess.bloomIntensity, 0.0);
    hdr *= max(postProcess.exposure, 0.0);
    vec3 mapped = postProcess.toneMappingMode == 1
                      ? reinhardToneMap(hdr)
                      : acesToneMap(hdr);
    float gammaValue = max(postProcess.gamma, 0.0001);
    outColor = vec4(pow(mapped, vec3(1.0 / gammaValue)), 1.0);
}
