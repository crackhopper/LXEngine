// LX_MATERIAL_CONTRACT_BEGIN
// type: substrate
// status: supported
// reflectionHash: substrate-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// parameter: Kd required rgb texture spectrum
// parameter: Ks required rgb texture spectrum
// parameter: uroughness required float texture
// parameter: vroughness required float texture
// parameter: normalmap optional texture
// storageField: baseColor vec4 parameter Kd value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter Kd texture defaultTexture=white
// storageField: baseColorChannel channelSelector parameter Kd channel default=rgba
// storageField: specular vec4 parameter Ks value default=1,1,1,1
// storageField: specularTexture textureSlot parameter Ks texture defaultTexture=white
// storageField: specularChannel channelSelector parameter Ks channel default=rgba
// storageField: uRoughness float parameter uroughness value default=0.5
// storageField: uRoughnessTexture textureSlot parameter uroughness texture defaultTexture=white
// storageField: uRoughnessChannel channelSelector parameter uroughness channel default=r
// storageField: vRoughness float parameter vroughness value default=0.5
// storageField: vRoughnessTexture textureSlot parameter vroughness texture defaultTexture=white
// storageField: vRoughnessChannel channelSelector parameter vroughness channel default=r
// storageField: normalTexture textureSlot parameter normalmap texture defaultTexture=flatNormal
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"
#include "../material_bsdf.glsl"

uint lxGetMaterialType() {
  return LX_MATERIAL_TYPE_LIT;
}

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  surface.baseColor = vec3(1.0);
  surface.alpha = 1.0;
  surface.metallic = 0.0;
  surface.roughness = 0.5;
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
