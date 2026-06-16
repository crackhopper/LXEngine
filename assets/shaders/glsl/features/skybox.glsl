#ifndef LX_FEATURE_SKYBOX_GLSL
#define LX_FEATURE_SKYBOX_GLSL

layout(set = 1, binding = 0) uniform samplerCube SkyboxMap;

layout(set = 1, binding = 1) uniform SkyboxUBO {
  vec3 color;
  float intensity;
  float rotation;
} skybox;

vec3 lxSkyboxSampleDirection(float rotation) {
  return vec3(cos(rotation), 0.0, sin(rotation));
}

vec4 lxEvaluateVisibleSkybox() {
  vec3 radiance = texture(SkyboxMap, lxSkyboxSampleDirection(skybox.rotation)).rgb;
  return vec4(radiance * skybox.color * max(skybox.intensity, 0.0), 1.0);
}

#endif
