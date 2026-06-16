#version 450

#include "features/skybox.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = lxEvaluateVisibleSkybox();
}
