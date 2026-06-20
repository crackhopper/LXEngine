#ifndef LX_STANDARD_PBR_RADIANCE_HIT_GLSL
#define LX_STANDARD_PBR_RADIANCE_HIT_GLSL

// LX_HIT_SHADER_BEGIN
// payload: radiance
// function: lxHitStandardPbrRadiance
// RenderFeature hitShaderTable entries and the software dispatch switch must
// stay aligned with this function name until hardware RT lowers the same table
// into shader binding table records.
// LX_HIT_SHADER_END

struct LxStandardPbrRadianceHitInput {
  LxMaterialSurface surface;
  vec3 worldPosition;
  vec3 worldNormal;
  vec3 viewDirection;
  vec3 lightDirection;
  vec3 lightColor;
  float lightIntensity;
  float visibility;
};

struct LxStandardPbrRadianceHitResult {
  vec3 radiance;
  vec3 nextDirection;
  float pdf;
};

LxStandardPbrRadianceHitResult
lxHitStandardPbrRadiance(LxStandardPbrRadianceHitInput hitInput) {
  LxPbrDirectInput pbrInput;
  pbrInput.baseColor = max(hitInput.surface.baseColor, vec3(0.0));
  pbrInput.normal = normalize(hitInput.surface.normal);
  pbrInput.viewDir = normalize(hitInput.viewDirection);
  pbrInput.lightDir = normalize(hitInput.lightDirection);
  pbrInput.lightColor = vec3(1.0);
  pbrInput.metallic = clamp(hitInput.surface.metallic, 0.0, 1.0);
  pbrInput.roughness = clamp(hitInput.surface.roughness, 0.04, 1.0);
  pbrInput.ao = clamp(hitInput.surface.ao, 0.0, 1.0);
  pbrInput.emissive = max(hitInput.surface.emissive, vec3(0.0));

  float NdotL = max(dot(pbrInput.normal, pbrInput.lightDir), 0.0);

  LxStandardPbrRadianceHitResult result;
  result.radiance =
      lxPbrDirectBrdf(pbrInput) * hitInput.lightColor *
          hitInput.lightIntensity * hitInput.visibility * NdotL * pbrInput.ao +
      pbrInput.emissive;
  result.nextDirection = normalize(hitInput.worldNormal);
  result.pdf = 1.0;
  return result;
}

#endif
