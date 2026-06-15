// LX_MATERIAL_CONTRACT_BEGIN
// type: standard-pbr
// status: supported
// reflectionHash: standard-pbr-source-contract-v1
// storageAbiHash: standard-pbr-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: baseColor optional rgb
// parameter: baseColorTexture optional texture
// parameter: metallic optional float
// parameter: metallicRoughnessTexture optional texture
// parameter: roughness optional float
// parameter: normalTexture optional texture
// parameter: occlusionTexture optional texture
// parameter: emissive optional rgb
// parameter: emissiveTexture optional texture
// parameter: alphaMode optional string
// parameter: alphaCutoff optional float
// storageField: baseColor vec4 parameter baseColor value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter baseColorTexture texture defaultTexture=white
// storageField: metallic float parameter metallic value default=1
// storageField: metallicRoughnessTexture textureSlot parameter metallicRoughnessTexture texture defaultTexture=white
// storageField: roughness float parameter roughness value default=1
// storageField: normalTexture textureSlot parameter normalTexture texture defaultTexture=flatNormal
// storageField: occlusionTexture textureSlot parameter occlusionTexture texture defaultTexture=white
// storageField: emissive vec4 parameter emissive value default=0,0,0,0
// storageField: emissiveTexture textureSlot parameter emissiveTexture texture defaultTexture=black
// storageField: alphaMode flags parameter alphaMode value default=0
// storageField: alphaCutoff float parameter alphaCutoff value default=0.5
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"
#include "../material_bsdf.glsl"
#include "../pbr.glsl"

#ifndef LX_STANDARD_PBR_SOURCE_RECORDS_DECLARED
#define LX_STANDARD_PBR_SOURCE_RECORDS_DECLARED
struct LxSceneMaterialRefRecord {
  uint sourceStorageIndex;
  uint sourceLocalMaterialIndex;
  uint reserved0;
  uint reserved1;
};

struct LxStandardPbrSourceRecord {
  vec4 baseColor;
  uint baseColorTexture;
  float metallic;
  uint metallicRoughnessTexture;
  float roughness;
  uint normalTexture;
  uint occlusionTexture;
  uint padding0;
  uint padding1;
  vec4 emissive;
  uint emissiveTexture;
  uint alphaMode;
  float alphaCutoff;
  uint padding2;
};

layout(std430, set = 0, binding = 12) readonly buffer SceneMaterialRefs {
  LxSceneMaterialRefRecord materialRefs[];
};

layout(std430, set = 0, binding = 13) readonly buffer SceneSourceMaterialRecords {
  LxStandardPbrSourceRecord sourceMaterials[];
};
#endif

#ifndef LX_SCENE_TEXTURES_DECLARED
#define LX_SCENE_TEXTURES_DECLARED
layout(set = 0, binding = 14) uniform sampler2D SceneTextures[256];
#endif

vec4 lxSampleSceneTexture(uint textureSlot, vec2 uv) {
  return texture(SceneTextures[nonuniformEXT(textureSlot)], uv);
}

vec3 lxLoadStandardPbrNormal(uint textureSlot, vec2 uv, vec3 geometricNormal,
                             mat3 tangentFrame) {
  vec3 tangentNormal = lxSampleSceneTexture(textureSlot, uv).xyz * 2.0 - 1.0;
  vec3 mapped = normalize(tangentFrame * tangentNormal);
  return dot(mapped, mapped) > 0.0 ? mapped : normalize(geometricNormal);
}

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  uint materialRefIndex = materialIndex;
  LxSceneMaterialRefRecord materialRef = materialRefs[materialRefIndex];
  LxStandardPbrSourceRecord material =
      sourceMaterials[materialRef.sourceLocalMaterialIndex];

  vec4 baseColorTex = lxSampleSceneTexture(material.baseColorTexture, uv);
  vec4 metallicRoughnessTex =
      lxSampleSceneTexture(material.metallicRoughnessTexture, uv);
  vec4 occlusionTex = lxSampleSceneTexture(material.occlusionTexture, uv);
  vec4 emissiveTex = lxSampleSceneTexture(material.emissiveTexture, uv);

  LxMaterialSurface surface;
  surface.baseColor = material.baseColor.rgb * baseColorTex.rgb;
  surface.alpha = material.baseColor.a * baseColorTex.a;
  surface.metallic = material.metallic * metallicRoughnessTex.b;
  surface.roughness = material.roughness * metallicRoughnessTex.g;
  surface.normal = lxLoadStandardPbrNormal(material.normalTexture, uv,
                                           geometricNormal, tangentFrame);
  surface.ao = occlusionTex.r;
  surface.emissive = material.emissive.rgb * emissiveTex.rgb;
  return surface;
}

LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput) {
  LxPbrDirectInput pbrInput;
  pbrInput.baseColor = max(bsdfInput.baseColor, vec3(0.0));
  pbrInput.normal = bsdfInput.normal;
  pbrInput.viewDir = bsdfInput.wo;
  pbrInput.lightDir = bsdfInput.wi;
  pbrInput.lightColor = vec3(1.0);
  pbrInput.metallic = bsdfInput.metallic;
  pbrInput.roughness = bsdfInput.roughness;
  pbrInput.ao = bsdfInput.ao;
  pbrInput.emissive = bsdfInput.emissive;

  LxBsdfEvaluateOutput result;
  result.value = lxPbrDirectBrdf(pbrInput);
  return result;
}

LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput bsdfInput) {
  return lxSampleCosineHemisphereBsdf(bsdfInput);
}
