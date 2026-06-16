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
    float backgroundMode;
} environmentLighting;

layout(set = 3, binding = 0) uniform EnvironmentLightingFiniteBoxUBO {
    vec3 minBounds;
    float _pad0;
    vec3 maxBounds;
    float _pad1;
} finiteBox;

mat3 lxeYawRotation(float radians) {
    float c = cos(radians);
    float s = sin(radians);
    return mat3(c, 0.0, -s,
                0.0, 1.0, 0.0,
                s, 0.0, c);
}

bool lxeIntersectFiniteBox(vec3 rayOrigin, vec3 rayDirection, out vec3 hitPoint) {
    vec3 invDir = 1.0 / max(abs(rayDirection), vec3(0.000001)) * sign(rayDirection);
    vec3 t0 = (finiteBox.minBounds - rayOrigin) * invDir;
    vec3 t1 = (finiteBox.maxBounds - rayOrigin) * invDir;
    vec3 tMin3 = min(t0, t1);
    vec3 tMax3 = max(t0, t1);
    float tNear = max(max(tMin3.x, tMin3.y), tMin3.z);
    float tFar = min(min(tMax3.x, tMax3.y), tMax3.z);
    if (tFar < max(tNear, 0.0)) {
        return false;
    }
    float t = tNear > 0.0 ? tNear : tFar;
    hitPoint = rayOrigin + rayDirection * t;
    return true;
}

void main() {
    const float LxeBackgroundModeInfinite = 1.0;
    const float LxeBackgroundModeFiniteBox = 2.0;
    bool isInfinite =
        abs(environmentLighting.backgroundMode - LxeBackgroundModeInfinite) <= 0.5;
    bool isFiniteBox =
        abs(environmentLighting.backgroundMode - LxeBackgroundModeFiniteBox) <= 0.5;
    if (!isInfinite && !isFiniteBox) {
        discard;
    }

    vec4 viewPos = inverse(camera.proj) * vec4(vNdc, 1.0, 1.0);
    vec3 viewDir = normalize(viewPos.xyz / max(abs(viewPos.w), 0.0001));
    vec3 worldDir = normalize(transpose(mat3(camera.view)) * viewDir);
    vec3 sampleDir = worldDir;
    if (isFiniteBox) {
        vec3 hitPoint;
        if (!lxeIntersectFiniteBox(camera.eyePos, worldDir, hitPoint)) {
            discard;
        }
        vec3 center = 0.5 * (finiteBox.minBounds + finiteBox.maxBounds);
        sampleDir = normalize(hitPoint - center);
    }
    sampleDir = normalize(lxeYawRotation(environmentLighting.rotation) * sampleDir);

    EnvironmentLightingParams params;
    params.color = environmentLighting.color;
    params.intensity = environmentLighting.intensity;
    params.rotation = environmentLighting.rotation;
    params.backgroundMode = environmentLighting.backgroundMode;

    vec3 hdr = lxeApplyEnvironmentRadiance(texture(SkyboxMap, sampleDir).rgb,
                                           params);
    outColor = vec4(hdr, 1.0);
}
