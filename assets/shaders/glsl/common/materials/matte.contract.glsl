// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: sigma required float texture
// parameter: normalmap optional texture
// storageField: baseColor vec4 parameter Kd value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter Kd texture defaultTexture=white
// storageField: baseColorChannel channelSelector parameter Kd channel default=rgba
// storageField: sigma float parameter sigma value default=0
// storageField: sigmaTexture textureSlot parameter sigma texture defaultTexture=white
// storageField: sigmaChannel channelSelector parameter sigma channel default=r
// storageField: normalTexture textureSlot parameter normalmap texture defaultTexture=flatNormal
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"

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
