#version 450

#include "features/skybox.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

void main() {
    vec4 viewPos = inverse(camera.proj) * vec4(vNdc, 1.0, 1.0);
    vec3 viewDir = normalize(viewPos.xyz / max(abs(viewPos.w), 0.0001));
    vec3 worldDir = normalize(transpose(mat3(camera.view)) * viewDir);
    outColor = vec4(lxEvaluateSkyboxRadiance(worldDir), 1.0);
}
