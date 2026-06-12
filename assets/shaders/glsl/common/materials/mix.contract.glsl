// LX_MATERIAL_CONTRACT_BEGIN
// type: mix
// status: supported
// reflectionHash: mix-source-contract-v1
// storageAbiHash: pbrt-envelope-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: namedmaterial1 required materialRef
// parameter: namedmaterial2 required materialRef
// parameter: amount required float
// parameter: normalmap optional texture
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) {
  LxMaterialSurface surface;
  surface.baseColor = vec3(1.0);
  surface.alpha = 1.0;
  surface.metallic = 0.0;
  surface.roughness = 0.5;
  surface.normal = vec3(0.0, 0.0, 1.0);
  surface.ao = 1.0;
  surface.emissive = vec3(0.0);
  return surface;
}
