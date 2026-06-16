#ifndef LX_FEATURE_ENVIRONMENT_LIGHTING_GLSL
#define LX_FEATURE_ENVIRONMENT_LIGHTING_GLSL

layout(set = 3, binding = 0) uniform samplerCube SkyboxMap;

layout(set = 3, binding = 3) uniform EnvironmentLightingUBO {
  vec3 color;
  float intensity;
  float rotation;
} environmentLighting;

vec3 lxEnvironmentLightingSampleDirection(float rotation) {
  return vec3(cos(rotation), 0.0, sin(rotation));
}

vec3 lxEvaluateEnvironmentLightingRadiance() {
  vec3 radiance = texture(SkyboxMap,
                          lxEnvironmentLightingSampleDirection(
                              environmentLighting.rotation))
                      .rgb;
  return radiance * environmentLighting.color *
         max(environmentLighting.intensity, 0.0);
}

#endif
