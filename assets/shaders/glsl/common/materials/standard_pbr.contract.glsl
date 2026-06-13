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

struct LxBsdfEvaluateInput {
  vec3 baseColor;
  vec3 normal;
};

struct LxBsdfEvaluateOutput {
  vec3 value;
};

struct LxBsdfSampleInput {
  vec3 normal;
};

struct LxBsdfSampleOutput {
  vec3 wi;
  vec3 value;
  float pdf;
};

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  surface.baseColor = vec3(1.0);
  surface.alpha = 1.0;
  surface.metallic = 1.0;
  surface.roughness = 1.0;
  surface.normal = dot(geometricNormal, geometricNormal) > 0.0
                       ? normalize(geometricNormal)
                       : vec3(0.0, 0.0, 1.0);
  surface.ao = 1.0;
  surface.emissive = vec3(0.0);
  return surface;
}

LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput) {
  LxBsdfEvaluateOutput output;
  output.value = bsdfInput.baseColor;
  return output;
}

LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput bsdfInput) {
  LxBsdfSampleOutput output;
  output.wi = bsdfInput.normal;
  output.value = vec3(1.0);
  output.pdf = 1.0;
  return output;
}
