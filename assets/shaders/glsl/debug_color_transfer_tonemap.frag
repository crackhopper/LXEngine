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
    vec3 hdr = texture(SceneColor, vUV).rgb;
    vec3 mapped = debugTransfer.toneMappingMode == 1
                      ? lxToneMapReinhard(hdr, debugTransfer.exposure)
                      : lxToneMapAces(hdr, debugTransfer.exposure);
    outColor = vec4(mapped, 1.0);
}
