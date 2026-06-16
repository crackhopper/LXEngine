// LX_MATERIAL_CONTRACT_BEGIN
// type: unlit-texture
// status: supported
// reflectionHash: unlit-texture-source-contract-v1
// storageAbiHash: unlit-texture-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: baseColorTexture required texture
// storageField: baseColorTexture textureSlot parameter baseColorTexture texture defaultTexture=white
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"
#include "../material_bsdf.glsl"

#ifndef LX_UNLIT_TEXTURE_SOURCE_RECORDS_DECLARED
#define LX_UNLIT_TEXTURE_SOURCE_RECORDS_DECLARED
struct LxSceneMaterialRefRecord {
  uint sourceStorageIndex;
  uint sourceLocalMaterialIndex;
  uint reserved0;
  uint reserved1;
};

struct LxUnlitTextureSourceRecord {
  uint baseColorTexture;
  uint padding0;
  uint padding1;
  uint padding2;
};

layout(std430, set = 0, binding = 12) readonly buffer SceneMaterialRefs {
  LxSceneMaterialRefRecord materialRefs[];
};

layout(std430, set = 0, binding = 13) readonly buffer SceneSourceMaterialRecords {
  LxUnlitTextureSourceRecord sourceMaterials[];
};
#endif

#ifndef LX_SCENE_TEXTURES_DECLARED
#define LX_SCENE_TEXTURES_DECLARED
layout(set = 0, binding = 14) uniform sampler2D SceneTextures[256];
#endif

vec4 lxSampleSceneTexture(uint textureSlot, vec2 uv) {
  return texture(SceneTextures[nonuniformEXT(textureSlot)], uv);
}

uint lxGetMaterialType() {
  return LX_MATERIAL_TYPE_UNLIT;
}

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv,
                                        vec3 geometricNormal,
                                        mat3 tangentFrame) {
  uint materialRefIndex = materialIndex;
  LxSceneMaterialRefRecord materialRef = materialRefs[materialRefIndex];
  LxUnlitTextureSourceRecord material =
      sourceMaterials[materialRef.sourceLocalMaterialIndex];
  vec4 baseColor = lxSampleSceneTexture(material.baseColorTexture, uv);

  LxMaterialSurface surface;
  surface.baseColor = baseColor.rgb;
  surface.alpha = baseColor.a;
  surface.metallic = 0.0;
  surface.roughness = 1.0;
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
