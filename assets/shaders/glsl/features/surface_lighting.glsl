#ifndef LX_FEATURE_SURFACE_LIGHTING_GLSL
#define LX_FEATURE_SURFACE_LIGHTING_GLSL

layout(std140, set = 4, binding = 2) uniform SurfaceLightingUBO {
  uint enableIblLighting;
  float diffuseIblIntensity;
  float specularIblIntensity;
  uint environmentIblReady;
  uint standardPbrIblReady;
} surfaceLighting;

bool lxSurfaceLightingStandardPbrIblReady() {
  return surfaceLighting.enableIblLighting != 0u &&
         surfaceLighting.environmentIblReady != 0u &&
         surfaceLighting.standardPbrIblReady != 0u;
}

#endif
