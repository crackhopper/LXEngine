// LX_MATERIAL_CONTRACT_BEGIN
// type: metal
// status: supported
// reflectionHash: metal-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: eta required spectrum
// parameter: k required spectrum
// parameter: uroughness optional float texture
// parameter: vroughness optional float texture
// parameter: normalmap optional texture
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"

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
