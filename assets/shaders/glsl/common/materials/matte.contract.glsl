// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: sigma required float texture
// parameter: normalmap optional texture
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) {
  LxMaterialSurface surface;
  return surface;
}
