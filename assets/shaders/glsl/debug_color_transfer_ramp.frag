#version 450

#include "common/tone_mapping.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform DebugColorTransferUBO {
    float exposure;
    int toneMappingMode;
    int outputEncodingMode;
    float gamma;
} debugTransfer;

float fixedProbe(float x) {
    if (x < 0.2) {
        return 0.0;
    }
    if (x < 0.4) {
        return 0.18;
    }
    if (x < 0.6) {
        return 0.5;
    }
    if (x < 0.8) {
        return 1.0;
    }
    return clamp((x - 0.8) / 0.2, 0.0, 1.0);
}

void main() {
    float linearValue = fixedProbe(vUV.x);
    vec3 linearColor = vec3(linearValue);
    vec3 encoded = debugTransfer.outputEncodingMode == 1
                       ? lxLinearToSrgbGamma(linearColor, debugTransfer.gamma)
                       : linearColor;
    outColor = vec4(encoded, 1.0);
}
