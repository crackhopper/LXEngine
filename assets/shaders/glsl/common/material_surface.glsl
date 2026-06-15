#ifndef LX_MATERIAL_SURFACE_GLSL
#define LX_MATERIAL_SURFACE_GLSL

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
