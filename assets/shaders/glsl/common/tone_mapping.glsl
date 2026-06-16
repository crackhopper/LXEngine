#ifndef LX_TONE_MAPPING_GLSL
#define LX_TONE_MAPPING_GLSL

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

vec3 lxLinearToSrgbGamma(vec3 color, float gamma) {
  return pow(clamp(color, vec3(0.0), vec3(1.0)),
             vec3(1.0 / max(gamma, 0.0001)));
}

vec3 lxToneMapLinearToSrgb(vec3 color, float exposure, float gamma) {
  return lxLinearToSrgbGamma(lxToneMapAces(color, exposure), gamma);
}

struct LxToneMappingParams {
  float enabled;
  float exposure;
  float mode;
  float gamma;
};

vec3 lxApplyToneMapping(vec3 hdr, LxToneMappingParams params) {
  if (params.enabled < 0.5) {
    return hdr;
  }
  return abs(params.mode - 1.0) < 0.5
             ? lxToneMapReinhard(hdr, params.exposure)
             : lxToneMapAces(hdr, params.exposure);
}

#endif
