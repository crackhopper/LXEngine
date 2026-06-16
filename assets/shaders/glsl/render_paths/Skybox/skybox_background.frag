#version 450

#include "common/environment_lighting.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 1, binding = 0) uniform samplerCube SkyboxMap;

layout(set = 2, binding = 0) uniform EnvironmentLightingUBO {
    vec3 color;
    float intensity;
    float rotation;
    float visibleInBackground;
} environmentLighting;

mat3 lxeYawRotation(float radians) {
    float c = cos(radians);
    float s = sin(radians);
    return mat3(c, 0.0, -s,
                0.0, 1.0, 0.0,
                s, 0.0, c);
}

void main() {
    if (environmentLighting.visibleInBackground <= 0.5) {
        discard;
    }

    vec4 viewPos = inverse(camera.proj) * vec4(vNdc, 1.0, 1.0);
    vec3 viewDir = normalize(viewPos.xyz / max(abs(viewPos.w), 0.0001));
    vec3 worldDir = normalize(transpose(mat3(camera.view)) * viewDir);
    worldDir = normalize(lxeYawRotation(environmentLighting.rotation) * worldDir);

    EnvironmentLightingParams params;
    params.color = environmentLighting.color;
    params.intensity = environmentLighting.intensity;
    params.rotation = environmentLighting.rotation;
    params.visibleInBackground = environmentLighting.visibleInBackground;

    vec3 hdr = lxeApplyEnvironmentRadiance(texture(SkyboxMap, worldDir).rgb,
                                           params);
    outColor = vec4(hdr, 1.0);
}
