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

vec3 lxSkyboxRotateDirection(vec3 direction, float rotation) {
  float c = cos(rotation);
  float s = sin(rotation);
  mat3 yaw = mat3(c, 0.0, -s,
                  0.0, 1.0, 0.0,
                  s, 0.0, c);
  return normalize(yaw * direction);
}

vec3 lxEvaluateSkyboxRadiance(vec3 direction) {
  vec3 sampleDir = lxSkyboxRotateDirection(normalize(direction), skybox.rotation);
  vec3 radiance = texture(SkyboxMap, sampleDir).rgb;
  return radiance * skybox.color * max(skybox.intensity, 0.0);
}

vec3 lxEvaluateSkyboxRadianceLod(vec3 direction, float lod) {
  vec3 sampleDir = lxSkyboxRotateDirection(normalize(direction), skybox.rotation);
  vec3 radiance = textureLod(SkyboxMap, sampleDir, max(lod, 0.0)).rgb;
  return radiance * skybox.color * max(skybox.intensity, 0.0);
}

vec3 lxEvaluateSkyboxRadianceMaxMip(vec3 direction) {
  int levels = textureQueryLevels(SkyboxMap);
  return lxEvaluateSkyboxRadianceLod(direction, float(max(levels - 1, 0)));
}

vec4 lxEvaluateVisibleSkybox() {
  return vec4(lxEvaluateSkyboxRadiance(lxSkyboxSampleDirection(0.0)), 1.0);
}

#endif
