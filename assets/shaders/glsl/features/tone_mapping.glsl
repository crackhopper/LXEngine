#ifndef LX_FEATURE_TONE_MAPPING_GLSL
#define LX_FEATURE_TONE_MAPPING_GLSL

layout(set = 4, binding = 0) uniform ToneMappingUBO {
  float exposure;
  int mode;
} toneMapping;

vec3 lxToneMapAces(vec3 color, float exposure) {
  vec3 exposed = max(color * max(exposure, 0.0), vec3(0.0));
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((exposed * (a * exposed + b)) /
                   (exposed * (c * exposed + d) + e),
               vec3(0.0), vec3(1.0));
}

vec3 lxToneMapReinhard(vec3 color, float exposure) {
  vec3 exposed = max(color * max(exposure, 0.0), vec3(0.0));
  return exposed / (exposed + vec3(1.0));
}

vec3 lxApplyToneMappingCurve(vec3 hdr, float exposure, int mode) {
  return mode == 1 ? lxToneMapReinhard(hdr, exposure)
                   : lxToneMapAces(hdr, exposure);
}

#endif
