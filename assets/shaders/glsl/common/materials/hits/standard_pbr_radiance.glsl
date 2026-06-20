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
  vec3 worldPosition;
  vec3 worldNormal;
  vec3 viewDirection;
};

struct LxStandardPbrRadianceHitResult {
  vec3 radiance;
  vec3 nextDirection;
  float pdf;
};

LxStandardPbrRadianceHitResult
lxHitStandardPbrRadiance(LxStandardPbrRadianceHitInput hitInput) {
  LxStandardPbrRadianceHitResult result;
  result.radiance = vec3(0.0);
  result.nextDirection = normalize(hitInput.worldNormal);
  result.pdf = 1.0;
  return result;
}

#endif
