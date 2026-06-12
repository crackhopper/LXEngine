// LX_MATERIAL_CONTRACT_BEGIN
// type: uber
// status: supported
// reflectionHash: uber-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: Ks required rgb texture spectrum
// parameter: Kr optional rgb texture spectrum
// parameter: Kt optional rgb texture spectrum
// parameter: opacity optional rgb texture
// parameter: eta optional float texture
// parameter: uroughness optional float texture
// parameter: vroughness optional float texture
// parameter: normalmap optional texture
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) {
  LxMaterialSurface surface;
  return surface;
}
