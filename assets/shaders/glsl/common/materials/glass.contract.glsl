// LX_MATERIAL_CONTRACT_BEGIN
// type: glass
// status: supported
// reflectionHash: glass-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kr required rgb texture spectrum
// parameter: Kt required rgb texture spectrum
// parameter: eta required float texture
// parameter: uroughness required float texture
// parameter: vroughness required float texture
// parameter: normalmap optional texture
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) {
  LxMaterialSurface surface;
  return surface;
}
