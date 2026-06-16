#ifndef LX_MATERIAL_SURFACE_GLSL
#define LX_MATERIAL_SURFACE_GLSL

const uint LX_MATERIAL_TYPE_LIT = 0u;
const uint LX_MATERIAL_TYPE_UNLIT = 1u;

struct LxMaterialSurface {
  vec3 baseColor;
  float alpha;
  float metallic;
  float roughness;
  vec3 normal;
  float ao;
  vec3 emissive;
};

#endif
