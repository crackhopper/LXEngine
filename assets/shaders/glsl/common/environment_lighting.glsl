#ifndef LXE_COMMON_ENVIRONMENT_LIGHTING_GLSL
#define LXE_COMMON_ENVIRONMENT_LIGHTING_GLSL

struct EnvironmentLightingParams {
    vec3 color;
    float intensity;
    float rotation;
    float backgroundMode;
};

vec3 lxeApplyEnvironmentRadiance(vec3 sampleRadiance,
                                 EnvironmentLightingParams params) {
    return sampleRadiance * params.color * max(params.intensity, 0.0);
}

#endif
