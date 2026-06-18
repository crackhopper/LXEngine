#version 450

#include "features/surface_lighting.glsl"

layout(location = 0) out vec4 outColor;

void main() {
  outColor = vec4(surfaceLighting.diffuseIblIntensity,
                  surfaceLighting.specularIblIntensity,
                  lxSurfaceLightingStandardPbrIblReady() ? 1.0 : 0.0, 1.0);
}
