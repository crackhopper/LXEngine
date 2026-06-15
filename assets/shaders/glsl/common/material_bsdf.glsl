#ifndef LX_COMMON_MATERIAL_BSDF_GLSL
#define LX_COMMON_MATERIAL_BSDF_GLSL

const float LX_BSDF_PI = 3.14159265359;

struct LxBsdfEvaluateInput {
  vec3 normal;
  vec3 wi;
  vec3 wo;
  vec3 baseColor;
  float metallic;
  float roughness;
  float ao;
  vec3 emissive;
};

struct LxBsdfEvaluateOutput {
  vec3 value;
};

struct LxBsdfSampleInput {
  vec3 normal;
  vec3 wo;
  vec2 xi;
};

struct LxBsdfSampleOutput {
  vec3 wi;
  vec3 value;
  float pdf;
};

vec3 lxBsdfSafeNormalize(vec3 value, vec3 fallback) {
  float len2 = dot(value, value);
  if (len2 <= 1.0e-8) {
    return fallback;
  }
  return value * inversesqrt(len2);
}

mat3 lxBsdfTangentFrame(vec3 normal) {
  vec3 n = lxBsdfSafeNormalize(normal, vec3(0.0, 0.0, 1.0));
  vec3 helper = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0)
                                 : vec3(0.0, 1.0, 0.0);
  vec3 tangent = lxBsdfSafeNormalize(cross(helper, n), vec3(1.0, 0.0, 0.0));
  vec3 bitangent = normalize(cross(n, tangent));
  return mat3(tangent, bitangent, n);
}

LxBsdfEvaluateOutput lxEvaluateLambertLikeBsdf(
    LxBsdfEvaluateInput bsdfInput) {
  vec3 n = lxBsdfSafeNormalize(bsdfInput.normal, vec3(0.0, 0.0, 1.0));
  float nDotWi = max(dot(n, lxBsdfSafeNormalize(bsdfInput.wi, n)), 0.0);
  float nDotWo = max(dot(n, lxBsdfSafeNormalize(bsdfInput.wo, n)), 0.0);

  LxBsdfEvaluateOutput result;
  result.value = nDotWi > 0.0 && nDotWo > 0.0
                     ? max(bsdfInput.baseColor, vec3(0.0)) / LX_BSDF_PI
                     : vec3(0.0);
  return result;
}

LxBsdfSampleOutput lxSampleCosineHemisphereBsdf(
    LxBsdfSampleInput bsdfInput) {
  vec2 xi = clamp(bsdfInput.xi, vec2(0.0), vec2(0.999999));
  float r = sqrt(xi.x);
  float phi = 2.0 * LX_BSDF_PI * xi.y;
  vec3 localWi =
      vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - xi.x, 0.0)));
  vec3 n = lxBsdfSafeNormalize(bsdfInput.normal, vec3(0.0, 0.0, 1.0));

  LxBsdfSampleOutput result;
  result.wi = normalize(lxBsdfTangentFrame(n) * localWi);
  result.value = vec3(1.0 / LX_BSDF_PI);
  result.pdf = max(dot(n, result.wi), 0.0) / LX_BSDF_PI;
  return result;
}

#endif
