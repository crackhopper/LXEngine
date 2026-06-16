#version 450

#include "features/environment_lighting.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(lxEvaluateEnvironmentLightingRadiance(), 1.0);
}
