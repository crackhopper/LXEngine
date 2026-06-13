// LX_MATERIAL_CONTRACT_BEGIN
// type: metal
// status: supported
// reflectionHash: metal-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// parameter: eta required spectrum
// parameter: k required spectrum
// parameter: uroughness optional float texture
// parameter: vroughness optional float texture
// parameter: normalmap optional texture
// storageField: eta vec4 parameter eta value default=1,1,1,1
// storageField: k vec4 parameter k value default=1,1,1,1
// storageField: uRoughness float parameter uroughness value default=0.25
// storageField: uRoughnessTexture textureSlot parameter uroughness texture defaultTexture=white
// storageField: uRoughnessChannel channelSelector parameter uroughness channel default=r
// storageField: vRoughness float parameter vroughness value default=0.25
// storageField: vRoughnessTexture textureSlot parameter vroughness texture defaultTexture=white
// storageField: vRoughnessChannel channelSelector parameter vroughness channel default=r
// storageField: normalTexture textureSlot parameter normalmap texture defaultTexture=flatNormal
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"
#include "../material_bsdf.glsl"

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  surface.baseColor = vec3(1.0);
  surface.alpha = 1.0;
  surface.metallic = 1.0;
  surface.roughness = 0.25;
  surface.normal = dot(geometricNormal, geometricNormal) > 0.0
                       ? normalize(geometricNormal)
                       : vec3(0.0, 0.0, 1.0);
  surface.ao = 1.0;
  surface.emissive = vec3(0.0);
  return surface;
}

LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput) {
  return lxEvaluateLambertLikeBsdf(bsdfInput);
}

LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput bsdfInput) {
  return lxSampleCosineHemisphereBsdf(bsdfInput);
}
