#version 450

#include "common/tone_mapping.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 0, binding = 1) uniform PostProcessUBO {
    float exposure;
    int toneMappingMode;
    int outputEncodingMode;
    float gamma;
    float bloomIntensity;
} postProcess;

layout(set = 0, binding = 2) uniform sampler2D BloomColor;

void main() {
    vec3 hdr = texture(SceneColor, vUV).rgb;
    hdr += texture(BloomColor, vUV).rgb * max(postProcess.bloomIntensity, 0.0);
    vec3 mapped = postProcess.toneMappingMode == 1
                      ? lxToneMapReinhard(hdr, postProcess.exposure)
                      : lxToneMapAces(hdr, postProcess.exposure);
    vec3 encoded = postProcess.outputEncodingMode == 1
                       ? lxLinearToSrgbGamma(mapped, postProcess.gamma)
                       : mapped;
    outColor = vec4(encoded, 1.0);
}
