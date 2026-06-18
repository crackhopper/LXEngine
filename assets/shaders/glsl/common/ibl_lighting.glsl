#ifndef LX_COMMON_IBL_LIGHTING_GLSL
#define LX_COMMON_IBL_LIGHTING_GLSL

#include "common/pbr.glsl"
#include "features/surface_lighting.glsl"

layout(set = 3, binding = 0) uniform samplerCube IrradianceMap;
layout(set = 3, binding = 1) uniform samplerCube PrefilteredEnvMap;
layout(set = 3, binding = 2) uniform sampler2D BrdfLut;

vec3 evaluateIblStandardPbr(vec3 baseColor,
                            float metallic,
                            float roughness,
                            float ao,
                            vec3 N,
                            vec3 V,
                            float NdotV,
                            vec3 F0) {
  if (!lxSurfaceLightingStandardPbrIblReady()) {
    return vec3(0.0);
  }

  float clampedMetallic = clamp(metallic, 0.0, 1.0);
  float clampedRoughness = clamp(roughness, 0.04, 1.0);
  float clampedAo = clamp(ao, 0.0, 1.0);

  vec3 F = lxFresnelSchlickRoughness(NdotV, F0, clampedRoughness);
  vec3 kD = (vec3(1.0) - F) * (1.0 - clampedMetallic);

  vec3 irradiance = texture(IrradianceMap, N).rgb;
  vec3 diffuse = irradiance * baseColor *
                 max(surfaceLighting.diffuseIblIntensity, 0.0);

  vec3 R = reflect(-V, N);
  float maxMip = max(float(textureQueryLevels(PrefilteredEnvMap)) - 1.0, 0.0);
  vec3 prefilteredColor =
      textureLod(PrefilteredEnvMap, R, clampedRoughness * maxMip).rgb;
  vec2 brdf = texture(BrdfLut, vec2(NdotV, clampedRoughness)).rg;
  vec3 specular =
      prefilteredColor * (F * brdf.x + brdf.y) *
      max(surfaceLighting.specularIblIntensity, 0.0);

  return (kD * diffuse + specular) * clampedAo;
}

#endif
