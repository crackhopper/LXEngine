#ifndef LX_GAMMA_ADJUST_GLSL
#define LX_GAMMA_ADJUST_GLSL

vec4 lxApplyGammaAdjust(vec4 linearColor) {
  return vec4(pow(clamp(linearColor.rgb, vec3(0.0), vec3(1.0)),
                  vec3(1.0 / 2.2)),
              linearColor.a);
}

#endif
