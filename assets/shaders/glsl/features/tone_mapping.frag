#version 450

#include "features/tone_mapping.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(lxApplyToneMappingCurve(vec3(1.0), toneMapping.exposure,
                                            toneMapping.mode),
                    1.0);
}
