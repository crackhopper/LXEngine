#ifndef LX_FEATURE_BLOOM_GLSL
#define LX_FEATURE_BLOOM_GLSL

layout(set = 5, binding = 0) uniform BloomUBO {
  float threshold;
  float intensity;
  float radius;
} bloom;

vec3 lxApplyBloomBlit(vec3 color) {
  float brightness = max(max(color.r, color.g), color.b);
  float bloomMask = brightness > bloom.threshold ? 1.0 : 0.0;
  vec3 bloomColor = color * bloomMask * max(bloom.intensity, 0.0);
  return color + bloomColor * max(bloom.radius, 0.0);
}

#endif
