#version 450

#include "features/bloom.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(bloom.threshold, bloom.intensity, bloom.radius, 1.0);
}
