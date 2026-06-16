#version 450

#include "common/environment_lighting.glsl"
#include "common/tone_mapping.glsl"

layout(location = 0) in vec3 vWorldPos;
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
    float backgroundMode;
} environmentLighting;

layout(set = 3, binding = 0) uniform EnvironmentLightingFiniteBoxUBO {
    vec3 minBounds;
    float _pad0;
    vec3 maxBounds;
    float _pad1;
} finiteBox;

layout(set = 4, binding = 0) uniform ToneMappingUBO {
    vec4 params; // x: enabled, y: exposure, z: mode, w: gamma
} toneMapping;

mat3 lxeYawRotation(float radians) {
    float c = cos(radians);
    float s = sin(radians);
    return mat3(c, 0.0, -s,
                0.0, 1.0, 0.0,
                s, 0.0, c);
}

void main() {
    const float LxeBackgroundModeFiniteBox = 2.0;
    if (abs(environmentLighting.backgroundMode - LxeBackgroundModeFiniteBox) > 0.5) {
        discard;
    }
    bool cameraInsideBox = !any(lessThan(camera.eyePos, finiteBox.minBounds)) &&
                           !any(greaterThan(camera.eyePos, finiteBox.maxBounds));
    if (!cameraInsideBox) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 center = 0.5 * (finiteBox.minBounds + finiteBox.maxBounds);
    vec3 sampleDir = normalize(vWorldPos - center);
    sampleDir = normalize(lxeYawRotation(environmentLighting.rotation) * sampleDir);

    EnvironmentLightingParams params;
    params.color = environmentLighting.color;
    params.intensity = environmentLighting.intensity;
    params.rotation = environmentLighting.rotation;
    params.backgroundMode = environmentLighting.backgroundMode;

    vec3 hdr = lxeApplyEnvironmentRadiance(texture(SkyboxMap, sampleDir).rgb,
                                           params);
    LxToneMappingParams toneParams;
    toneParams.enabled = toneMapping.params.x;
    toneParams.exposure = toneMapping.params.y;
    toneParams.mode = toneMapping.params.z;
    toneParams.gamma = toneMapping.params.w;
    outColor = vec4(lxApplyToneMapping(hdr, toneParams), 1.0);
}
