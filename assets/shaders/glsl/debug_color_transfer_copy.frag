#version 450

#include "common/tone_mapping.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 0, binding = 1) uniform DebugColorTransferUBO {
    float exposure;
    int toneMappingMode;
    int outputEncodingMode;
    float gamma;
} debugTransfer;

void main() {
    vec3 linearLdr = texture(SceneColor, vUV).rgb;
    vec3 encoded = debugTransfer.outputEncodingMode == 1
                       ? lxLinearToSrgbGamma(linearLdr, debugTransfer.gamma)
                       : clamp(linearLdr, vec3(0.0), vec3(1.0));
    outColor = vec4(encoded, 1.0);
}
