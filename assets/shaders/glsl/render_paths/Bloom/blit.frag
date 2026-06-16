#version 450

#include "features/bloom.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

void main() {
    vec3 color = texture(SceneColor, vUV).rgb;
    outColor = vec4(lxApplyBloomBlit(color), 1.0);
}
