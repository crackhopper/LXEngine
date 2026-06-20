#ifndef LX_UNLIT_TEXTURE_RADIANCE_HIT_GLSL
#define LX_UNLIT_TEXTURE_RADIANCE_HIT_GLSL

// LX_HIT_SHADER_BEGIN
// payload: radiance
// function: lxHitUnlitTextureRadiance
// RenderFeature hitShaderTable entries and the software dispatch switch must
// stay aligned with this function name until hardware RT lowers the same table
// into shader binding table records.
// LX_HIT_SHADER_END

struct LxUnlitTextureRadianceHitInput {
  vec3 baseColor;
};

struct LxUnlitTextureRadianceHitResult {
  vec3 radiance;
};

LxUnlitTextureRadianceHitResult
lxHitUnlitTextureRadiance(LxUnlitTextureRadianceHitInput hitInput) {
  LxUnlitTextureRadianceHitResult result;
  result.radiance = max(hitInput.baseColor, vec3(0.0));
  return result;
}

#endif
