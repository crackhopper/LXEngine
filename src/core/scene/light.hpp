#pragma once
#include "../math/vec.hpp"


namespace LX_core {
  class LightBase{

  };

  class DirectionalLight: public LightBase {
  public:
    Vec3f lightDir = Vec3f(0.0f, -1.0f, 0.0f);
    Vec3f color = Vec3f(1.0f, 1.0f, 1.0f);
  };
}