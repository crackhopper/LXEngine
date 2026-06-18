#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec2 outBrdf;

layout(set = 0, binding = 0) uniform sampler2D BakeMaterialSource;

void main() {
    vec2 source = texture(BakeMaterialSource, vUV).rg;
    outBrdf = mix(vUV, source, 0.001);
}
