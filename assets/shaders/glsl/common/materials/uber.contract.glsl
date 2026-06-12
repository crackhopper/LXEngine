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
